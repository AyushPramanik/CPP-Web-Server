#include "net/socket.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppws::net {

namespace {

[[noreturn]] void throw_errno(const char* ctx) {
  throw std::runtime_error(std::string(ctx) + ": " + std::strerror(errno));
}

int setsockopt_int(int fd, int level, int opt, int val) {
  return ::setsockopt(fd, level, opt, &val, sizeof(val));
}

} // namespace

// ── Construction ──────────────────────────────────────────────────────────────

Socket::Socket(int raw_fd) : fd_(raw_fd) {
  if (fd_ != INVALID_FD) {
    set_nonblocking();
  }
}

Socket Socket::make_tcp() {
  // SOCK_NONBLOCK: set atomically — avoids the race window between socket()
  // and a subsequent fcntl() call on multi-threaded accept paths.
  int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    throw_errno("socket()");
  }
  // Construct directly: fd is already non-blocking, skip redundant fcntl
  Socket s;
  s.fd_ = fd;
  return s;
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = INVALID_FD;
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = INVALID_FD;
  }
  return *this;
}

Socket::~Socket() {
  close();
}

// ── Configuration ─────────────────────────────────────────────────────────────

void Socket::set_reuse_addr(bool enable) {
  int val = enable ? 1 : 0;
  if (setsockopt_int(fd_, SOL_SOCKET, SO_REUSEADDR, val) < 0) {
    throw_errno("setsockopt(SO_REUSEADDR)");
  }
}

void Socket::set_reuse_port(bool enable) {
  int val = enable ? 1 : 0;
  if (setsockopt_int(fd_, SOL_SOCKET, SO_REUSEPORT, val) < 0) {
    throw_errno("setsockopt(SO_REUSEPORT)");
  }
}

void Socket::set_tcp_nodelay(bool enable) {
  int val = enable ? 1 : 0;
  if (setsockopt_int(fd_, IPPROTO_TCP, TCP_NODELAY, val) < 0) {
    throw_errno("setsockopt(TCP_NODELAY)");
  }
}

void Socket::set_recv_buf(int bytes) {
  if (setsockopt_int(fd_, SOL_SOCKET, SO_RCVBUF, bytes) < 0) {
    throw_errno("setsockopt(SO_RCVBUF)");
  }
}

void Socket::set_send_buf(int bytes) {
  if (setsockopt_int(fd_, SOL_SOCKET, SO_SNDBUF, bytes) < 0) {
    throw_errno("setsockopt(SO_SNDBUF)");
  }
}

void Socket::bind(uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw_errno("bind()");
  }
}

void Socket::listen(int backlog) {
  if (::listen(fd_, backlog) < 0) {
    throw_errno("listen()");
  }
}

Socket Socket::accept() {
  sockaddr_in peer{};
  socklen_t len = sizeof(peer);

  // SOCK_NONBLOCK on accept4() sets O_NONBLOCK atomically on the new fd.
  // This is a Linux extension (glibc 2.10+, kernel 2.6.28+) that NGINX also
  // uses to avoid the fcntl() round-trip per accepted connection.
  int client_fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&peer), &len, SOCK_NONBLOCK);

  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // No pending connections — normal in non-blocking mode
      return Socket{};
    }
    if (errno == ECONNABORTED) {
      // Client disconnected before we could accept — not fatal
      LOG_WARN("accept4: client aborted connection");
      return Socket{};
    }
    throw_errno("accept4()");
  }

  // Construct directly: accept4 with SOCK_NONBLOCK already set the flag
  Socket s;
  s.fd_ = client_fd;
  return s;
}

void Socket::close() noexcept {
  if (fd_ != INVALID_FD) {
    ::close(fd_);
    fd_ = INVALID_FD;
  }
}

// ── Private ───────────────────────────────────────────────────────────────────

void Socket::set_nonblocking() {
  int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags < 0) {
    throw_errno("fcntl(F_GETFL)");
  }
  if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw_errno("fcntl(F_SETFL, O_NONBLOCK)");
  }
}

} // namespace cppws::net
