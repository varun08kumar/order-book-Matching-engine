#pragma once

#include <memory>
#include <vector>

#include "matching_engine/command.hpp"
#include "matching_engine/events.hpp"
#include "matching_engine/matching_engine.hpp"
#include "matching_engine/symbol_router.hpp"

namespace me {

// Owns N single-threaded MatchingEngine partitions and routes commands to
// the correct one by symbol. In production each partition's Process() calls
// would run on its own dedicated thread, fed by a per-partition SPSC queue
// (Phase 6); this class provides the routing/ownership structure that a
// multi-threaded driver sits on top of, and is also directly usable
// single-threaded for tests and benchmarks.
class PartitionedEngine {
 public:
  explicit PartitionedEngine(std::size_t partition_count) : router_(partition_count) {
    partitions_.reserve(partition_count);
    for (std::size_t i = 0; i < partition_count; ++i) {
      partitions_.push_back(std::make_unique<MatchingEngine>(i));
    }
  }

  void RegisterSymbol(SymbolId symbol) {
    std::size_t p = router_.PartitionFor(symbol);
    partitions_[p]->RegisterSymbol(symbol);
  }

  void RegisterSymbolOnPartition(SymbolId symbol, std::size_t partition) {
    router_.Pin(symbol, partition);
    partitions_[partition]->RegisterSymbol(symbol);
  }

  SequenceNumber Dispatch(const Command& cmd, std::vector<Event>& out_events) {
    std::size_t p = router_.PartitionFor(SymbolOf(cmd));
    return partitions_[p]->Process(cmd, out_events);
  }

  [[nodiscard]] MatchingEngine& Partition(std::size_t index) { return *partitions_[index]; }
  [[nodiscard]] const MatchingEngine& Partition(std::size_t index) const { return *partitions_[index]; }
  [[nodiscard]] std::size_t PartitionCount() const { return partitions_.size(); }
  [[nodiscard]] const SymbolRouter& router() const { return router_; }

 private:
  SymbolRouter router_;
  std::vector<std::unique_ptr<MatchingEngine>> partitions_;
};

}  // namespace me
