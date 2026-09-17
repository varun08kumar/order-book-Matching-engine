// Benchmark 4: mixed workload. A single stream of ~60% new limit orders
// (a slice of which cross and trade), ~20% cancels, and ~20% modifies
// against a live pool of resting order ids, matching a shape a real
// exchange session would see rather than isolating one operation type.
#include <random>
#include <vector>

#include "bench_harness.hpp"
#include "matching_engine/fast/order_book.hpp"
#include "matching_engine/naive/order_book.hpp"

using namespace me;
using namespace me::bench;

namespace {
constexpr SymbolId kSym = 1;
constexpr std::size_t kIterations = 300000;

template <typename Book>
void RunMixed(const char* label, Book& book) {
  std::mt19937 rng(99);
  std::uniform_int_distribution<int> op_dist(0, 99);
  std::uniform_int_distribution<Price> price_dist(950, 1050);
  std::uniform_int_distribution<Quantity> qty_dist(1, 20);
  std::uniform_int_distribution<int> side_dist(0, 1);

  std::vector<OrderId> live_ids;
  live_ids.reserve(kIterations);
  OrderId next_id = 1;
  SequenceNumber seq = 1;
  std::size_t trades = 0;

  // trades/sec must reflect only the timed region, so warmup is run
  // manually (with its own throwaway counter) before RunBenchmark's timed
  // loop starts, rather than relying on RunBenchmark's internal warmup
  // (which shares this same `trades` accumulator across both phases).
  auto step = [&](std::size_t& trade_sink) {
    int op = op_dist(rng);
    if (op < 60 || live_ids.empty()) {
      OrderId id = next_id++;
      Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      auto r = book.AddOrder(id, 1, side, OrderType::Limit, price_dist(rng), qty_dist(rng),
                              TimeInForce::GoodTillCancel, seq++, id);
      trade_sink += r.trades.size();
      if (r.accepted && r.remaining_quantity > 0) live_ids.push_back(id);
    } else if (op < 80) {
      std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
      std::size_t idx = pick(rng);
      book.CancelOrder(live_ids[idx]);
      live_ids[idx] = live_ids.back();
      live_ids.pop_back();
    } else {
      std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
      std::size_t idx = pick(rng);
      auto r = book.ModifyOrder(live_ids[idx], price_dist(rng), qty_dist(rng), seq++, 0);
      trade_sink += r.trades.size();
      if (!r.success || r.remaining_quantity == 0) {
        live_ids[idx] = live_ids.back();
        live_ids.pop_back();
      }
    }
  };

  std::size_t warmup_trades_discarded = 0;
  for (std::size_t i = 0; i < kIterations / 10; ++i) step(warmup_trades_discarded);

  auto rec = RunBenchmark(label, 0, kIterations, [&](std::size_t) { step(trades); });
  rec.Report(trades);
}
}  // namespace

int main() {
  {
    naive::OrderBook book(kSym);
    RunMixed("mixed_workload / naive::OrderBook", book);
  }
  {
    fast::OrderBook book(kSym, kIterations + 16);
    RunMixed("mixed_workload / fast::OrderBook", book);
  }
  return 0;
}
