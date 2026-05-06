#include "net/connection.hpp"

#include "util/logger.hpp"

#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>

namespace cppws::net {

Connection::Connection(Socket socket, uint64_t id)
    : socket_(std::move(socket)), id_(id) {
  LOG_DEBUG("Connection [{}] opened fd={}", id_, socket_.fd());
}

Connection::~Connection() {
  LOG_DEBUG("Connection [{}] destroyed", id_);
}

// ── Event handlers ────────────────────────────────────────────────────────────

bool Connection::on_readable() {
  // Edge-triggered: we MUST read until EAGAIN or we miss data on this fd.
  // Each iteration fills the remaining writable space in the read buffer.
  while (true) {
    // Compact if the write end is full but there is consumed space at the front.
    // This avoids a realloc while keeping the buffer sized at construction.
    if (read_buf_.writable_bytes() == 0) {
      const std::size_t recovered = read_buf_.compact();
      if (recovered == 0) {
        // Buffer is full of unconsumed data — the client is sending too fast
        // or the parser is stalling. Close to apply backpressure.
        LOG_WARN("Connection [{}]: read buffer full, closing", id_);
        on_closed();
        return false;
      }
    }

    const ssize_t n = ::recv(
        socket_.fd(),
        read_buf_.write_ptr(),
        read_buf_.writable_bytes(),
        0);

    if (n > 0) {
      read_buf_.commit_write(static_cast<std::size_t>(n));
      LOG_TRACE("Connection [{}]: recv {} bytes", id_, n);
      // Call the parser/HTTP layer callback
      if (on_read_) {
        on_read_(*this);
      }
      // Loop: there may be more data in the kernel buffer
      continue;
    }

    if (n == 0) {
      // Peer performed a graceful half-close (FIN).
      LOG_DEBUG("Connection [{}]: peer closed", id_);
      on_closed();
      return false;
    }

    // n < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Kernel buffer drained — edge-triggered contract fulfilled.
      return true;
    }

    if (errno == EINTR) {
      continue; // Interrupted by signal, retry
    }

    LOG_WARN("Connection [{}]: recv error: {}", id_, std::strerror(errno));
    on_closed();
    return false;
  }
}

bool Connection::on_writable() {
  const std::size_t remaining = flush_write();
  if (state_ == State::Closed) {
    return false;
  }
  // If the write buffer is now empty and we're not keeping alive, close.
  if (remaining == 0 && !keep_alive_) {
    on_closed();
    return false;
  }
  return true;
}

void Connection::on_closed() {
  if (state_ == State::Closed) {
    return;
  }
  state_ = State::Closed;
  LOG_DEBUG("Connection [{}] closing", id_);
  if (on_close_) {
    on_close_(*this);
  }
  socket_.close();
}

// ── Write path ────────────────────────────────────────────────────────────────

void Connection::enqueue_write(const char* data, std::size_t len) {
  // Ensure there is writable space; grow if needed.
  while (write_buf_.writable_bytes() < len) {
    write_buf_.compact();
    if (write_buf_.writable_bytes() < len) {
      write_buf_.reserve(write_buf_.capacity() * 2);
    }
  }
  std::memcpy(write_buf_.write_ptr(), data, len);
  write_buf_.commit_write(len);
  state_ = State::Writing;
}

std::size_t Connection::flush_write() {
  // Loop write() until EAGAIN or buffer drained — same drain logic as reads.
  while (!write_buf_.empty()) {
    const auto readable = write_buf_.readable();

    const ssize_t n = ::send(
        socket_.fd(),
        readable.data(),
        readable.size(),
        MSG_NOSIGNAL); // Suppress SIGPIPE — we handle errors via return value

    if (n > 0) {
      write_buf_.consume(static_cast<std::size_t>(n));
      LOG_TRACE("Connection [{}]: sent {} bytes", id_, n);
      continue;
    }

    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Socket send buffer full — caller must re-register EPOLLOUT
        break;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == ECONNRESET || errno == EPIPE) {
        LOG_DEBUG("Connection [{}]: peer reset", id_);
        on_closed();
        return 0;
      }
      LOG_WARN("Connection [{}]: send error: {}", id_, std::strerror(errno));
      on_closed();
      return 0;
    }
  }

  if (write_buf_.empty()) {
    state_ = keep_alive_ ? State::Reading : State::Closing;
  }

  return write_buf_.readable_bytes();
}

} // namespace cppws::net
