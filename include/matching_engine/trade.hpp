#pragma once

#include <type_traits>

#include "matching_engine/types.hpp"

namespace me {

// A single execution. Always priced at the RESTING order's price, per
// price-time-priority matching rules (the aggressor takes the resting side's
// price, never its own).
struct Trade {
  SequenceNumber sequence_number = 0;
  Timestamp timestamp = 0;
  SymbolId symbol_id = 0;
  OrderId aggressor_order_id = kInvalidOrderId;
  OrderId resting_order_id = kInvalidOrderId;
  TraderId aggressor_trader_id = 0;
  TraderId resting_trader_id = 0;
  Side aggressor_side = Side::Buy;
  Price price = 0;
  Quantity quantity = 0;
};
static_assert(std::is_trivially_copyable_v<Trade>);

}  // namespace me
