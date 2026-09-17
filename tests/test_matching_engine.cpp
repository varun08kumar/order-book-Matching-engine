#include "matching_engine/matching_engine.hpp"

#include <gtest/gtest.h>

#include "matching_engine/partitioned_engine.hpp"
#include "matching_engine/symbol_router.hpp"

using namespace me;

namespace {

template <typename T>
const T* FindEvent(const std::vector<Event>& events) {
  for (const auto& e : events) {
    if (auto* p = std::get_if<T>(&e)) return p;
  }
  return nullptr;
}

template <typename T>
std::size_t CountEvent(const std::vector<Event>& events) {
  std::size_t n = 0;
  for (const auto& e : events)
    if (std::holds_alternative<T>(e)) ++n;
  return n;
}

}  // namespace

TEST(SymbolRouter, DefaultHashIsDeterministic) {
  SymbolRouter r(4);
  EXPECT_EQ(r.PartitionFor(10), r.PartitionFor(10));
  EXPECT_LT(r.PartitionFor(10), 4u);
}

TEST(SymbolRouter, PinOverridesDefaultHash) {
  SymbolRouter r(4);
  r.Pin(7, 2);
  EXPECT_EQ(r.PartitionFor(7), 2u);
}

TEST(MatchingEngine, RejectsCommandForUnregisteredSymbol) {
  MatchingEngine engine(0);
  std::vector<Event> events;
  NewOrderCommand cmd{1, 100, 42, Side::Buy, OrderType::Limit, 1000, 5,
                       TimeInForce::GoodTillCancel, 1};
  engine.Process(cmd, events);
  ASSERT_EQ(events.size(), 1u);
  auto* rej = std::get_if<RejectedEvent>(&events[0]);
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, RejectReason::InvalidSymbol);
}

TEST(MatchingEngine, NewOrderProducesAcceptedAndBookUpdate) {
  MatchingEngine engine(0);
  engine.RegisterSymbol(42);
  std::vector<Event> events;
  NewOrderCommand cmd{1, 100, 42, Side::Buy, OrderType::Limit, 1000, 5,
                       TimeInForce::GoodTillCancel, 1};
  engine.Process(cmd, events);

  auto* acc = FindEvent<AcceptedEvent>(events);
  ASSERT_NE(acc, nullptr);
  EXPECT_EQ(acc->order_id, 1u);
  EXPECT_EQ(acc->status, OrderStatus::New);

  auto* bu = FindEvent<BookUpdateEvent>(events);
  ASSERT_NE(bu, nullptr);
  EXPECT_EQ(bu->price, 1000);
  EXPECT_EQ(bu->aggregate_quantity, 5u);
}

TEST(MatchingEngine, MatchingProducesTradeEvents) {
  MatchingEngine engine(0);
  engine.RegisterSymbol(42);
  std::vector<Event> events;

  engine.Process(NewOrderCommand{1, 100, 42, Side::Sell, OrderType::Limit, 1000, 5,
                                  TimeInForce::GoodTillCancel, 1},
                  events);
  events.clear();
  engine.Process(NewOrderCommand{2, 200, 42, Side::Buy, OrderType::Limit, 1000, 5,
                                  TimeInForce::GoodTillCancel, 2},
                  events);

  EXPECT_EQ(CountEvent<Trade>(events), 1u);
  auto* t = FindEvent<Trade>(events);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->price, 1000);
  EXPECT_EQ(t->quantity, 5u);
}

TEST(MatchingEngine, CancelProducesCancelledAndBookUpdate) {
  MatchingEngine engine(0);
  engine.RegisterSymbol(42);
  std::vector<Event> events;
  engine.Process(NewOrderCommand{1, 100, 42, Side::Buy, OrderType::Limit, 1000, 5,
                                  TimeInForce::GoodTillCancel, 1},
                  events);
  events.clear();
  engine.Process(CancelCommand{1, 100, 42, 2}, events);

  auto* c = FindEvent<CancelledEvent>(events);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->order_id, 1u);
  auto* bu = FindEvent<BookUpdateEvent>(events);
  ASSERT_NE(bu, nullptr);
  EXPECT_EQ(bu->aggregate_quantity, 0u);  // level fully removed
}

TEST(MatchingEngine, CancelUnknownOrderIsRejected) {
  MatchingEngine engine(0);
  engine.RegisterSymbol(42);
  std::vector<Event> events;
  engine.Process(CancelCommand{999, 1, 42, 1}, events);
  auto* rej = FindEvent<RejectedEvent>(events);
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->reason, RejectReason::UnknownOrder);
}

TEST(MatchingEngine, SequenceNumbersAreMonotonicAcrossCommands) {
  MatchingEngine engine(0);
  engine.RegisterSymbol(42);
  std::vector<Event> events;
  SequenceNumber s1 = engine.Process(
      NewOrderCommand{1, 1, 42, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 1},
      events);
  SequenceNumber s2 = engine.Process(
      NewOrderCommand{2, 1, 42, Side::Buy, OrderType::Limit, 1000, 5, TimeInForce::GoodTillCancel, 2},
      events);
  SequenceNumber s3 = engine.Process(CancelCommand{1, 1, 42, 3}, events);
  EXPECT_LT(s1, s2);
  EXPECT_LT(s2, s3);
}

TEST(PartitionedEngine, RoutesToCorrectPartitionAndIsolatesSymbols) {
  PartitionedEngine pe(4);
  pe.RegisterSymbolOnPartition(1, 0);
  pe.RegisterSymbolOnPartition(2, 1);

  std::vector<Event> events;
  pe.Dispatch(NewOrderCommand{1, 1, 1, Side::Buy, OrderType::Limit, 1000, 5,
                               TimeInForce::GoodTillCancel, 1},
              events);
  pe.Dispatch(NewOrderCommand{2, 1, 2, Side::Sell, OrderType::Limit, 2000, 3,
                               TimeInForce::GoodTillCancel, 2},
              events);

  const auto* book1 = pe.Partition(0).FindBook(1);
  const auto* book2 = pe.Partition(1).FindBook(2);
  ASSERT_NE(book1, nullptr);
  ASSERT_NE(book2, nullptr);
  EXPECT_EQ(book1->BestBid(), 1000);
  EXPECT_EQ(book2->BestAsk(), 2000);
  EXPECT_EQ(pe.Partition(0).FindBook(2), nullptr);
}
