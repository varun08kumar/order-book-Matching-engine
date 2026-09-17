#include "matching_engine/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace me;

TEST(SpscQueue, RejectsNonPowerOfTwoCapacity) {
  EXPECT_THROW(SpscQueue<int>(0), std::invalid_argument);
  EXPECT_THROW(SpscQueue<int>(3), std::invalid_argument);
  EXPECT_NO_THROW(SpscQueue<int>(4));
}

TEST(SpscQueue, PopFromEmptyFails) {
  SpscQueue<int> q(4);
  int out = 0;
  EXPECT_FALSE(q.TryPop(out));
}

TEST(SpscQueue, PushPopPreservesFifoOrder) {
  SpscQueue<int> q(8);
  for (int i = 0; i < 5; ++i) EXPECT_TRUE(q.TryPush(i));
  for (int i = 0; i < 5; ++i) {
    int out = -1;
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out, i);
  }
}

TEST(SpscQueue, PushFailsWhenFull) {
  SpscQueue<int> q(4);
  for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.TryPush(i));
  EXPECT_FALSE(q.TryPush(99));
  int out = 0;
  ASSERT_TRUE(q.TryPop(out));
  EXPECT_EQ(out, 0);
  EXPECT_TRUE(q.TryPush(99));  // freed one slot
}

TEST(SpscQueue, WrapsAroundCorrectly) {
  SpscQueue<int> q(4);
  for (int round = 0; round < 100; ++round) {
    ASSERT_TRUE(q.TryPush(round));
    int out = -1;
    ASSERT_TRUE(q.TryPop(out));
    EXPECT_EQ(out, round);
  }
}

TEST(SpscQueue, ConcurrentProducerConsumerDeliversAllInOrder) {
  constexpr int kCount = 200000;
  SpscQueue<int> q(1024);
  std::atomic<bool> done{false};

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!q.TryPush(i)) {
        std::this_thread::yield();
      }
    }
    done.store(true, std::memory_order_release);
  });

  std::vector<int> consumed;
  consumed.reserve(kCount);
  std::thread consumer([&] {
    int value;
    while (consumed.size() < static_cast<std::size_t>(kCount)) {
      if (q.TryPop(value)) {
        consumed.push_back(value);
      }
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(consumed.size(), static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    ASSERT_EQ(consumed[static_cast<std::size_t>(i)], i) << "order violated at index " << i;
  }
}
