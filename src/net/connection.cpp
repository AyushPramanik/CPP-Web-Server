#include "net/connection.hpp"

#include "util/logger.hpp"

#include <cerrno>
#include <cstring>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppws::net {

// ── SendfileOp ────────────────────────────────────────────────────────────────

SendfileOp::~SendfileOp() {
  if (file_fd_ >= 0) {
    ::close(file_fd_);
    file_fd_ = -1;
  }
}

SendfileOp::SendfileOp(SendfileOp&& other) noexcept
    : file_fd_(other.file_fd_), offset_(other.offset_), remaining_(other.remaining_) {
  other.file_fd_ = -1;
}

SendfileOp& SendfileOp::operator=(SendfileOp&& other) noexcept {
  if (this != &other) {
    if (file_fd_ >= 0) ::close(file_fd_);
    file_fd_   = other.file_fd_;
    offset_    = other.offset_;
    remaining_ = other.remaining_;
    other.file_fd_ = -1;
  }
  return *this;
}

// ── Connection ────────────────────────────────────────────────────────────────

Connection::Connection(Socket socket, uint64_t conn_id)
    : socket_(std::move(socket)), id_(conn_id) {
  LOG_DEBUG("Connection [{}] opened fd={}", id_, socket_.fd());
}

Connection::~Connection() {
  LOG_DEBUG("Connection [{}] destroyed", id_);
}

// ── Event handlers ────────────────────────────────────────────────────────────

bool Connection::on_readable() {
  touch(); // Update last-active timestamp on every I/O event

  while (true) {
    if (read_buf_.writable_bytes() == 0) {
      const std::size_t recovered = read_buf_.compact();
      if (recovered == 0) {
        LOG_WARN("Connection [{}]: read buffer full, closing", id_);
        on_closed();
        return false;
      }
    }

    const ssize_t n = ::recv(
        socket_.fd(), read_buf_.write_ptr(), read_buf_.writable_bytes(), 0);

    if (n > 0) {
      read_buf_.commit_write(static_cast<std::size_t>(n));
      if (on_read_) on_read_(*this);
      continue;
    }

    if (n == 0) {
      on_closed();
      return false;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
    if (errno == EINTR) continue;

    LOG_WARN("Connection [{}]: recv error: {}", id_, std::strerror(errno));
    on_closed();
    return false;
  }
}

bool Connection::on_writable() {
  touch();
  const std::size_t remaining = flush_write();
  if (state_ == State::Closed) return false;
  if (remaining == 0 && !keep_alive_) {
    on_closed();
    return false;
  }
  return true;
}

void Connection::on_closed() {
  if (state_ == State::Closed) return;
  state_ = State::Closed;
  sendfile_op_.reset(); // Close the file fd if a sendfile was in progress
  LOG_DEBUG("Connection [{}] closing", id_);
  if (on_close_) on_close_(*this);
  socket_.close();
}

// ── Write path ────────────────────────────────────────────────────────────────

void Connection::enqueue_write(const char* data, std::size_t len) {
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

void Connection::begin_sendfile(int file_fd, off_t size) {
  sendfile_op_.emplace(file_fd, size);
  state_ = State::Writing;
}

std::size_t Connection::flush_write() {
  bool would_block = false;

  // ── Phase 1: drain write buffer (headers) ───────────────────────────────────
  while (!write_buf_.empty() && !would_block) {
    const auto readable = write_buf_.readable();
    const ssize_t n = ::send(
        socket_.fd(), readable.data(), readable.size(), MSG_NOSIGNAL);

    if (n > 0) {
      write_buf_.consume(static_cast<std::size_t>(n));
    } else if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        would_block = true;
      } else if (errno == EINTR) {
        // retry
      } else if (errno == ECONNRESET || errno == EPIPE) {
        on_closed();
        return 0;
      } else {
        LOG_WARN("Connection [{}]: send error: {}", id_, std::strerror(errno));
        on_closed();
        return 0;
      }
    }
  }

  // ── Phase 2: sendfile body (zero-copy) ──────────────────────────────────────
  // Only called once write buffer is drained — ensures headers precede body.
  //
  // sendfile(out_fd, in_fd, offset, count):
  //   Kernel copies from page cache directly to the socket buffer.
  //   No userspace memcpy. 'offset' is updated in-place by the kernel.
  while (!would_block && sendfile_op_ && sendfile_op_->active()) {
    const ssize_t n = ::sendfile(
        socket_.fd(),
        sendfile_op_->file_fd_,
        &sendfile_op_->offset_,
        static_cast<std::size_t>(sendfile_op_->remaining_));

    if (n > 0) {
      sendfile_op_->remaining_ -= static_cast<off_t>(n);
      LOG_TRACE("Connection [{}]: sendfile {} bytes, {} remaining",
                id_, n, sendfile_op_->remaining_);
    } else if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        would_block = true;
      } else if (errno == EINTR) {
        // retry
      } else {
        LOG_WARN("Connection [{}]: sendfile error: {}", id_, std::strerror(errno));
        on_closed();
        return 0;
      }
    } else {
      break; // EOF on file — unexpected but safe
    }
  }

  // ── Decide next state ───────────────────────────────────────────────────────
  const bool buf_empty = write_buf_.empty();
  const bool sf_done   = !sendfile_op_ || !sendfile_op_->active();
  if (buf_empty && sf_done) {
    sendfile_op_.reset();
    state_ = keep_alive_ ? State::Reading : State::Closing;
  }

  return write_buf_.readable_bytes() +
         static_cast<std::size_t>(sendfile_op_ ? sendfile_op_->remaining_ : 0);
}

} // namespace cppws::net
