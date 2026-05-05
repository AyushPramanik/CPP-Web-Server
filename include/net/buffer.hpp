#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Buffer — contiguous byte buffer with separate read and write cursors
//
// Layout of the internal storage:
//
//   ┌──────────────┬───────────────────────┬─────────────────┐
//   │  consumed    │    readable data       │  writable space │
//   │  (dead zone) │   [read_pos_, size_)   │  [size_, cap)   │
//   └──────────────┴───────────────────────┴─────────────────┘
//   0           read_pos_               write_pos_          capacity
//
// After reading N bytes: advance read_pos_ by N.
// After writing N bytes: advance write_pos_ by N.
// compact(): memmove readable bytes to front, reset both cursors.
//   Call compact() only when write_pos_ is near capacity and readable
//   data is small — avoids memmove in the common (empty) case.
//
// No allocation in steady state: the vector is sized at construction and
// only grows if the caller explicitly calls reserve().
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace cppws::net {

class Buffer {
public:
  static constexpr std::size_t DEFAULT_CAPACITY = 16UL * 1024UL; // 16 KiB

  explicit Buffer(std::size_t capacity = DEFAULT_CAPACITY);

  // ── Write side (filling from socket reads) ──────────────────────────────────

  // Raw pointer + length to write into. Pass to read()/recv().
  [[nodiscard]] char* write_ptr() noexcept;
  [[nodiscard]] std::size_t writable_bytes() const noexcept;

  // Advance write cursor after a successful read().
  // Must be called with n <= writable_bytes().
  void commit_write(std::size_t n) noexcept;

  // ── Read side (consuming for parsing/sending) ───────────────────────────────

  // View of bytes ready to consume.
  [[nodiscard]] std::span<const char> readable() const noexcept;
  [[nodiscard]] std::size_t readable_bytes() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return readable_bytes() == 0; }

  // Advance read cursor after consuming n bytes.
  void consume(std::size_t n) noexcept;

  // Consume all readable bytes at once.
  void consume_all() noexcept;

  // ── Buffer management ───────────────────────────────────────────────────────

  // If writable space is exhausted, shift unconsumed data to the front.
  // After compact(), write_ptr() == data_.data() + readable_bytes().
  // Returns the number of bytes recovered (= amount shifted).
  std::size_t compact() noexcept;

  // Grow capacity. Preserves existing readable data.
  void reserve(std::size_t new_capacity);

  [[nodiscard]] std::size_t capacity() const noexcept { return data_.size(); }

  // Discard all data and reset cursors (connection reuse / pooling).
  void reset() noexcept;

private:
  std::vector<char> data_;
  std::size_t read_pos_{0};
  std::size_t write_pos_{0};
};

} // namespace cppws::net
