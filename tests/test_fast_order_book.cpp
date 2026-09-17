#include "matching_engine/fast/order_book.hpp"

#include <gtest/gtest.h>

using namespace me;
using namespace me::fast;

namespace {
constexpr SymbolId kSym = 1;
}

TEST(FastOrderBook, RestingLimitOrderNoMatch) {
  OrderBook book(kSym, 16);
  auto r = book.AddOrder(1, 100, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::GoodTillCancel, 1, 1);
  EXPECT_TRUE(r.accepted);
  EXPECT_EQ(r.final_status, OrderStatus::New);
  EXPECT_EQ(book.BestBid(), 1000);
}

TEST(FastOrderBook, SimpleFullMatchAtRestingPrice) {
  OrderBook book(kSym, 16);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 990, 5, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 5,
                          TimeInForce::GoodTillCancel, 2, 2);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].price, 990);
  EXPECT_FALSE(book.HasOrder(1));
  EXPECT_FALSE(book.HasOrder(2));
}

TEST(FastOrderBook, PartialFillAndMultipleFills) {
  OrderBook book(kSym, 16);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1001, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 1001, 8,
                          TimeInForce::GoodTillCancel, 3, 3);
  ASSERT_EQ(r.trades.size(), 2u);
  EXPECT_EQ(r.trades[0].price, 1000);
  EXPECT_EQ(r.trades[0].quantity, 5u);
  EXPECT_EQ(r.trades[1].price, 1001);
  EXPECT_EQ(r.trades[1].quantity, 3u);
  EXPECT_EQ(book.QuantityAt(Side::Sell, 1001), 2u);
}

TEST(FastOrderBook, PriceTimePriorityFifo) {
  OrderBook book(kSym, 16);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 1000, 5,
                          TimeInForce::GoodTillCancel, 3, 3);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].resting_order_id, 1u);
  EXPECT_TRUE(book.HasOrder(2));
}

TEST(FastOrderBook, CancelFreesSlotBackToPool) {
  OrderBook book(kSym, 2);
  book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 999, 5, TimeInForce::GoodTillCancel, 2, 2);
  EXPECT_EQ(book.PoolAvailable(), 0u);

  auto c = book.CancelOrder(1);
  EXPECT_TRUE(c.success);
  EXPECT_EQ(book.PoolAvailable(), 1u);
  EXPECT_FALSE(book.HasOrder(1));
  EXPECT_TRUE(book.HasOrder(2));

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 998, 5,
                          TimeInForce::GoodTillCancel, 3, 3);
  EXPECT_TRUE(r.accepted);  // reused the freed slot
}

TEST(FastOrderBook, PoolExhaustionRejectsNewRestingOrder) {
  OrderBook book(kSym, 1);
  auto r1 = book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 1000, 5,
                           TimeInForce::GoodTillCancel, 1, 1);
  EXPECT_TRUE(r1.accepted);

  auto r2 = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 999, 5,
                           TimeInForce::GoodTillCancel, 2, 2);
  EXPECT_FALSE(r2.accepted);
  EXPECT_EQ(r2.reject_reason, RejectReason::PoolExhausted);
}

TEST(FastOrderBook, ModifyQuantityDecreaseKeepsPriority) {
  OrderBook book(kSym, 16);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 2, 2);

  auto m = book.ModifyOrder(1, 1000, 3, 3, 3);
  EXPECT_TRUE(m.success);
  EXPECT_FALSE(m.lost_priority);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 1000, 3,
                          TimeInForce::GoodTillCancel, 4, 4);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].resting_order_id, 1u);
}

TEST(FastOrderBook, MarketOrderSweepsBook) {
  OrderBook book(kSym, 16);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1001, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Market, 0, 8,
                          TimeInForce::ImmediateOrCancel, 3, 3);
  ASSERT_EQ(r.trades.size(), 2u);
  EXPECT_EQ(r.filled_quantity, 8u);
  EXPECT_FALSE(book.HasOrder(3));
}
