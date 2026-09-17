# Architecture

## Layers and where they live

| # | Layer | Implementation |
|---|-------|----------------|
| 1 | Binary TCP gateway | `net::TcpGateway`, `net::TcpListener`/`TcpSocket` (`include/matching_engine/net/tcp_gateway.hpp`, `tcp_socket.hpp`) |
| 2 | TCP message framing | `net::FrameDecoder` (`net/frame_decoder.hpp`) — see `docs/protocol.md` |
| 3 | Binary order decoder | `net::DecodeCommand` / `EncodeCommand` / `EncodeEvent` / `DecodeEvent` (`net/protocol.hpp`) |
| 4 | Symbol router | `SymbolRouter` (`symbol_router.hpp`) |
| 5 | Per-partition matching engine | `MatchingEngine`, `PartitionedEngine` (`matching_engine.hpp`, `partitioned_engine.hpp`) |
| 6 | In-memory order book | `naive::OrderBook` (correctness-first) and `fast::OrderBook` (optimized) |
| 7 | Price-time-priority matching | `OrderBook::Match` in both implementations |
| 8 | Order-ID index for cancellation | `unordered_map<OrderId, Locator/Node*>` inside each `OrderBook` |
| 9 | Preallocated order memory pool | `ObjectPool<T>` (`object_pool.hpp`), used by `fast::OrderBook` |
| 10 | Lock-free queues | `SpscQueue<T>` (`spsc_queue.hpp`) |
| 11 | Trade/event publisher | `market_data::MarketDataPublisher` (`market_data/market_data_publisher.hpp`) |
| 12 | Append-only journal | `persistence::JournalWriter`/`AsyncJournalWriter`/`ReadJournal` |
| 13 | Snapshot and recovery | `persistence::WriteSnapshot`/`LoadSnapshot` — see `docs/recovery.md` |
| 14 | Market-data publisher | (same as #11 — the publisher fans out every accept/reject/cancel/trade/book-update event) |
| 15 | Benchmark suite | `benchmarks/bench_*.cpp` — see `docs/performance.md` |

## Data flow

```
                     ┌─────────────────────────────────────────────┐
TCP client  ───────► │ GatewaySession (reader thread)                │
                     │   recv() -> FrameDecoder -> DecodeCommand    │
                     └───────────────┬───────────────────────────────┘
                                     │ SpscQueue<Command>  (bounded, lock-free)
                                     ▼
                     ┌─────────────────────────────────────────────┐
                     │ engine-driving thread (single, per partition) │
                     │   TryPopInbound -> AsyncJournalWriter.TryAppend│  (non-blocking)
                     │              -> PartitionedEngine::Dispatch   │
                     │              -> MarketDataPublisher.Publish   │  (non-blocking)
                     └───────────────┬───────────────────────────────┘
                                     │ SpscQueue<OutboundFrame>
                                     ▼
                     ┌─────────────────────────────────────────────┐
TCP client  ◄─────── │ GatewaySession (writer thread)                │
                     │   EncodeEvent -> FrameMessage -> send()      │
                     └─────────────────────────────────────────────┘
```

Two background threads (`AsyncJournalWriter`'s worker and
`MarketDataPublisher`'s worker) drain their own `SpscQueue`s independently,
so neither disk I/O nor market-data fan-out can add latency to, or block,
the command that produced them.

## Partitioning and threading model

Symbols are assigned to partitions by `SymbolRouter` (a hash, or an
explicit pin) and that assignment never changes. Each partition is a
`MatchingEngine` that owns the `OrderBook`s for its symbols and is meant to
be driven by exactly one thread for its entire lifetime — this is what lets
`OrderBook::AddOrder/CancelOrder/ModifyOrder` and the pool inside
`fast::OrderBook` avoid any locking: there is never more than one writer.
`PartitionedEngine::Dispatch` routes a `Command` to the right partition by
symbol; a real multi-threaded deployment runs one such dispatch loop per
partition, each fed by the inbound `SpscQueue`s of the sessions currently
routed to it.

## Two order book implementations, by design

`naive::OrderBook` (`include/matching_engine/naive/`) is the
correctness-first reference: `std::map` price levels, `std::list` FIFO
queues, `std::unordered_map` index. It is what `MatchingEngine` uses, what
persistence (journal/snapshot) is defined against, and what
`fast::OrderBook` is checked against in
`tests/test_naive_vs_fast_equivalence.cpp` (a 5000-operation randomized
differential test asserting identical results and identical resulting book
state at every step).

`fast::OrderBook` (`include/matching_engine/fast/`) keeps the same
semantics and public surface but draws `Order` storage from a preallocated
`ObjectPool` and threads each price level's FIFO queue as an intrusive
doubly-linked list through the pooled nodes — adding, filling, and
cancelling an order touches no heap allocator. Price *levels* themselves
still come from `std::map`'s default allocator (see `docs/performance.md`
for what that costs and why it was left as-is).

## Hot-path constraints

Inside `OrderBook::AddOrder/CancelOrder/ModifyOrder` and `MatchingEngine::
Process`, there is no: network I/O, disk I/O, logging, blocking
synchronization, or (in `fast::OrderBook`) per-order heap allocation.
Everything that needs any of those — journaling, market data, the socket
read/write loops — runs on other threads and communicates only through
`SpscQueue`.
