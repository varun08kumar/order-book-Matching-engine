// Benchmark 3: matching. Pre-populates N resting SELL orders (qty 1 each)
// at N distinct ascending prices, then fires N aggressive qty-1 IOC BUY
// orders that each generate exactly one trade against the best remaining
// ask, sweeping the book exactly once. Isolates per-trade matching cost.
#include "bench_harness.hpp"
#include "matching_engine/fast/order_book.hpp"
#include "matching_engine/naive/order_book.hpp"

using namespace me;
using namespace me::bench;

namespace {
constexpr SymbolId kSym = 1;
constexpr std::size_t kCount = 200000;
}  // namespace

int main() {
  {
    naive::OrderBook book(kSym);
    for (std::size_t i = 0; i < kCount; ++i) {
      book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Sell, OrderType::Limit,
                    static_cast<Price>(i) + 1, 1, TimeInForce::GoodTillCancel,
                    static_cast<SequenceNumber>(i) + 1, i);
    }
    std::size_t trades = 0;
    auto rec = RunBenchmark("matching / naive::OrderBook", 0, kCount, [&](std::size_t i) {
      auto r = book.AddOrder(static_cast<OrderId>(kCount) + static_cast<OrderId>(i) + 1, 2,
                              Side::Buy, OrderType::Market, 0, 1, TimeInForce::ImmediateOrCancel,
                              static_cast<SequenceNumber>(kCount + i) + 1, kCount + i);
      trades += r.trades.size();
    });
    rec.Report(trades);
  }

  {
    fast::OrderBook book(kSym, kCount + 16);
    for (std::size_t i = 0; i < kCount; ++i) {
      book.AddOrder(static_cast<OrderId>(i) + 1, 1, Side::Sell, OrderType::Limit,
                    static_cast<Price>(i) + 1, 1, TimeInForce::GoodTillCancel,
                    static_cast<SequenceNumber>(i) + 1, i);
    }
    std::size_t trades = 0;
    auto rec = RunBenchmark("matching / fast::OrderBook", 0, kCount, [&](std::size_t i) {
      auto r = book.AddOrder(static_cast<OrderId>(kCount) + static_cast<OrderId>(i) + 1, 2,
                              Side::Buy, OrderType::Market, 0, 1, TimeInForce::ImmediateOrCancel,
                              static_cast<SequenceNumber>(kCount + i) + 1, kCount + i);
      trades += r.trades.size();
    });
    rec.Report(trades);
  }
  return 0;
}
