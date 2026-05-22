#include "core/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using cppws::core::ThreadPool;

TEST(ThreadPoolTest, ExecutesSubmittedTasks) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  for (int i = 0; i < 10; ++i) {
    pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
  }

  pool.shutdown();
  EXPECT_EQ(counter.load(), 10);
}

TEST(ThreadPoolTest, ShutdownDrainsInFlightTasks) {
  ThreadPool pool(4);
  std::atomic<int> started{0};
  std::atomic<int> finished{0};
  constexpr int N = 50;

  for (int i = 0; i < N; ++i) {
    pool.submit([&started, &finished] {
      started.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      finished.fetch_add(1, std::memory_order_relaxed);
    });
  }

  pool.shutdown(); // Must wait for all tasks to complete
  EXPECT_EQ(finished.load(), N);
}

TEST(ThreadPoolTest, HandlesExceptionsWithoutCrash) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  pool.submit([] { throw std::runtime_error("task error"); });
  pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

  pool.shutdown();
  // Counter should still reach 1 — the exception didn't kill the worker
  EXPECT_EQ(counter.load(), 1);
}

TEST(ThreadPoolTest, DefaultThreadCountIsHardwareConcurrency) {
  ThreadPool pool(0);
  const auto expected = std::max(1u, std::thread::hardware_concurrency());
  EXPECT_EQ(pool.thread_count(), expected);
  pool.shutdown();
}

TEST(ThreadPoolTest, ConcurrentTasksRunInParallel) {
  constexpr int N = 4;
  ThreadPool pool(static_cast<std::size_t>(N));
  std::atomic<int> concurrent_peak{0};
  std::atomic<int> currently_running{0};

  std::vector<std::promise<void>> barriers(N);
  std::vector<std::future<void>> futures;
  futures.reserve(N);
  for (auto& p : barriers) futures.push_back(p.get_future());

  for (int i = 0; i < N; ++i) {
    pool.submit([&currently_running, &concurrent_peak, i, &barriers] {
      const int running = currently_running.fetch_add(1, std::memory_order_relaxed) + 1;
      int peak = concurrent_peak.load(std::memory_order_relaxed);
      while (running > peak &&
             !concurrent_peak.compare_exchange_weak(peak, running,
                                                    std::memory_order_relaxed)) {}
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      barriers[static_cast<std::size_t>(i)].set_value();
      currently_running.fetch_sub(1, std::memory_order_relaxed);
    });
  }

  for (auto& f : futures) f.wait();
  pool.shutdown();

  // With N threads and N tasks that sleep, peak concurrency should be N
  EXPECT_GE(concurrent_peak.load(), 2); // At least some parallelism observed
}
