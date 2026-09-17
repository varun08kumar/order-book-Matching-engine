#include "matching_engine/net/protocol.hpp"

#include <gtest/gtest.h>

using namespace me;
using namespace me::net;

TEST(Protocol, RoundTripsNewOrderCommand) {
  NewOrderCommand c;
  c.order_id = 123;
  c.trader_id = 456;
  c.symbol_id = 7;
  c.side = Side::Sell;
  c.type = OrderType::Limit;
  c.time_in_force = TimeInForce::ImmediateOrCancel;
  c.price = -9999;
  c.quantity = 1000;
  c.timestamp = 111222333;

  auto body = EncodeCommand(Command{c});
  auto decoded = DecodeCommand(body);
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<NewOrderCommand>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->order_id, c.order_id);
  EXPECT_EQ(got->trader_id, c.trader_id);
  EXPECT_EQ(got->symbol_id, c.symbol_id);
  EXPECT_EQ(got->side, c.side);
  EXPECT_EQ(got->type, c.type);
  EXPECT_EQ(got->time_in_force, c.time_in_force);
  EXPECT_EQ(got->price, c.price);
  EXPECT_EQ(got->quantity, c.quantity);
  EXPECT_EQ(got->timestamp, c.timestamp);
}

TEST(Protocol, RoundTripsCancelCommand) {
  CancelCommand c{1, 2, 3, 4};
  auto decoded = DecodeCommand(EncodeCommand(Command{c}));
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<CancelCommand>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->order_id, 1u);
  EXPECT_EQ(got->trader_id, 2u);
  EXPECT_EQ(got->symbol_id, 3u);
  EXPECT_EQ(got->timestamp, 4u);
}

TEST(Protocol, RoundTripsModifyCommand) {
  ModifyCommand c{1, 2, 3, 5000, 10, 6};
  auto decoded = DecodeCommand(EncodeCommand(Command{c}));
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<ModifyCommand>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->new_price, 5000);
  EXPECT_EQ(got->new_quantity, 10u);
}

TEST(Protocol, RejectsEmptyOrTruncatedBody) {
  EXPECT_FALSE(DecodeCommand({}).has_value());
  std::vector<std::uint8_t> truncated = {0x01, 0x02, 0x03};  // NewOrder type but no payload
  EXPECT_FALSE(DecodeCommand(truncated).has_value());
}

TEST(Protocol, RejectsUnknownMessageType) {
  std::vector<std::uint8_t> body = {0xFF};
  EXPECT_FALSE(DecodeCommand(body).has_value());
}

TEST(Protocol, RoundTripsTradeEvent) {
  Trade t;
  t.sequence_number = 10;
  t.timestamp = 20;
  t.symbol_id = 30;
  t.aggressor_order_id = 1;
  t.resting_order_id = 2;
  t.aggressor_trader_id = 3;
  t.resting_trader_id = 4;
  t.aggressor_side = Side::Buy;
  t.price = 100;
  t.quantity = 5;

  auto decoded = DecodeEvent(EncodeEvent(Event{t}));
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<Trade>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->price, 100);
  EXPECT_EQ(got->quantity, 5u);
  EXPECT_EQ(got->aggressor_order_id, 1u);
  EXPECT_EQ(got->resting_order_id, 2u);
}

TEST(Protocol, RoundTripsAcceptedEvent) {
  AcceptedEvent e;
  e.sequence_number = 1;
  e.timestamp = 2;
  e.symbol_id = 3;
  e.order_id = 4;
  e.trader_id = 5;
  e.side = Side::Sell;
  e.type = OrderType::Market;
  e.price = 0;
  e.quantity = 10;
  e.filled_quantity = 10;
  e.remaining_quantity = 0;
  e.status = OrderStatus::Filled;

  auto decoded = DecodeEvent(EncodeEvent(Event{e}));
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<AcceptedEvent>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->status, OrderStatus::Filled);
  EXPECT_EQ(got->quantity, 10u);
}

TEST(Protocol, RoundTripsBookUpdateEvent) {
  BookUpdateEvent e{1, 2, 3, Side::Buy, 1000, 0};
  auto decoded = DecodeEvent(EncodeEvent(Event{e}));
  ASSERT_TRUE(decoded.has_value());
  auto* got = std::get_if<BookUpdateEvent>(&*decoded);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->aggregate_quantity, 0u);  // level removed
}
