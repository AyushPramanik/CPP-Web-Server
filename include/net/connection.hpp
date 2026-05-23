#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Connection — per-TCP-connection state machine
//
// Phase 5 additions:
//   SendfileOp: tracks an in-progress sendfile(2) transfer.
//   flush_write() first drains the write buffer (headers), then
//   calls sendfile() in a loop until EAGAIN or transfer complete.
//
// sendfile(2) zero-copy path:
//   Normal write:   app buffer → kernel socket buffer (1 copy + 1 ctx switch)
//   sendfile:       page cache → kernel socket buffer (0 copies in userspace)
//   For large static files the difference is measurable at ~1 GB/s disk I/O.
// ─────────────────────────────────────────────────────────────────────────────

#include "net/buffer.hpp"
#include "net/socket.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace cppws::net {

class Connection;
using ConnectionPtr = std::shared_ptr<Connection>;

using ReadCallback  = std::function<void(Connection&)>;
using CloseCallback = std::function<void(Connection&)>;

// ─────────────────────────────────────────────────────────────────────────────
// SendfileOp — RAII owner of an in-progress sendfile(2) transfer
//
// Owns the file fd (closes it on destruction). Tracks byte offset so
// subsequent EPOLLOUT events can resume where the previous call stopped.
// ─────────────────────────────────────────────────────────────────────────────
struct SendfileOp {
  int file_fd_{-1};
  off_t offset_{0};
  off_t remaining_{0};

  SendfileOp() = default;
  SendfileOp(int file_fd, off_t size) : file_fd_(file_fd), remaining_(size) {}

  ~SendfileOp();

  SendfileOp(const SendfileOp&) = delete;
  SendfileOp& operator=(const SendfileOp&) = delete;
  SendfileOp(SendfileOp&&) noexcept;
  SendfileOp& operator=(SendfileOp&&) noexcept;

  [[nodiscard]] bool active() const noexcept {
    return file_fd_ >= 0 && remaining_ > 0;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Connection
// ─────────────────────────────────────────────────────────────────────────────
class Connection : public std::enable_shared_from_this<Connection> {
public:
  enum class State { Reading, Writing, Closing, Closed };

  using Clock     = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  Connection(Socket socket, uint64_t conn_id);

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  ~Connection();

  // ── Event handlers ──────────────────────────────────────────────────────────

  [[nodiscard]] bool on_readable();
  [[nodiscard]] bool on_writable();
  void on_closed();

  // ── Write API ───────────────────────────────────────────────────────────────

  void enqueue_write(const char* data, std::size_t len);
  void enqueue_write(std::string_view sv) { enqueue_write(sv.data(), sv.size()); }

  // Zero-copy: queue a sendfile after the write buffer is drained.
  // file_fd is OWNED by this Connection from this point on.
  void begin_sendfile(int file_fd, off_t size);

  // Flush the write buffer and any pending sendfile. Returns bytes remaining.
  // Non-zero means the socket is not yet ready — caller should register EPOLLOUT.
  [[nodiscard]] std::size_t flush_write();

  // ── Callbacks ───────────────────────────────────────────────────────────────

  void set_read_callback(ReadCallback cb) { on_read_ = std::move(cb); }
  void set_close_callback(CloseCallback cb) { on_close_ = std::move(cb); }

  // ── Accessors ───────────────────────────────────────────────────────────────

  [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
  [[nodiscard]] uint64_t id() const noexcept { return id_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool is_alive() const noexcept { return state_ != State::Closed; }

  [[nodiscard]] Buffer& read_buf() noexcept { return read_buf_; }
  [[nodiscard]] const Buffer& read_buf() const noexcept { return read_buf_; }

  void set_keep_alive(bool val) noexcept { keep_alive_ = val; }
  [[nodiscard]] bool keep_alive() const noexcept { return keep_alive_; }

  // ── Timeout tracking ────────────────────────────────────────────────────────

  // Record the timestamp of the last I/O activity.
  void touch() noexcept { last_active_ = Clock::now(); }
  [[nodiscard]] TimePoint last_active() const noexcept { return last_active_; }

  // True if the connection has been idle for longer than `timeout`.
  [[nodiscard]] bool is_timed_out(std::chrono::seconds timeout) const noexcept {
    return (Clock::now() - last_active_) > timeout;
  }

private:
  Socket socket_;
  uint64_t id_;
  State state_{State::Reading};

  Buffer read_buf_;
  Buffer write_buf_;

  std::optional<SendfileOp> sendfile_op_{};

  bool keep_alive_{true};
  TimePoint last_active_{Clock::now()};

  ReadCallback  on_read_;
  CloseCallback on_close_;
};

} // namespace cppws::net
