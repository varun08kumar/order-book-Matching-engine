#pragma once

#include <string>

#include "matching_engine/partitioned_engine.hpp"

namespace me::persistence {

// Serializes the full state of every partition (every resting order, in
// FIFO order, plus each partition's next sequence number) to `path`.
// Sufficient, together with the journal records written after this point,
// to reconstruct identical engine state on recovery.
void WriteSnapshot(const std::string& path, const PartitionedEngine& engine);

// Reconstructs a PartitionedEngine from a snapshot file previously written
// by WriteSnapshot(). The returned engine has the same partition count,
// registered symbols, resting orders (with time priority preserved), and
// per-partition sequence-number counters as the moment the snapshot was
// taken.
PartitionedEngine LoadSnapshot(const std::string& path);

}  // namespace me::persistence
