#include "core/thread_pool.hpp"

#include "util/logger.hpp"

namespace cppws::core {

ThreadPool::ThreadPool(std::size_t n_threads) {
  if (n_threads == 0) {
    n_threads = std::thread::hardware_concurrency();
    if (n_threads == 0) {
      n_threads = 4; // Sensible fallback when hardware_concurrency() returns 0
    }
  }

  workers_.reserve(n_threads);
  for (std::size_t i = 0; i < n_threads; ++i) {
    workers_.emplace_back([this] { worker_loop(); });
  }

  LOG_INFO("ThreadPool started with {} worker threads", workers_.size());
}

ThreadPool::~ThreadPool() {
  shutdown();
}

void ThreadPool::submit(Task task) {
  queue_.push(std::move(task));
}

bool ThreadPool::try_submit(Task task) {
  return queue_.try_push(std::move(task));
}

void ThreadPool::shutdown() {
  if (shutdown_called_) return;
  shutdown_called_ = true;

  LOG_INFO("ThreadPool shutting down, draining {} pending tasks", queue_.size());

  // Close the queue — all blocked pop() calls will return nullopt.
  // Workers will drain remaining tasks then exit their loops.
  queue_.close();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  LOG_INFO("ThreadPool stopped, all workers joined");
}

void ThreadPool::worker_loop() {
  LOG_DEBUG("Worker thread started: id={}", std::hash<std::thread::id>{}(std::this_thread::get_id()));

  // Drain the queue until it's closed and empty.
  while (auto task = queue_.pop()) {
    try {
      (*task)(); // Execute the task
    } catch (const std::exception& ex) {
      // Tasks must not throw — log and continue so the worker doesn't die.
      // A crashed worker thread reduces throughput but doesn't crash the server.
      LOG_ERROR("Uncaught exception in worker task: {}", ex.what());
    } catch (...) {
      LOG_ERROR("Unknown exception in worker task");
    }
  }

  LOG_DEBUG("Worker thread exiting");
}

} // namespace cppws::core
