#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// ThreadPool — fixed-size pool of worker threads draining a TaskQueue
//
// Thread model:
//   • N worker threads, each running a drain loop on a shared TaskQueue.
//   • Tasks are std::function<void()> — any callable.
//   • Graceful shutdown: close the queue, workers drain remaining tasks, exit.
//
// Why std::jthread (C++20)?
//   jthread joins in its destructor, so there is no way to forget to join.
//   If the destructor runs during exception unwinding, threads are still
//   joined cleanly — no std::terminate().
//
// Thread count choice:
//   hardware_concurrency() = number of logical CPUs.  This is the right
//   default for CPU-bound work.  For I/O-bound work (waiting on DB, upstream
//   HTTP) you would use more threads (2x–10x) to keep CPUs busy during waits.
//   Our handlers are mostly I/O-bound (file reads), so Phase 6 will make
//   thread count configurable.
//
// Comparison to other designs:
//   • Folly's IOThreadPoolExecutor: per-thread queues with work-stealing.
//     Reduces contention but more complex. Good for millions of tasks/sec.
//   • Boost.Asio thread pool: similar design, uses io_context.
//   • Our design: simple, correct, and measurably fast enough for Phase 4.
// ─────────────────────────────────────────────────────────────────────────────

#include "core/task_queue.hpp"

#include <functional>
#include <thread>
#include <vector>

namespace cppws::core {

using Task = std::function<void()>;

class ThreadPool {
public:
  // Construct and start n_threads worker threads.
  // n_threads = 0 → use hardware_concurrency().
  explicit ThreadPool(std::size_t n_threads = 0);

  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  // ── Task submission ─────────────────────────────────────────────────────────

  // Submit a task. Blocks if the queue is at capacity (backpressure).
  void submit(Task task);

  // Non-blocking submit. Returns false if queue is full or shut down.
  [[nodiscard]] bool try_submit(Task task);

  // ── Lifecycle ───────────────────────────────────────────────────────────────

  // Signal all workers to stop after draining in-flight tasks.
  // Called automatically by destructor — safe to call early.
  void shutdown();

  [[nodiscard]] std::size_t thread_count() const noexcept { return workers_.size(); }
  [[nodiscard]] std::size_t pending_tasks() const { return queue_.size(); }

private:
  void worker_loop();

  TaskQueue<Task> queue_;
  std::vector<std::thread> workers_;
  bool shutdown_called_{false};
};

} // namespace cppws::core
