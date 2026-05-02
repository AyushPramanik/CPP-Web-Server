#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Socket — RAII wrapper around a POSIX file descriptor
//
// Core insight: in a non-blocking server EVERY fd must be O_NONBLOCK.
// read()/write() then return EAGAIN instead of blocking when the kernel
// buffer is empty/full. The event loop re-registers the fd with epoll and
// moves on to other connections. This is the fundamental difference between
// a server that handles 100 connections and one that handles 100,000.
//
// RAII ensures:
//   • fd is always closed when the Socket object is destroyed
//   • No double-close (move clears the fd)
//   • No accidental copy (copying an fd leads to subtle close-ordering bugs)
// ─────────────────────────────────────────────────────────────────────────────

#include "util/logger.hpp"

#include <cstdint>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppws::net {

// ─────────────────────────────────────────────────────────────────────────────
// Socket
//
// Thin RAII wrapper. Does not perform I/O — read()/write() live on Connection.
// ─────────────────────────────────────────────────────────────────────────────
class Socket {
public:
  static constexpr int INVALID_FD = -1;

  // ── Construction ────────────────────────────────────────────────────────────

  // Adopt an existing raw_fd (e.g. from accept()).
  // Immediately sets O_NONBLOCK — callers must not pass blocking fds.
  explicit Socket(int raw_fd);

  // Create a new TCP/IPv4 listening socket (SOCK_STREAM | SOCK_NONBLOCK).
  // SOCK_NONBLOCK is set atomically in socket() to avoid a TOCTOU race with
  // fcntl() on kernels >= 2.6.27 — the same technique NGINX uses.
  static Socket make_tcp();

  // Not copyable: two Sockets owning the same fd would double-close it.
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // Move transfers ownership; source becomes invalid.
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  ~Socket();

  // ── Accessors ───────────────────────────────────────────────────────────────

  [[nodiscard]] int fd() const noexcept { return fd_; }
  [[nodiscard]] bool is_valid() const noexcept { return fd_ != INVALID_FD; }

  // ── Server-side setup ───────────────────────────────────────────────────────

  // SO_REUSEADDR: allows rebinding to a port that is in TIME_WAIT state.
  // Without this, restarting the server after a crash fails with EADDRINUSE
  // for up to 2*MSL (typically 60–120 seconds). Essential for fast restarts.
  void set_reuse_addr(bool enable = true);

  // SO_REUSEPORT (Linux 3.9+): multiple sockets can bind to the same port.
  // Each worker can have its own accept socket, eliminating the accept lock
  // that is a bottleneck in multi-process servers (NGINX worker model).
  void set_reuse_port(bool enable = true);

  // Disable Nagle's algorithm. Nagle batches small writes into one TCP segment
  // to improve throughput but adds ~40ms latency on small responses.
  // For HTTP/1.1 request-response patterns, TCP_NODELAY is almost always right.
  void set_tcp_nodelay(bool enable = true);

  // Set socket receive/send buffer sizes. The kernel doubles what you set.
  // Tuning these can improve throughput under high load.
  void set_recv_buf(int bytes);
  void set_send_buf(int bytes);

  // Bind to the given port on all interfaces (INADDR_ANY).
  void bind(uint16_t port);

  // Linux ignores backlog > /proc/sys/net/core/somaxconn (default 4096).
  static constexpr int DEFAULT_BACKLOG = 4096;
  void listen(int backlog = DEFAULT_BACKLOG);

  // Accept a new connection. Returns an invalid Socket if EAGAIN (no pending
  // connections in non-blocking mode). Throws on real errors.
  [[nodiscard]] Socket accept();

  // Close the fd and mark invalid. Called by destructor; safe to call earlier.
  void close() noexcept;

private:
  // Default ctor creates an invalid socket; used by make_tcp() and accept()
  // which set fd_ directly before returning, keeping the factory clean.
  Socket() = default;

  int fd_{INVALID_FD};

  void set_nonblocking();
};

} // namespace cppws::net
