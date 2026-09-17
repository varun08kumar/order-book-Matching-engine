#pragma once

#include <variant>

#include "matching_engine/trade.hpp"
#include "matching_engine/types.hpp"

namespace me {

// Emitted once a new order has been accepted into the engine (it may already
// carry fills — see `filled_quantity` / `remaining_quantity`).
struct AcceptedEvent {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Quantity quantity = 0;
  Quantity filled_quantity = 0;
  Quantity remaining_quantity = 0;
  OrderStatus status = OrderStatus::New;
};

struct RejectedEvent {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  RejectReason reason = RejectReason::None;
};

struct CancelledEvent {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
};

struct ModifiedEvent {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  Price new_price = 0;
  Quantity new_remaining_quantity = 0;
  bool lost_priority = false;
};

// Aggregate quantity at one price level after a mutation. `aggregate_quantity
// == 0` means the level was removed entirely.
struct BookUpdateEvent {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  Side side = Side::Buy;
  Price price = 0;
  Quantity aggregate_quantity = 0;
};

using Event = std::variant<AcceptedEvent, RejectedEvent, CancelledEvent, ModifiedEvent, Trade,
                            BookUpdateEvent>;

}  // namespace me
