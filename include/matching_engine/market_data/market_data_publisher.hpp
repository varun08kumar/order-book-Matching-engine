#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "matching_engine/events.hpp"
#include "matching_engine/spsc_queue.hpp"

namespace me::market_data {

// Decouples market-data fan-out (accepted/rejected/cancelled/trade/book
// update events) from the matching hot path. The engine thread's only
// interaction with this class is Publish(), which is a non-blocking,
// non-allocating SpscQueue::TryPush — it never waits on the sink and never
// touches a socket or does any I/O. A dedicated background thread drains the
// queue and invokes the sink (e.g. serialize + broadcast to subscribers).
//
// If the sink can't keep up and the queue fills, Publish() drops the event
// and counts it rather than blocking the engine — matching correctness
// (via the journal) never depends on market data being delivered.
class MarketDataPublisher {
 public:
  using Sink = std::function<void(const Event&)>;

  explicit MarketDataPublisher(std::size_t queue_capacity = 65536) : queue_(queue_capacity) {}
  ~MarketDataPublisher() { Stop(); }

  void Start(Sink sink) {
    sink_ = std::move(sink);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { Run(); });
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
  }

  // Engine-thread-only. Non-blocking.
  bool Publish(const Event& event) {
    bool ok = queue_.TryPush(event);
    if (!ok) dropped_.fetch_add(1, std::memory_order_relaxed);
    return ok;
  }

  [[nodiscard]] std::uint64_t dropped_count() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  void Run() {
    Event event;
    while (running_.load(std::memory_order_acquire)) {
      if (queue_.TryPop(event)) {
        sink_(event);
      } else {
        std::this_thread::yield();
      }
    }
    while (queue_.TryPop(event)) sink_(event);  // drain on shutdown
  }

  SpscQueue<Event> queue_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
  Sink sink_;
};

}  // namespace me::market_data
