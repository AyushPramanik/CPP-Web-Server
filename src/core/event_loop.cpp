#include "core/event_loop.hpp"

#include "util/logger.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace cppws::core {

EventLoop::EventLoop(uint16_t port, std::size_t n_workers)
    : listen_socket_(net::Socket::make_tcp()),
      epoll_(MAX_EVENTS),
      thread_pool_(n_workers) {

  listen_socket_.set_reuse_addr(true);
  listen_socket_.set_reuse_port(true);
  listen_socket_.bind(port);
  listen_socket_.listen();
  epoll_.add(listen_socket_.fd(), EPOLLIN | EPOLLET);

  // eventfd: EFD_NONBLOCK so eventfd_read never blocks on the event loop thread.
  // EFD_CLOEXEC: don't inherit across exec().
  // The semaphore flag (EFD_SEMAPHORE) is NOT used — we read the entire counter
  // at once to drain all pending wake signals in one syscall.
  wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeup_fd_ < 0) {
    throw std::runtime_error("eventfd() failed");
  }
  epoll_.add(wakeup_fd_, EPOLLIN | EPOLLET);

  LOG_INFO("EventLoop ready on port {} ({} workers)", port, thread_pool_.thread_count());
}

EventLoop::~EventLoop() {
  if (wakeup_fd_ >= 0) {
    ::close(wakeup_fd_);
    wakeup_fd_ = -1;
  }
  thread_pool_.shutdown();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void EventLoop::run() {
  running_.store(true, std::memory_order_relaxed);

  while (running_.load(std::memory_order_relaxed)) {
    const auto events = epoll_.wait(EPOLL_TIMEOUT_MS);

    for (const auto& event : events) {
      handle_event(event);
    }

    // Reap dead connections outside the event loop
    std::vector<int> dead;
    for (const auto& [fd, conn] : connections_) {
      if (!conn->is_alive()) dead.push_back(fd);
    }
    for (int dead_fd : dead) remove_connection(dead_fd);
  }

  LOG_INFO("EventLoop draining {} connections", connections_.size());
  thread_pool_.shutdown();
  connections_.clear();
}

// ── Worker-facing API ─────────────────────────────────────────────────────────

void EventLoop::post_response(net::ConnectionPtr conn, std::string data) {
  // Called from worker threads — must be lock-safe.
  // push() is thread-safe (mutex inside TaskQueue).
  pending_writes_.push(PendingWrite{std::move(conn), std::move(data)});

  // Signal the event loop to wake up from epoll_wait.
  // Writing 1 increments the eventfd counter; epoll sees it as readable.
  const uint64_t val = 1;
  if (::write(wakeup_fd_, &val, sizeof(val)) < 0 && errno != EAGAIN) {
    LOG_WARN("eventfd write failed: {}", std::strerror(errno));
  }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void EventLoop::accept_new_connections() {
  while (true) {
    net::Socket client = listen_socket_.accept();
    if (!client.is_valid()) break;

    const int client_fd   = client.fd();
    const uint64_t conn_id = next_conn_id_++;

    auto conn = std::make_shared<net::Connection>(std::move(client), conn_id);
    conn->set_close_callback([this](net::Connection& c) {
      epoll_.remove(c.fd());
    });

    connections_.emplace(client_fd, conn);
    epoll_.add(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP);

    if (connection_handler_) {
      connection_handler_(conn);
    }
  }
}

void EventLoop::drain_pending_writes() {
  // Drain the eventfd counter (read clears it atomically).
  uint64_t count = 0;
  if (::read(wakeup_fd_, &count, sizeof(count)) < 0 && errno != EAGAIN) {
    LOG_WARN("eventfd read failed: {}", std::strerror(errno));
  }

  // Drain all pending writes from worker threads.
  // try_pop() is non-blocking — stop when queue is empty.
  while (auto pw = pending_writes_.try_pop()) {
    auto& conn = pw->conn;
    if (!conn || !conn->is_alive()) continue;

    conn->enqueue_write(pw->data.data(), pw->data.size());
    const std::size_t remaining = conn->flush_write();

    if (remaining > 0) {
      // Partial write: register EPOLLOUT so we can flush the rest later.
      epoll_.modify(conn->fd(), EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
    }
  }
}

void EventLoop::handle_event(const net::EpollEvent& event) {
  if (event.fd == listen_socket_.fd()) {
    accept_new_connections();
    return;
  }

  if (event.fd == wakeup_fd_) {
    drain_pending_writes();
    return;
  }

  auto it = connections_.find(event.fd);
  if (it == connections_.end()) {
    LOG_DEBUG("Stale event for fd={}", event.fd);
    return;
  }

  net::Connection& conn = *it->second;

  if (event.is_closed()) {
    conn.on_closed();
    return;
  }

  if (event.is_readable()) {
    if (!conn.on_readable()) return;
  }

  if (event.is_writable()) {
    const std::size_t remaining = conn.flush_write();
    if (remaining == 0) {
      // Write buffer drained — remove EPOLLOUT to stop spurious wakeups
      epoll_.modify(event.fd, EPOLLIN | EPOLLET | EPOLLRDHUP);
    }
  }
}

void EventLoop::remove_connection(int conn_fd) {
  epoll_.remove(conn_fd);
  connections_.erase(conn_fd);
}

} // namespace cppws::core
