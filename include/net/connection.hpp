#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Connection — per-TCP-connection state machine
//
// State transitions:
//
//   READING ──(request complete)──▶ PROCESSING ──(response ready)──▶ WRITING
//      ▲                                                                  │
//      │                (keep-alive & request pipeline empty)             │
//      └──────────────────────────────────────────────────────────────────┘
//      │                           (keep-alive off || peer closed)        │
//      └───────────────────────── CLOSING ◀────────────────────────────────
//
// Key design rules (from edge-triggered epoll):
//   1. on_readable(): loop read() until EAGAIN — must drain completely.
//   2. on_writable(): loop write() until EAGAIN or all bytes sent.
//      If partial write: keep EPOLLOUT registered.
//      If write complete: deregister EPOLLOUT (avoid spurious wakeups).
//   3. Never block: any syscall that could block is a bug.
//
// The Connection does NOT know about HTTP — that's the HTTP layer's job.
// It exposes the read buffer for the parser to consume and lets callers
// enqueue raw bytes into the write buffer.
// ─────────────────────────────────────────────────────────────────────────────

#include "net/buffer.hpp"
#include "net/socket.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cppws::net {

class Connection;
using ConnectionPtr = std::shared_ptr<Connection>;

// Callback types used by the event loop to notify higher layers
using ReadCallback  = std::function<void(Connection&)>;
using CloseCallback = std::function<void(Connection&)>;

// ─────────────────────────────────────────────────────────────────────────────
// Connection
// ─────────────────────────────────────────────────────────────────────────────
class Connection : public std::enable_shared_from_this<Connection> {
public:
  enum class State { Reading, Writing, Closing, Closed };

  // id: monotonically increasing connection ID (for logging/debugging)
  Connection(Socket socket, uint64_t id);

  // Not copyable or movable — shared_from_this requires a stable address
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  ~Connection();

  // ── Event handlers (called by EventLoop) ────────────────────────────────────

  // Called when epoll reports EPOLLIN. Reads until EAGAIN.
  // Returns false if the connection should be closed.
  [[nodiscard]] bool on_readable();

  // Called when epoll reports EPOLLOUT. Flushes write buffer until EAGAIN
  // or fully drained. Returns false if the connection should be closed.
  [[nodiscard]] bool on_writable();

  // Called on EPOLLHUP | EPOLLERR | peer close.
  void on_closed();

  // ── Write API ───────────────────────────────────────────────────────────────

  // Append data to the write buffer. Does NOT flush immediately — call
  // flush_write() or wait for EPOLLOUT to trigger on_writable().
  void enqueue_write(const char* data, std::size_t len);
  void enqueue_write(std::string_view sv) { enqueue_write(sv.data(), sv.size()); }

  // Attempt to flush the write buffer right now. If the socket would block,
  // returns the number of bytes remaining (caller should register EPOLLOUT).
  std::size_t flush_write();

  // ── Callbacks ───────────────────────────────────────────────────────────────

  void set_read_callback(ReadCallback cb) { on_read_ = std::move(cb); }
  void set_close_callback(CloseCallback cb) { on_close_ = std::move(cb); }

  // ── Accessors ───────────────────────────────────────────────────────────────

  [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
  [[nodiscard]] uint64_t id() const noexcept { return id_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool is_alive() const noexcept { return state_ != State::Closed; }

  // Direct access to the read buffer for zero-copy parsing
  [[nodiscard]] Buffer& read_buf() noexcept { return read_buf_; }
  [[nodiscard]] const Buffer& read_buf() const noexcept { return read_buf_; }

  // Keep-alive control (HTTP layer sets this after parsing Connection header)
  void set_keep_alive(bool val) noexcept { keep_alive_ = val; }
  [[nodiscard]] bool keep_alive() const noexcept { return keep_alive_; }

private:
  Socket socket_;
  uint64_t id_;
  State state_{State::Reading};

  Buffer read_buf_;
  Buffer write_buf_;

  bool keep_alive_{true};

  ReadCallback  on_read_;
  CloseCallback on_close_;
};

} // namespace cppws::net
