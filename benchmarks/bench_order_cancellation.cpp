// Benchmark 2: order cancellation. Pre-populates a book with N resting
// orders, then cancels them in a randomly shuffled order (so cancellation
// cost cannot benefit from favorable/sequential access patterns), timing
// only the cancel calls.
#include <algorithm>
#include <random>
#include <vector>

#include "bench_harness.hpp"
#include "matching_engine/fast/order_book.hpp"
#include "matching_engine/naive/order_book.hpp"

using namespace me;
using namespace me::bench;

namespace {
constexpr SymbolId kSym = 1;
constexpr std::size_t kCount = 200000;

std::vector<OrderId> ShuffledIds(std::size_t n, unsigned seed) {
  std::vector<OrderId> ids(n);
  for (std::size_t i = 0; i < n; ++i) ids[i] = static_cast<OrderId>(i) + 1;
  std::mt19937 rng(seed);
  std::shuffle(ids.begin(), ids.end(), rng);
  return ids;
}
}  // namespace

int main() {
  std::mt19937 rng(7);
  std::uniform_int_distribution<Price> price_dist(1, 100000);
  std::uniform_int_distribution<Quantity> qty_dist(1, 1000);

  {
    naive::OrderBook book(kSym);
    for (std::size_t i = 0; i < kCount; ++i) {
      book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Buy, OrderType::Limit, price_dist(rng),
                    qty_dist(rng), TimeInForce::GoodTillCancel, static_cast<SequenceNumber>(i) + 1,
                    i);
    }
    auto ids = ShuffledIds(kCount, 123);
    auto rec = RunBenchmark("order_cancellation / naive::OrderBook", 0, kCount,
                             [&](std::size_t i) { book.CancelOrder(ids[i]); });
    rec.Report();
  }

  {
    fast::OrderBook book(kSym, kCount + 16);
    for (std::size_t i = 0; i < kCount; ++i) {
      book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Buy, OrderType::Limit, price_dist(rng),
                    qty_dist(rng), TimeInForce::GoodTillCancel, static_cast<SequenceNumber>(i) + 1,
                    i);
    }
    auto ids = ShuffledIds(kCount, 123);
    auto rec = RunBenchmark("order_cancellation / fast::OrderBook", 0, kCount,
                             [&](std::size_t i) { book.CancelOrder(ids[i]); });
    rec.Report();
  }
  return 0;
}
