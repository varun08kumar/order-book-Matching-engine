#include "matching_engine/naive/order_book.hpp"

#include <gtest/gtest.h>

using namespace me;
using namespace me::naive;

namespace {
constexpr SymbolId kSym = 1;
}

TEST(NaiveOrderBook, RestingLimitOrderNoMatch) {
  OrderBook book(kSym);
  auto r = book.AddOrder(1, 100, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::GoodTillCancel, 1, 1000);
  EXPECT_TRUE(r.accepted);
  EXPECT_EQ(r.final_status, OrderStatus::New);
  EXPECT_EQ(r.filled_quantity, 0u);
  EXPECT_TRUE(r.trades.empty());
  EXPECT_EQ(book.BestBid(), 1000);
  EXPECT_FALSE(book.HasBestAsk());
}

TEST(NaiveOrderBook, SimpleFullMatch) {
  OrderBook book(kSym);
  book.AddOrder(1, 100, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 200, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::GoodTillCancel, 2, 2);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].price, 1000);
  EXPECT_EQ(r.trades[0].quantity, 10u);
  EXPECT_EQ(r.trades[0].resting_order_id, 1u);
  EXPECT_EQ(r.trades[0].aggressor_order_id, 2u);
  EXPECT_EQ(r.final_status, OrderStatus::Filled);
  EXPECT_FALSE(book.HasOrder(1));
  EXPECT_FALSE(book.HasOrder(2));
  EXPECT_FALSE(book.HasBestBid());
  EXPECT_FALSE(book.HasBestAsk());
}

TEST(NaiveOrderBook, TradesExecuteAtRestingPrice) {
  OrderBook book(kSym);
  // Resting sell at 990; aggressive buy willing to pay up to 1000.
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 990, 5, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 5,
                          TimeInForce::GoodTillCancel, 2, 2);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].price, 990);  // resting price, not aggressor's limit
}

TEST(NaiveOrderBook, PartialFill) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::GoodTillCancel, 2, 2);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].quantity, 5u);
  EXPECT_EQ(r.filled_quantity, 5u);
  EXPECT_EQ(r.remaining_quantity, 5u);
  EXPECT_EQ(r.final_status, OrderStatus::PartiallyFilled);
  EXPECT_EQ(book.BestBid(), 1000);
  EXPECT_EQ(book.QuantityAt(Side::Buy, 1000), 5u);
}

TEST(NaiveOrderBook, MultipleFillsAcrossLevels) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1001, 5, TimeInForce::GoodTillCancel, 2, 2);
  book.AddOrder(3, 3, Side::Sell, OrderType::Limit, 1002, 5, TimeInForce::GoodTillCancel, 3, 3);

  auto r = book.AddOrder(4, 4, Side::Buy, OrderType::Limit, 1002, 12,
                          TimeInForce::GoodTillCancel, 4, 4);
  ASSERT_EQ(r.trades.size(), 3u);
  EXPECT_EQ(r.trades[0].price, 1000);
  EXPECT_EQ(r.trades[1].price, 1001);
  EXPECT_EQ(r.trades[2].price, 1002);
  EXPECT_EQ(r.trades[2].quantity, 2u);
  EXPECT_EQ(r.filled_quantity, 12u);
  EXPECT_EQ(r.remaining_quantity, 0u);
  EXPECT_EQ(book.QuantityAt(Side::Sell, 1002), 3u);
}

TEST(NaiveOrderBook, PriceTimePriorityFifoAtSameLevel) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 1000, 5,
                          TimeInForce::GoodTillCancel, 3, 3);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].resting_order_id, 1u);  // first-in, first matched
  EXPECT_TRUE(book.HasOrder(2));
  EXPECT_FALSE(book.HasOrder(1));
}

TEST(NaiveOrderBook, CancelRemovesOrderWithoutScanningAffectingOthers) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto c = book.CancelOrder(1);
  EXPECT_TRUE(c.success);
  EXPECT_FALSE(book.HasOrder(1));
  EXPECT_TRUE(book.HasOrder(2));
  EXPECT_EQ(book.QuantityAt(Side::Buy, 1000), 5u);
}

TEST(NaiveOrderBook, CancelUnknownOrderRejected) {
  OrderBook book(kSym);
  auto c = book.CancelOrder(999);
  EXPECT_FALSE(c.success);
  EXPECT_EQ(c.reject_reason, RejectReason::UnknownOrder);
}

TEST(NaiveOrderBook, ModifyQuantityDecreaseKeepsPriority) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 2, 2);

  auto m = book.ModifyOrder(1, 1000, 3, 3, 3);
  EXPECT_TRUE(m.success);
  EXPECT_FALSE(m.lost_priority);
  EXPECT_EQ(book.QuantityAt(Side::Sell, 1000), 13u);

  // Order 1 should still be first in line despite the modify.
  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Limit, 1000, 3,
                          TimeInForce::GoodTillCancel, 4, 4);
  ASSERT_EQ(r.trades.size(), 1u);
  EXPECT_EQ(r.trades[0].resting_order_id, 1u);
}

TEST(NaiveOrderBook, ModifyPriceChangeLosesPriorityAndCanMatch) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 995, 10, TimeInForce::GoodTillCancel, 2, 2);

  // Lower the ask to 995: now crosses the resting bid and should trade.
  auto m = book.ModifyOrder(1, 995, 10, 3, 3);
  EXPECT_TRUE(m.success);
  EXPECT_TRUE(m.lost_priority);
  ASSERT_EQ(m.trades.size(), 1u);
  EXPECT_EQ(m.trades[0].price, 995);
  EXPECT_EQ(m.final_status, OrderStatus::Filled);
}

TEST(NaiveOrderBook, ModifyUnknownOrderRejected) {
  OrderBook book(kSym);
  auto m = book.ModifyOrder(42, 1000, 5, 1, 1);
  EXPECT_FALSE(m.success);
  EXPECT_EQ(m.reject_reason, RejectReason::UnknownOrder);
}

TEST(NaiveOrderBook, MarketOrderMatchesBestPrices) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 2, Side::Sell, OrderType::Limit, 1001, 5, TimeInForce::GoodTillCancel, 2, 2);

  auto r = book.AddOrder(3, 3, Side::Buy, OrderType::Market, 0, 8,
                          TimeInForce::ImmediateOrCancel, 3, 3);
  ASSERT_EQ(r.trades.size(), 2u);
  EXPECT_EQ(r.trades[0].price, 1000);
  EXPECT_EQ(r.trades[1].price, 1001);
  EXPECT_EQ(r.trades[1].quantity, 3u);
  EXPECT_EQ(r.filled_quantity, 8u);
  EXPECT_FALSE(book.HasOrder(3));  // market orders never rest
}

TEST(NaiveOrderBook, MarketOrderRejectedWhenNoLiquidity) {
  OrderBook book(kSym);
  auto r = book.AddOrder(1, 1, Side::Buy, OrderType::Market, 0, 5,
                          TimeInForce::ImmediateOrCancel, 1, 1);
  EXPECT_FALSE(r.accepted);
  EXPECT_EQ(r.reject_reason, RejectReason::MarketOrderNoLiquidity);
}

TEST(NaiveOrderBook, MarketOrderPartialFillCancelsRemainder) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 3, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Market, 0, 10,
                          TimeInForce::ImmediateOrCancel, 2, 2);
  EXPECT_EQ(r.filled_quantity, 3u);
  EXPECT_EQ(r.remaining_quantity, 0u);
  EXPECT_EQ(r.final_status, OrderStatus::Cancelled);
}

TEST(NaiveOrderBook, ImmediateOrCancelDoesNotRest) {
  OrderBook book(kSym);
  auto r = book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 1000, 5,
                          TimeInForce::ImmediateOrCancel, 1, 1);
  EXPECT_TRUE(r.accepted);
  EXPECT_EQ(r.final_status, OrderStatus::Cancelled);
  EXPECT_FALSE(book.HasOrder(1));
  EXPECT_FALSE(book.HasBestBid());
}

TEST(NaiveOrderBook, FillOrKillRejectedWhenInsufficientLiquidity) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 3, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::FillOrKill, 2, 2);
  EXPECT_FALSE(r.accepted);
  EXPECT_EQ(r.reject_reason, RejectReason::FillOrKillNotFilled);
  EXPECT_TRUE(book.HasOrder(1));  // untouched
}

TEST(NaiveOrderBook, FillOrKillExecutesFullyWhenLiquiditySufficient) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Sell, OrderType::Limit, 1000, 10, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(2, 2, Side::Buy, OrderType::Limit, 1000, 10,
                          TimeInForce::FillOrKill, 2, 2);
  EXPECT_TRUE(r.accepted);
  EXPECT_EQ(r.filled_quantity, 10u);
  EXPECT_EQ(r.final_status, OrderStatus::Filled);
}

TEST(NaiveOrderBook, RejectsDuplicateOrderId) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1, 1);
  auto r = book.AddOrder(1, 2, Side::Sell, OrderType::Limit, 1000, 5,
                          TimeInForce::GoodTillCancel, 2, 2);
  EXPECT_FALSE(r.accepted);
  EXPECT_EQ(r.reject_reason, RejectReason::DuplicateOrderId);
}

TEST(NaiveOrderBook, RejectsInvalidPriceAndQuantity) {
  OrderBook book(kSym);
  auto r1 = book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 0, 5, TimeInForce::GoodTillCancel, 1, 1);
  EXPECT_EQ(r1.reject_reason, RejectReason::InvalidPrice);
  auto r2 = book.AddOrder(2, 1, Side::Buy, OrderType::Limit, 1000, 0, TimeInForce::GoodTillCancel, 2, 2);
  EXPECT_EQ(r2.reject_reason, RejectReason::InvalidQuantity);
}

TEST(NaiveOrderBook, BestBidIsHighestBestAskIsLowest) {
  OrderBook book(kSym);
  book.AddOrder(1, 1, Side::Buy, OrderType::Limit, 990, 5, TimeInForce::GoodTillCancel, 1, 1);
  book.AddOrder(2, 1, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 2, 2);
  book.AddOrder(3, 1, Side::Buy, OrderType::Limit, 995, 5, TimeInForce::GoodTillCancel, 3, 3);
  EXPECT_EQ(book.BestBid(), 1000);

  book.AddOrder(4, 2, Side::Sell, OrderType::Limit, 1050, 5, TimeInForce::GoodTillCancel, 4, 4);
  book.AddOrder(5, 2, Side::Sell, OrderType::Limit, 1010, 5, TimeInForce::GoodTillCancel, 5, 5);
  book.AddOrder(6, 2, Side::Sell, OrderType::Limit, 1020, 5, TimeInForce::GoodTillCancel, 6, 6);
  EXPECT_EQ(book.BestAsk(), 1010);
}
