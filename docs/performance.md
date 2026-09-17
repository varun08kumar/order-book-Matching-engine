# Performance

All numbers below were measured by actually running the benchmark binaries
in `benchmarks/` (Release build, `-O3` via `CMAKE_BUILD_TYPE=Release`) on:

- Apple M4, macOS (Darwin 25.6.0), single-threaded (no contention) for
  benchmarks 1-4; the end-to-end benchmark additionally involves the
  gateway's accept/reader/writer/dispatch threads described in
  `docs/architecture.md`.
- Compiler: AppleClang 21 (Clang), `-O3`, no sanitizers.

These are **illustrative measurements from one development machine**, not
formal targets or guaranteed SLAs - different hardware, OS scheduler
behavior, and background load will change them. What is guaranteed is that
every number below came from a benchmark that actually measured it (see
`benchmarks/bench_harness.hpp`): latency is per-operation wall-clock time
via `std::chrono::steady_clock`, and allocation counts come from overriding
global `operator new`/`operator delete` for the benchmark binaries
(`benchmarks/allocation_counter.cpp`) and resetting the counter right
before each timed region.

To reproduce: `cmake --build build --target bench_order_insertion
bench_order_cancellation bench_matching bench_mixed_workload
bench_end_to_end_tcp` then run each binary in `build/benchmarks/`.

## 1. Order insertion

200,000 resting BUY limit orders inserted into an otherwise-empty book (no
SELL side present, so nothing matches - isolates accept+insert cost).

| Book | ops/sec | p50 | p95 | p99 | p99.9 | allocations |
|------|--------:|----:|----:|----:|------:|------------:|
| `naive::OrderBook` | 3,737,312 | 208 ns | 541 ns | 1,167 ns | 3,375 ns | 478,192 |
| `fast::OrderBook`  | 4,651,134 | 167 ns | 417 ns | 625 ns | 1,542 ns | 278,263 |

`fast::OrderBook` allocates less (no per-order `std::list` node, no pooled
`Order` heap allocation) but not zero: every *new distinct price level* is
still a `std::map` node allocation in both implementations, and the
`unordered_map` order-id index also allocates/rehashes in both. That
residual cost is deliberate and documented in `docs/architecture.md` - it
was not eliminated because doing so (e.g. a custom pooled-node map
allocator) was judged out of scope relative to its benefit at these order
counts.

## 2. Order cancellation

200,000 resting orders pre-populated, then cancelled in a shuffled (not
sequential) order.

| Book | ops/sec | p50 | p95 | p99 | p99.9 | allocations |
|------|--------:|----:|----:|----:|------:|------------:|
| `naive::OrderBook` | 2,382,075 | 416 ns | 750 ns | 1,000 ns | 1,583 ns | 21 |
| `fast::OrderBook`  | 3,575,431 | 250 ns | 458 ns | 583 ns | 750 ns | 21 |

Both implementations do effectively zero allocation on cancel (freeing
memory dominates: 486,554 vs 286,676 deallocations - the fast book's pool
`Release` plus map-node erase frees less than the naive book's list-node
plus map-node erase). Latency is dominated by the `std::map`/
`std::unordered_map` lookups both books share, which is why the gap here is
narrower than for insertion.

## 3. Matching

200,000 resting SELL orders (qty 1 each, distinct ascending prices)
pre-populated, then 200,000 qty-1 IOC market BUY orders sweep the book from
best to worst, one trade per operation.

| Book | ops/sec | trades/sec | p50 | p95 | p99 | p99.9 |
|------|--------:|-----------:|----:|----:|----:|------:|
| `naive::OrderBook` | 10,608,974 | 10,608,974 | 83 ns | 125 ns | 208 ns | 1,208 ns |
| `fast::OrderBook`  | 17,119,730 | 17,119,730 | 42 ns | 84 ns | 209 ns | 334 ns |

This is where the intrusive-list + pool design pays off most directly: each
matched order is both erased from its FIFO queue and released back to a
pool slot in O(1) with no allocator call, versus `std::list::erase` freeing
a heap node.

## 4. Mixed workload

300,000 operations: ~60% new limit orders (some crossing and trading),
~20% cancels, ~20% modifies, against a live pool of resting orders in a
deliberately narrow price band (so books get deep and a single aggressive
order can generate several trades).

| Book | ops/sec | trades/sec | p50 | p95 | p99 | p99.9 |
|------|--------:|-----------:|----:|----:|----:|------:|
| `naive::OrderBook` | 7,164,957 | 3,564,518 | 125 ns | 292 ns | 458 ns | 667 ns |
| `fast::OrderBook`  | 11,454,942 | 5,698,757 | 83 ns | 167 ns | 250 ns | 375 ns |

## 5. End-to-end: TCP -> decode -> match

A real TCP client sends one `NewOrder` (IOC market buy, qty 1) at a time
over a loopback socket to a `TcpGateway`-fronted engine seeded with 1M
resting sell orders, and waits for its Accepted + Trade + BookUpdate
response before sending the next. This measures the whole pipeline in
`docs/architecture.md`'s data-flow diagram, including real `recv()`/
`send()` syscalls and two extra threads (session reader/writer) plus the
engine-driving thread.

| ops/sec | trades/sec | p50 | p95 | p99 | p99.9 |
|--------:|-----------:|----:|----:|----:|------:|
| 46,501 | 46,501 | 20,667 ns | 26,542 ns | 45,792 ns | 105,500 ns |

Three orders of magnitude slower than the in-process matching benchmark -
expected, since this now pays for two loopback TCP round-trips' worth of
syscalls and context switches per operation instead of a single function
call. This is also why the architecture keeps network I/O off the matching
hot path entirely: the cost above belongs to the gateway/socket layer, not
to matching itself, and does not accumulate per-order inside a partition
that is also serving other sessions concurrently.

## What was optimized and what was not

Optimized (Phase 10, `fast::OrderBook`):
- Order storage: `ObjectPool<Node>` instead of per-order heap allocation.
- FIFO queue per price level: intrusive doubly-linked list (O(1) unlink
  given a node pointer) instead of `std::list` (same complexity, but every
  node was a separate heap allocation).

Deliberately left alone (documented, not overlooked):
- Price-level storage is still `std::map` in both books - allocates a tree
  node per *distinct price level*, not per order. At realistic order-to-
  price-level ratios this is a small fraction of total allocations (see the
  insertion benchmark: 278,263 allocations for 200,000 orders is roughly
  1.4 per order, not 1-to-1), and replacing it (e.g. a pooled-node
  allocator for `std::map`, or a flat sorted structure) was judged
  premature optimization for an educational system relative to the
  complexity it would add.
- The order-id index (`std::unordered_map`) is unchanged between the two
  books for the same reason.
