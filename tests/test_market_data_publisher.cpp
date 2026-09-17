#include "matching_engine/market_data/market_data_publisher.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <vector>

using namespace me;
using namespace me::market_data;

TEST(MarketDataPublisher, DeliversAllEventsInOrder) {
  MarketDataPublisher pub(1024);
  std::mutex mutex;
  std::vector<SequenceNumber> received;

  pub.Start([&](const Event& e) {
    std::lock_guard<std::mutex> lock(mutex);
    received.push_back(std::visit([](const auto& x) { return x.sequence_number; }, e));
  });

  constexpr int kCount = 2000;
  for (int i = 0; i < kCount; ++i) {
    AcceptedEvent e;
    e.sequence_number = static_cast<SequenceNumber>(i);
    while (!pub.Publish(Event{e})) std::this_thread::yield();
  }
  pub.Stop();  // drains remaining queued events before returning

  ASSERT_EQ(received.size(), static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    EXPECT_EQ(received[static_cast<std::size_t>(i)], static_cast<SequenceNumber>(i));
  }
}

TEST(MarketDataPublisher, DropsAndCountsWhenSlowConsumerFallsBehind) {
  MarketDataPublisher pub(4);  // tiny queue, power of two
  std::mutex mutex;
  std::condition_variable cv;
  bool release = false;

  pub.Start([&](const Event&) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return release; });  // stall the consumer
  });

  // First event gets picked up and stalls the worker; the rest pile up
  // until the bounded queue is full and Publish starts reporting failure.
  bool saw_drop = false;
  for (int i = 0; i < 10000 && !saw_drop; ++i) {
    if (!pub.Publish(AcceptedEvent{})) saw_drop = true;
  }
  EXPECT_TRUE(saw_drop);
  EXPECT_GT(pub.dropped_count(), 0u);

  {
    std::lock_guard<std::mutex> lock(mutex);
    release = true;
  }
  cv.notify_all();
  pub.Stop();
}
