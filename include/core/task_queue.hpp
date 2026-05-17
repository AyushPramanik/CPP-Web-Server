#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// TaskQueue<T> — bounded, thread-safe MPMC queue
//
// Design: mutex + condition variable over a std::deque.
//
// Why not lock-free?
//   Lock-free MPMC queues (e.g., Dmitry Vyukov's) are faster under sustained
//   high contention, but they are complex, require careful memory ordering
//   analysis, and are hard to get right.  For our workload — one producer
//   (event loop) and N consumers (workers) with tasks that take microseconds
//   to process — the mutex is uncontended almost all the time.
//
//   Rule: profile before optimising. If lock contention shows up in perf
//   output, THEN replace this with a lock-free implementation.
//
// Bounded capacity:
//   If push() is called when the queue is full, the caller blocks until a
//   consumer drains it.  This implements backpressure: the event loop slows
//   down accepting new work when workers can't keep up.  Without backpressure
//   the queue grows without bound and OOM kills the process.
//
// Shutdown protocol:
//   close() unblocks all waiting consumers so threads can exit cleanly.
//   Producers must stop calling push() before calling close().
// ─────────────────────────────────────────────────────────────────────────────

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace cppws::core {

template <typename T>
class TaskQueue {
public:
  static constexpr std::size_t DEFAULT_CAPACITY = 4096;

  explicit TaskQueue(std::size_t capacity = DEFAULT_CAPACITY)
      : capacity_(capacity) {}

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;
  TaskQueue(TaskQueue&&) = delete;
  TaskQueue& operator=(TaskQueue&&) = delete;

  // ── Producer API ────────────────────────────────────────────────────────────

  // Push an item. Blocks if the queue is at capacity (backpressure).
  // Returns false if the queue was closed — caller should stop producing.
  bool push(T item) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
    if (closed_) return false;
    queue_.push_back(std::move(item));
    not_empty_.notify_one();
    return true;
  }

  // Non-blocking push. Returns false if full or closed.
  bool try_push(T item) {
    std::unique_lock lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(item));
    not_empty_.notify_one();
    return true;
  }

  // ── Consumer API ────────────────────────────────────────────────────────────

  // Pop an item. Blocks until an item is available or the queue is closed.
  // Returns nullopt when closed and empty.
  std::optional<T> pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return std::nullopt;
    T item = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return item;
  }

  // Non-blocking pop. Returns nullopt if empty.
  std::optional<T> try_pop() {
    std::unique_lock lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    T item = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return item;
  }

  // ── Lifecycle ───────────────────────────────────────────────────────────────

  // Signal shutdown. All blocked pop() calls return nullopt.
  // Unblocks producers waiting on not_full_ too (they get false).
  void close() {
    std::unique_lock lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] bool is_closed() const {
    std::unique_lock lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t size() const {
    std::unique_lock lock(mutex_);
    return queue_.size();
  }

  [[nodiscard]] bool empty() const {
    std::unique_lock lock(mutex_);
    return queue_.empty();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<T> queue_;
  std::size_t capacity_;
  bool closed_{false};
};

} // namespace cppws::core
