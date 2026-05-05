#include "net/buffer.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace cppws::net {

Buffer::Buffer(std::size_t capacity) : data_(capacity) {}

// ── Write side ────────────────────────────────────────────────────────────────

char* Buffer::write_ptr() noexcept {
  return data_.data() + write_pos_;
}

std::size_t Buffer::writable_bytes() const noexcept {
  return data_.size() - write_pos_;
}

void Buffer::commit_write(std::size_t n) noexcept {
  assert(n <= writable_bytes());
  write_pos_ += n;
}

// ── Read side ─────────────────────────────────────────────────────────────────

std::span<const char> Buffer::readable() const noexcept {
  return {data_.data() + read_pos_, readable_bytes()};
}

std::size_t Buffer::readable_bytes() const noexcept {
  return write_pos_ - read_pos_;
}

void Buffer::consume(std::size_t n) noexcept {
  assert(n <= readable_bytes());
  read_pos_ += n;
}

void Buffer::consume_all() noexcept {
  read_pos_ = write_pos_ = 0;
}

// ── Buffer management ─────────────────────────────────────────────────────────

std::size_t Buffer::compact() noexcept {
  if (read_pos_ == 0) {
    return 0; // Nothing to compact
  }
  const std::size_t unread = readable_bytes();
  if (unread > 0) {
    // memmove handles overlapping regions (read_pos_ < write_pos_ always)
    std::memmove(data_.data(), data_.data() + read_pos_, unread);
  }
  const std::size_t recovered = read_pos_;
  read_pos_ = 0;
  write_pos_ = unread;
  return recovered;
}

void Buffer::reserve(std::size_t new_capacity) {
  if (new_capacity <= data_.size()) {
    return;
  }
  // Compact first to avoid losing readable data during resize
  compact();
  data_.resize(new_capacity);
}

void Buffer::reset() noexcept {
  read_pos_ = write_pos_ = 0;
}

} // namespace cppws::net
