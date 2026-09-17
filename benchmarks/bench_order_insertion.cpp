// Benchmark 1: order insertion. Inserts N resting limit BUY orders (no
// SELL side present, so nothing ever matches — this isolates the cost of
// "accept + insert into the book" from matching cost) into both the
// correctness-first and optimized books.
#include <cstdio>
#include <random>

#include "bench_harness.hpp"
#include "matching_engine/fast/order_book.hpp"
#include "matching_engine/naive/order_book.hpp"

using namespace me;
using namespace me::bench;

namespace {
constexpr SymbolId kSym = 1;
constexpr std::size_t kWarmup = 10000;
constexpr std::size_t kIterations = 200000;
}  // namespace

int main() {
  std::mt19937 rng(42);
  std::uniform_int_distribution<Price> price_dist(1, 100000);
  std::uniform_int_distribution<Quantity> qty_dist(1, 1000);

  {
    naive::OrderBook book(kSym);
    auto rec = RunBenchmark("order_insertion / naive::OrderBook", kWarmup, kIterations,
                             [&](std::size_t i) {
                               book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Buy,
                                             OrderType::Limit, price_dist(rng), qty_dist(rng),
                                             TimeInForce::GoodTillCancel,
                                             static_cast<SequenceNumber>(i) + 1, i);
                             });
    rec.Report();
  }

  {
    fast::OrderBook book(kSym, kWarmup + kIterations + 16);
    auto rec = RunBenchmark("order_insertion / fast::OrderBook", kWarmup, kIterations,
                             [&](std::size_t i) {
                               book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Buy,
                                             OrderType::Limit, price_dist(rng), qty_dist(rng),
                                             TimeInForce::GoodTillCancel,
                                             static_cast<SequenceNumber>(i) + 1, i);
                             });
    rec.Report();
  }
  return 0;
}
