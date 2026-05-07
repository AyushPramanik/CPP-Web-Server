#include "core/event_loop.hpp"

#include "util/logger.hpp"

#include <sys/epoll.h>

namespace cppws::core {

EventLoop::EventLoop(uint16_t port)
    : listen_socket_(net::Socket::make_tcp()),
      epoll_(MAX_EVENTS) {

  // SO_REUSEADDR: survive quick restarts (TIME_WAIT state)
  // SO_REUSEPORT: allow future multi-loop workers to bind the same port
  listen_socket_.set_reuse_addr(true);
  listen_socket_.set_reuse_port(true);
  listen_socket_.bind(port);
  listen_socket_.listen();

  // Register the listen socket for EPOLLIN | EPOLLET.
  // No EPOLLOUT on the listen socket — we only accept() on it.
  epoll_.add(listen_socket_.fd(), EPOLLIN | EPOLLET);

  LOG_INFO("EventLoop listening on port {}", port);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void EventLoop::run() {
  running_.store(true, std::memory_order_relaxed);
  LOG_INFO("EventLoop started, active_connections={}", connections_.size());

  while (running_.load(std::memory_order_relaxed)) {
    // epoll_wait: block up to EPOLL_TIMEOUT_MS, then loop to check running_.
    // The timeout prevents the loop from sleeping forever when stop() is called
    // from a signal handler (which cannot write to the epoll fd).
    const auto events = epoll_.wait(EPOLL_TIMEOUT_MS);

    for (const auto& ev : events) {
      handle_event(ev);
    }

    // Collect dead connections outside the event loop to avoid
    // iterator invalidation during removal.
    std::vector<int> to_remove;
    for (const auto& [fd, conn] : connections_) {
      if (!conn->is_alive()) {
        to_remove.push_back(fd);
      }
    }
    for (int dead_fd : to_remove) {
      remove_connection(dead_fd);
    }
  }

  LOG_INFO("EventLoop stopped, draining {} connections", connections_.size());
  connections_.clear();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void EventLoop::accept_new_connections() {
  // Edge-triggered: must loop accept() until EAGAIN.
  // Under high connection rate, many connections may queue up between
  // two epoll_wait calls — we must drain all of them.
  while (true) {
    net::Socket client = listen_socket_.accept();
    if (!client.is_valid()) {
      break; // EAGAIN — no more pending connections
    }

    const int client_fd = client.fd();
    const uint64_t conn_id = next_conn_id_++;

    auto conn = std::make_shared<net::Connection>(std::move(client), conn_id);

    // Close callback: remove from our map when the connection dies
    conn->set_close_callback([this](net::Connection& c) {
      epoll_.remove(c.fd());
    });

    connections_.emplace(client_fd, conn);

    // Register for EPOLLIN | EPOLLET | EPOLLRDHUP.
    // EPOLLRDHUP: detect half-close without needing a failed read().
    // Start with no EPOLLOUT — add it only when we have data to write.
    epoll_.add(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP);

    LOG_DEBUG("Accepted connection [{}] fd={}", conn_id, client_fd);

    // Notify the HTTP layer (or any higher-level handler)
    if (connection_handler_) {
      connection_handler_(conn);
    }
  }
}

void EventLoop::handle_event(const net::EpollEvent& event) {
  if (event.fd == listen_socket_.fd()) {
    accept_new_connections();
    return;
  }

  auto it = connections_.find(event.fd);
  if (it == connections_.end()) {
    LOG_DEBUG("Event for unknown fd={}, ignoring", event.fd);
    return;
  }

  net::Connection& conn = *it->second;

  if (event.is_closed()) {
    conn.on_closed();
    return;
  }

  if (event.is_readable()) {
    if (!conn.on_readable()) {
      return;
    }
  }

  if (event.is_writable()) {
    const std::size_t remaining = conn.flush_write();
    if (remaining == 0) {
      epoll_.modify(event.fd, EPOLLIN | EPOLLET | EPOLLRDHUP);
    }
  }
}

void EventLoop::remove_connection(int conn_fd) {
  epoll_.remove(conn_fd);
  connections_.erase(conn_fd);
  LOG_DEBUG("Removed connection fd={}, active={}", conn_fd, connections_.size());
}

} // namespace cppws::core
