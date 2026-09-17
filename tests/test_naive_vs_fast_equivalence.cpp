// Differential test: drives naive::OrderBook and fast::OrderBook with the
// exact same pseudo-random (fixed-seed, deterministic) sequence of
// new/cancel/modify operations and asserts every observable result and the
// resulting book state agree at every step. This is what actually licenses
// treating the fast book as a drop-in, allocation-free replacement for the
// correctness-first reference implementation in benchmarks.
#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "matching_engine/fast/order_book.hpp"
#include "matching_engine/naive/order_book.hpp"

using namespace me;

namespace {
constexpr SymbolId kSym = 1;
constexpr std::size_t kMaxOrders = 20000;

void ExpectTradesEqual(const std::vector<Trade>& a, const std::vector<Trade>& b) {
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].price, b[i].price) << "trade " << i;
    EXPECT_EQ(a[i].quantity, b[i].quantity) << "trade " << i;
    EXPECT_EQ(a[i].aggressor_order_id, b[i].aggressor_order_id) << "trade " << i;
    EXPECT_EQ(a[i].resting_order_id, b[i].resting_order_id) << "trade " << i;
  }
}

void ExpectBooksEqual(const naive::OrderBook& n, const fast::OrderBook& f) {
  ASSERT_EQ(n.HasBestBid(), f.HasBestBid());
  ASSERT_EQ(n.HasBestAsk(), f.HasBestAsk());
  if (n.HasBestBid()) EXPECT_EQ(n.BestBid(), f.BestBid());
  if (n.HasBestAsk()) EXPECT_EQ(n.BestAsk(), f.BestAsk());
  EXPECT_EQ(n.TotalOrderCount(), f.TotalOrderCount());

  for (auto [price, qty] : n.BidLevels()) {
    EXPECT_EQ(f.QuantityAt(Side::Buy, price), qty) << "bid @ " << price;
    EXPECT_EQ(f.OrderCountAt(Side::Buy, price), n.OrderCountAt(Side::Buy, price)) << "bid @ " << price;
  }
  for (auto [price, qty] : n.AskLevels()) {
    EXPECT_EQ(f.QuantityAt(Side::Sell, price), qty) << "ask @ " << price;
    EXPECT_EQ(f.OrderCountAt(Side::Sell, price), n.OrderCountAt(Side::Sell, price)) << "ask @ " << price;
  }
}
}  // namespace

TEST(NaiveVsFastEquivalence, RandomOperationSequenceProducesIdenticalState) {
  naive::OrderBook naive_book(kSym);
  fast::OrderBook fast_book(kSym, kMaxOrders);

  std::mt19937 rng(0xC0FFEE);
  std::uniform_int_distribution<int> op_dist(0, 99);
  std::uniform_int_distribution<Price> price_dist(950, 1050);
  std::uniform_int_distribution<Quantity> qty_dist(1, 20);
  std::uniform_int_distribution<int> side_dist(0, 1);

  std::vector<OrderId> live_ids;
  OrderId next_id = 1;
  SequenceNumber seq = 1;

  constexpr int kOps = 5000;
  for (int i = 0; i < kOps; ++i) {
    int op = op_dist(rng);
    Timestamp ts = static_cast<Timestamp>(i);

    if (op < 70 || live_ids.empty()) {
      // New order (limit, occasionally market).
      OrderId id = next_id++;
      Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      bool market = (op_dist(rng) < 5);
      Price price = market ? 0 : price_dist(rng);
      Quantity qty = qty_dist(rng);
      OrderType type = market ? OrderType::Market : OrderType::Limit;
      TimeInForce tif = market ? TimeInForce::ImmediateOrCancel : TimeInForce::GoodTillCancel;

      auto rn = naive_book.AddOrder(id, id, side, type, price, qty, tif, seq, ts);
      auto rf = fast_book.AddOrder(id, id, side, type, price, qty, tif, seq, ts);
      ++seq;

      ASSERT_EQ(rn.accepted, rf.accepted) << "op " << i << " id " << id;
      if (rn.accepted) {
        EXPECT_EQ(rn.final_status, rf.final_status) << "op " << i;
        EXPECT_EQ(rn.filled_quantity, rf.filled_quantity) << "op " << i;
        EXPECT_EQ(rn.remaining_quantity, rf.remaining_quantity) << "op " << i;
        ExpectTradesEqual(rn.trades, rf.trades);
        if (rn.remaining_quantity > 0 && type == OrderType::Limit) live_ids.push_back(id);
      }
    } else if (op < 90) {
      // Cancel a random live order.
      std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
      std::size_t idx = pick(rng);
      OrderId id = live_ids[idx];
      auto cn = naive_book.CancelOrder(id);
      auto cf = fast_book.CancelOrder(id);
      EXPECT_EQ(cn.success, cf.success) << "op " << i << " id " << id;
      live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
    } else {
      // Modify a random live order.
      std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
      std::size_t idx = pick(rng);
      OrderId id = live_ids[idx];
      Price new_price = price_dist(rng);
      Quantity new_qty = qty_dist(rng);

      auto mn = naive_book.ModifyOrder(id, new_price, new_qty, seq, ts);
      auto mf = fast_book.ModifyOrder(id, new_price, new_qty, seq, ts);
      ++seq;

      ASSERT_EQ(mn.success, mf.success) << "op " << i << " id " << id;
      if (mn.success) {
        EXPECT_EQ(mn.final_status, mf.final_status) << "op " << i;
        EXPECT_EQ(mn.remaining_quantity, mf.remaining_quantity) << "op " << i;
        ExpectTradesEqual(mn.trades, mf.trades);
        if (mn.remaining_quantity == 0 || mn.final_status == OrderStatus::Filled ||
            mn.final_status == OrderStatus::Cancelled) {
          live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
        }
      } else {
        live_ids.erase(live_ids.begin() + static_cast<std::ptrdiff_t>(idx));
      }
    }

    if (i % 200 == 0) ExpectBooksEqual(naive_book, fast_book);
  }

  ExpectBooksEqual(naive_book, fast_book);
}
