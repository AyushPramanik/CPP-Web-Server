#include "core/task_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using cppws::core::TaskQueue;

TEST(TaskQueueTest, PushAndPopSingleThread) {
  TaskQueue<int> queue(16);
  EXPECT_TRUE(queue.push(42));
  auto val = queue.try_pop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 42);
}

TEST(TaskQueueTest, TryPopEmptyReturnsNullopt) {
  TaskQueue<int> queue(16);
  EXPECT_FALSE(queue.try_pop().has_value());
}

TEST(TaskQueueTest, TryPushFullReturnsFalse) {
  TaskQueue<int> queue(2);
  EXPECT_TRUE(queue.try_push(1));
  EXPECT_TRUE(queue.try_push(2));
  EXPECT_FALSE(queue.try_push(3)); // Full
}

TEST(TaskQueueTest, CloseUnblocksPop) {
  TaskQueue<int> queue(16);
  std::thread closer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.close();
  });
  // pop() blocks until close() is called
  auto val = queue.pop();
  EXPECT_FALSE(val.has_value()); // nullopt when closed + empty
  closer.join();
}

TEST(TaskQueueTest, FifoOrdering) {
  TaskQueue<int> queue(16);
  for (int i = 0; i < 5; ++i) queue.push(i);
  for (int i = 0; i < 5; ++i) {
    auto val = queue.try_pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, i); // FIFO
  }
}

TEST(TaskQueueTest, MultiProducerSingleConsumer) {
  TaskQueue<int> queue(1024);
  constexpr int N_PRODUCERS = 4;
  constexpr int PER_PRODUCER = 100;
  std::atomic<int> total_consumed{0};

  // Producers
  std::vector<std::thread> producers;
  producers.reserve(N_PRODUCERS);
  for (int i = 0; i < N_PRODUCERS; ++i) {
    producers.emplace_back([&queue, i] {
      for (int j = 0; j < PER_PRODUCER; ++j) {
        queue.push(i * 1000 + j);
      }
    });
  }

  // Consumer in a separate thread
  std::thread consumer([&] {
    int count = 0;
    while (count < N_PRODUCERS * PER_PRODUCER) {
      if (auto val = queue.try_pop()) {
        ++count;
        total_consumed.fetch_add(1, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
  });

  for (auto& t : producers) t.join();
  consumer.join();

  EXPECT_EQ(total_consumed.load(), N_PRODUCERS * PER_PRODUCER);
}

TEST(TaskQueueTest, SizeReflectsContents) {
  TaskQueue<int> queue(16);
  EXPECT_EQ(queue.size(), 0u);
  queue.push(1);
  queue.push(2);
  EXPECT_EQ(queue.size(), 2u);
  queue.try_pop();
  EXPECT_EQ(queue.size(), 1u);
}
