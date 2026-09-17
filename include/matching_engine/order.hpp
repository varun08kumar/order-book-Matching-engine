#pragma once

#include <type_traits>

#include "matching_engine/types.hpp"

namespace me {

// Plain-old-data order record. Deliberately trivially copyable so it can be
// placed in a preallocated pool (Phase 4) and serialized byte-for-byte into
// the journal / snapshots (Phase 8) without a custom (de)serializer.
struct Order {
  OrderId id = kInvalidOrderId;
  TraderId trader_id = 0;
  SymbolId symbol_id = 0;
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  Price price = 0;               // limit price; unused (0) for market orders
  Quantity quantity = 0;         // original quantity
  Quantity remaining_quantity = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  TimeInForce time_in_force = TimeInForce::GoodTillCancel;
  OrderStatus status = OrderStatus::New;
};
static_assert(std::is_trivially_copyable_v<Order>);

}  // namespace me
