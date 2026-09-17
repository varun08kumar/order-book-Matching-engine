#pragma once

#include <string>
#include <vector>

#include "matching_engine/command.hpp"

namespace me::persistence {

// Append-only write-ahead log of every Command the engine has been asked to
// process, recorded *before* Process() runs it. Because matching is
// deterministic, replaying the same commands in the same order through a
// freshly loaded snapshot reproduces identical engine state — the journal
// therefore only needs to store commands, never derived events.
//
// Each record on disk is a length-prefixed frame (matching_engine/net's wire
// format) wrapping an encoded Command, so the on-disk format is exactly the
// same bytes that would cross the network.
class JournalWriter {
 public:
  // Opens (creating if necessary) `path` for appending. `fsync_every_write`
  // trades latency for durability: true guarantees every Append() is
  // fsync'd to disk before returning (safe across process crashes and OS
  // crashes), false only guarantees the OS page cache has it (safe across
  // process crashes, not power loss).
  explicit JournalWriter(const std::string& path, bool fsync_every_write = true);
  ~JournalWriter();

  JournalWriter(const JournalWriter&) = delete;
  JournalWriter& operator=(const JournalWriter&) = delete;

  void Append(const Command& cmd);
  void Flush();

 private:
  int fd_ = -1;
  bool fsync_every_write_;
};

// Reads every command previously written by a JournalWriter to `path`, in
// order. Throws std::runtime_error if the file contains a truncated or
// malformed record (a torn write from a crash mid-append is the expected
// cause — see docs/recovery.md for how the last record is handled).
std::vector<Command> ReadJournal(const std::string& path);

}  // namespace me::persistence
