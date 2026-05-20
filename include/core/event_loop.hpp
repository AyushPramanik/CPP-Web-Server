#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// EventLoop — epoll Reactor with integrated thread pool
//
// Phase 4 additions:
//   • ThreadPool: N worker threads execute HTTP handlers off the event loop.
//   • eventfd (wakeup_fd_): workers signal the event loop when a response is
//     ready without blocking on a mutex.
//   • TaskQueue<PendingWrite> (pending_writes_): the MPSC channel from workers
//     back to the event loop.  Only the event loop reads it; workers push to it.
//
// Data flow:
//
//   accept()
//     → Connection registered with epoll
//     → on EPOLLIN: parser feeds data
//     → on complete request: ThreadPool::submit(handler_task)
//
//   Worker thread:
//     → run router.dispatch()
//     → push PendingWrite to pending_writes_
//     → eventfd_write(wakeup_fd_, 1)
//
//   Event loop (wakeup_fd_ readable):
//     → eventfd_read(wakeup_fd_)   — drains the counter
//     → drain pending_writes_      — pull all ready responses
//     → conn->enqueue_write(data)
//     → conn->flush_write()
//     → if partial: epoll MOD to add EPOLLOUT
// ─────────────────────────────────────────────────────────────────────────────

#include "core/pending_write.hpp"
#include "core/task_queue.hpp"
#include "core/thread_pool.hpp"
#include "net/connection.hpp"
#include "net/epoll.hpp"
#include "net/socket.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace cppws::core {

class EventLoop {
public:
  using ConnectionHandler = std::function<void(net::ConnectionPtr)>;

  static constexpr int EPOLL_TIMEOUT_MS = 100;
  static constexpr int MAX_EVENTS       = 1024;

  // port: TCP port to listen on.
  // n_workers: thread pool size (0 = hardware_concurrency).
  explicit EventLoop(uint16_t port, std::size_t n_workers = 0);

  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  // ── Lifecycle ───────────────────────────────────────────────────────────────

  void run();
  void stop() noexcept { running_.store(false, std::memory_order_relaxed); }

  [[nodiscard]] bool is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
  }

  // ── Configuration ───────────────────────────────────────────────────────────

  void set_connection_handler(ConnectionHandler handler) {
    connection_handler_ = std::move(handler);
  }

  // ── Worker-facing API ────────────────────────────────────────────────────────

  // Called by worker threads to post a completed response back to the loop.
  // Thread-safe. Signals the event loop via eventfd.
  void post_response(net::ConnectionPtr conn, std::string data);

  // ── Stats ───────────────────────────────────────────────────────────────────

  [[nodiscard]] std::size_t active_connections() const noexcept {
    return connections_.size();
  }
  [[nodiscard]] uint64_t total_connections() const noexcept {
    return next_conn_id_;
  }

private:
  void accept_new_connections();
  void handle_event(const net::EpollEvent& event);
  void drain_pending_writes();
  void remove_connection(int conn_fd);

  net::Socket listen_socket_;
  net::Epoll  epoll_;
  int wakeup_fd_{-1}; // eventfd: workers write here to wake epoll_wait

  std::unordered_map<int, net::ConnectionPtr> connections_;

  // Pending writes from worker threads — only event loop reads, workers write.
  // Using try_pop (non-blocking) in drain_pending_writes.
  TaskQueue<PendingWrite> pending_writes_;

  ThreadPool thread_pool_;

  std::atomic<bool> running_{false};
  uint64_t next_conn_id_{0};

  ConnectionHandler connection_handler_;
};

} // namespace cppws::core
