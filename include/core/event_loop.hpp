#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// EventLoop — epoll-based Reactor
//
// The Reactor pattern (defined by Schmidt, 1995) separates:
//   1. Event demultiplexing — epoll_wait
//   2. Event dispatching    — call the right handler per fd
//   3. Request handling     — done by Connection / HTTP layer callbacks
//
// One EventLoop per thread. In Phase 4 we will run N event loops, one per
// worker thread, each with its own epoll and its own connection set.
// This is the "multi-reactor" or "one-loop-per-thread" model used by
// Nginx (worker processes), libuv, and Netty.
//
// EventLoop responsibilities:
//   • Accept new TCP connections on the listen socket
//   • Register accepted connections with epoll
//   • Dispatch readable/writable/close events to Connection objects
//   • Remove dead connections from the connection map
//   • Check the shutdown flag and exit cleanly
// ─────────────────────────────────────────────────────────────────────────────

#include "net/connection.hpp"
#include "net/epoll.hpp"
#include "net/socket.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace cppws::core {

// ─────────────────────────────────────────────────────────────────────────────
// EventLoop
// ─────────────────────────────────────────────────────────────────────────────
class EventLoop {
public:
  // Callback invoked for each accepted Connection (HTTP layer registers here)
  using ConnectionHandler = std::function<void(net::ConnectionPtr)>;

  // epoll_wait timeout in milliseconds.
  // 100ms: short enough to notice shutdown quickly, long enough to not spin.
  static constexpr int EPOLL_TIMEOUT_MS = 100;

  // How many events to process per epoll_wait batch
  static constexpr int MAX_EVENTS = 1024;

  explicit EventLoop(uint16_t port);

  ~EventLoop() = default;
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  // ── Lifecycle ───────────────────────────────────────────────────────────────

  // Enter the event loop. Blocks until stop() is called.
  void run();

  // Signal the event loop to exit after the current iteration.
  // Thread-safe (called from signal handler or another thread).
  void stop() noexcept { running_.store(false, std::memory_order_relaxed); }

  [[nodiscard]] bool is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
  }

  // ── Configuration ───────────────────────────────────────────────────────────

  void set_connection_handler(ConnectionHandler handler) {
    connection_handler_ = std::move(handler);
  }

  // ── Stats (for /metrics endpoint in Phase 6) ────────────────────────────────

  [[nodiscard]] std::size_t active_connections() const noexcept {
    return connections_.size();
  }

  [[nodiscard]] uint64_t total_connections() const noexcept {
    return next_conn_id_;
  }

private:
  // ── Internal helpers ────────────────────────────────────────────────────────

  void accept_new_connections();
  void handle_event(const net::EpollEvent& event);
  void remove_connection(int conn_fd);

  // ── Members ─────────────────────────────────────────────────────────────────

  net::Socket listen_socket_;
  net::Epoll  epoll_;

  // fd → Connection map. unordered_map gives O(1) lookup per event.
  // In Phase 5 we may replace with an intrusive list for cache friendliness.
  std::unordered_map<int, net::ConnectionPtr> connections_;

  std::atomic<bool> running_{false};
  uint64_t next_conn_id_{0};

  ConnectionHandler connection_handler_;
};

} // namespace cppws::core
