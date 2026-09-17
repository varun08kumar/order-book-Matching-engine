#pragma once

#include <cstddef>
#include <unordered_map>

#include "matching_engine/types.hpp"

namespace me {

// Deterministically maps a symbol to a partition index in [0, partition_count).
// Every symbol must always route to the same partition for the lifetime of
// the system (partition ownership of a symbol's book never migrates), which
// is what lets each partition run lock-free on a single owning thread.
class SymbolRouter {
 public:
  explicit SymbolRouter(std::size_t partition_count) : partition_count_(partition_count) {}

  // Pins a symbol to an explicit partition, overriding the default hash.
  // Must be called before the symbol is ever routed.
  void Pin(SymbolId symbol, std::size_t partition) { overrides_[symbol] = partition; }

  [[nodiscard]] std::size_t PartitionFor(SymbolId symbol) const {
    auto it = overrides_.find(symbol);
    if (it != overrides_.end()) return it->second;
    return static_cast<std::size_t>(symbol) % partition_count_;
  }

  [[nodiscard]] std::size_t partition_count() const { return partition_count_; }

 private:
  std::size_t partition_count_;
  std::unordered_map<SymbolId, std::size_t> overrides_;
};

}  // namespace me
