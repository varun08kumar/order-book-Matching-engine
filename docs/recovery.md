# Snapshot & Recovery

## Model

The matching engine is a deterministic state machine: given the same
starting state and the same ordered sequence of `Command`s, `Process()`
always produces the same resulting book state and the same sequence
numbers. Recovery exploits this directly instead of replaying derived
events:

- **Journal** (`persistence::JournalWriter` / `ReadJournal`) is a
  write-ahead log of `Command`s, written *before* they are applied,
  encoded with the exact same framing and codec as the network protocol
  (`docs/protocol.md`) — a journal file is a sequence of the same frames
  that would cross the wire.
- **Snapshot** (`persistence::WriteSnapshot` / `LoadSnapshot`) captures every
  resting order for every symbol on every partition, in FIFO order per price
  level, plus each partition's next-sequence-number counter.

## Startup sequence

1. `LoadSnapshot(path)` reconstructs a `PartitionedEngine`: symbols are
   re-registered on their original partitions, every resting order is
   re-inserted at the back of its price level's FIFO queue via
   `OrderBook::RestoreOrder` (no matching is run — these orders are already
   known not to cross each other), and each partition's sequence counter is
   restored.
2. `ReadJournal(path)` loads every command written *after* that snapshot was
   taken (see "log rotation" below) and each is replayed through
   `PartitionedEngine::Dispatch` in its original order.
3. Because sequence numbers are assigned purely as a function of call order
   (`MatchingEngine::Process` increments a per-partition counter), and that
   counter was restored to its exact snapshot-time value, replay reproduces
   identical sequence numbers, trades, and book state to the original run —
   verified in `tests/test_journal_and_snapshot.cpp`
   (`Recovery.SnapshotPlusJournalReplayReconstructsIdenticalState`).

## Log rotation

A snapshot only makes sense together with "the journal records written
after it". This implementation rotates: stop writing to the current journal
file, take the snapshot, then start a fresh journal file for subsequent
commands. Recovery loads the latest snapshot and replays only the journal
file(s) created after it. (An alternative — recording a byte/record offset
inside the snapshot and replaying a single ever-growing journal from that
offset — works too; rotation was chosen here because it keeps both formats
simpler and bounds journal file size.)

## Crash safety

`JournalWriter::Append` writes the length-prefixed frame with `::write()`
and, by default, `::fsync()`s before returning — a crash right after
`Append()` returns is guaranteed to have the record on disk. A crash
*during* the write can leave a torn trailing record (a length prefix with
fewer body bytes than promised, or fewer than 4 prefix bytes at all).
`ReadJournal` treats an incomplete *trailing* record as expected
crash-truncation and silently discards it (that command never got a
chance to be acknowledged, so losing it is safe); it treats a corrupt
record *followed by more data* (a declared length that overflows
`FrameDecoder::kMaxFrameBytes`, or a frame that decodes to no valid
command) as real corruption and throws, since that cannot be produced by a
torn tail write.

## Keeping the hot path off disk and off the queue-drain path

Calling `JournalWriter::Append` (which fsyncs) or a market-data sink
directly from the matching loop would violate the hot-path rule (no disk
I/O, no blocking). `AsyncJournalWriter` and `MarketDataPublisher`
(`include/matching_engine/market_data/`) both wrap their blocking work
behind a bounded SPSC queue and a dedicated background thread: the engine
thread only ever calls a non-blocking `TryPush`, which either enqueues the
command/event for the background thread to durably write / publish, or
(if the consumer has fallen behind and the queue is full) drops it and
increments a counter — the engine thread itself never blocks either way.
