#pragma once

#include <variant>

#include "matching_engine/types.hpp"

namespace me {

struct NewOrderCommand {
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  SymbolId symbol_id = 0;
  Side side = Side::Buy;
  OrderType type = OrderType::Limit;
  Price price = 0;
  Quantity quantity = 0;
  TimeInForce time_in_force = TimeInForce::GoodTillCancel;
  Timestamp timestamp = 0;
};

struct CancelCommand {
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  SymbolId symbol_id = 0;
  Timestamp timestamp = 0;
};

struct ModifyCommand {
  OrderId order_id = kInvalidOrderId;
  TraderId trader_id = 0;
  SymbolId symbol_id = 0;
  Price new_price = 0;
  Quantity new_quantity = 0;
  Timestamp timestamp = 0;
};

using Command = std::variant<NewOrderCommand, CancelCommand, ModifyCommand>;

// Extracts the routing key common to every command variant.
inline SymbolId SymbolOf(const Command& cmd) {
  return std::visit([](const auto& c) { return c.symbol_id; }, cmd);
}

}  // namespace me
