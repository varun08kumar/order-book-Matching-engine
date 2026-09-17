#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace me {

// Bounded single-producer/single-consumer lock-free ring buffer.
//
// Correctness relies on exactly one thread ever calling TryPush() and
// exactly one (possibly different) thread ever calling TryPop(). Given that,
// no locks or CAS loops are needed — a single producer-owned index and a
// single consumer-owned index, each written by only one thread, are enough.
//
// Memory ordering rationale
// --------------------------
// `head_` is the producer's write cursor; only the producer thread ever
// stores to it. `tail_` is the consumer's read cursor; only the consumer
// thread ever stores to it. Each side takes cross-thread dependencies on the
// *other* index, which is where the acquire/release pairing lives:
//
//   - TryPush() reads its own `head_` with relaxed order: no other thread
//     writes `head_`, so there is nothing to synchronize with.
//   - TryPush() reloads `tail_` with acquire (only when its cached copy
//     suggests the queue might be full) to observe the consumer's most
//     recent slot-freeing store, so it never overwrites a slot the consumer
//     is still reading.
//   - TryPush() publishes the new `head_` with release: this makes the
//     element write (`buffer_[head & mask] = ...`) above it happen-before
//     any consumer thread that later acquire-loads this same `head_` value,
//     which is exactly what TryPop() does before reading that slot.
//   - TryPop() mirrors this: relaxed load of its own `tail_`, acquire load
//     of `head_` to synchronize-with the producer's release store (making
//     the element write visible before the read), and a release store to
//     `tail_` so the producer's later acquire-load of `tail_` is guaranteed
//     to observe that this slot's read has completed before reusing it.
//
// Each cursor plus its owner-local cache of the other cursor is placed on
// its own cache line (`alignas(64)`) so the producer and consumer never
// invalidate each other's cache line on every operation (false sharing).
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity) : mask_(capacity - 1), buffer_(capacity) {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
      throw std::invalid_argument("SpscQueue capacity must be a power of two");
    }
  }

  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  // Producer-only. Returns false if the queue is full.
  bool TryPush(T value) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = head + 1;
    if (next - cached_tail_ > buffer_.size()) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (next - cached_tail_ > buffer_.size()) return false;  // full
    }
    buffer_[head & mask_] = std::move(value);
    head_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer-only. Returns false if the queue is empty.
  bool TryPop(T& out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail == cached_head_) return false;  // empty
    }
    out = std::move(buffer_[tail & mask_]);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Approximate size, safe to call from either thread for monitoring; may be
  // stale by the time it's read since the other side can be concurrently
  // mutating its cursor.
  [[nodiscard]] std::size_t SizeApprox() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t capacity() const { return buffer_.size(); }

 private:
  const std::size_t mask_;
  std::vector<T> buffer_;

  alignas(64) std::atomic<std::size_t> head_{0};
  std::size_t cached_tail_ = 0;  // producer-local; avoids an atomic load on every push

  alignas(64) std::atomic<std::size_t> tail_{0};
  std::size_t cached_head_ = 0;  // consumer-local; avoids an atomic load on every pop
};

}  // namespace me
