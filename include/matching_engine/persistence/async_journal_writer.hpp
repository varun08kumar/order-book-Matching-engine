#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "matching_engine/command.hpp"
#include "matching_engine/persistence/journal.hpp"
#include "matching_engine/spsc_queue.hpp"

namespace me::persistence {

// Moves the blocking, fsync'ing JournalWriter off the matching hot path.
// The engine thread only ever calls TryAppend(), a non-blocking SpscQueue
// push; a dedicated background thread drains the queue and performs the
// actual (durable) disk write.
//
// Trade-off: because the write is asynchronous, Process()-ing a command and
// it being durably journaled are no longer the same instant — a crash
// between TryAppend() and the background thread's write can lose the last
// few queued commands. This implementation favors matching-loop latency;
// a deployment that needs zero-loss durability would instead journal
// synchronously (JournalWriter directly) and accept the fsync latency in
// the critical path, or use group commit to amortize it.
class AsyncJournalWriter {
 public:
  AsyncJournalWriter(const std::string& path, std::size_t queue_capacity = 65536,
                      bool fsync_every_write = true)
      : queue_(queue_capacity), writer_(path, fsync_every_write) {
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { Run(); });
  }

  ~AsyncJournalWriter() { Stop(); }

  void Stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
  }

  // Engine-thread-only. Non-blocking; never touches disk.
  bool TryAppend(const Command& cmd) {
    bool ok = queue_.TryPush(cmd);
    if (!ok) dropped_.fetch_add(1, std::memory_order_relaxed);
    return ok;
  }

  [[nodiscard]] std::uint64_t dropped_count() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  void Run() {
    Command cmd;
    while (running_.load(std::memory_order_acquire)) {
      if (queue_.TryPop(cmd)) {
        writer_.Append(cmd);
      } else {
        std::this_thread::yield();
      }
    }
    while (queue_.TryPop(cmd)) writer_.Append(cmd);  // drain on shutdown
  }

  SpscQueue<Command> queue_;
  JournalWriter writer_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace me::persistence
