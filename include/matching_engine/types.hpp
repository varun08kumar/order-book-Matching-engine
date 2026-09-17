#pragma once

#include <cstdint>
#include <string_view>

namespace me {

// Fixed-point price: integer number of "ticks" (e.g. 1 tick = $0.0001).
// Never use floating point for prices — floating point rounding would make
// matching non-deterministic across platforms/compilers.
using Price = std::int64_t;
using OrderId = std::uint64_t;
using TraderId = std::uint64_t;
using SymbolId = std::uint32_t;
using Quantity = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t; // nanoseconds since epoch

inline constexpr Price kInvalidPrice = -1;
inline constexpr OrderId kInvalidOrderId = 0;

enum class Side : std::uint8_t {
  Buy = 0,
  Sell = 1,
};

constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

enum class OrderType : std::uint8_t {
  Limit = 0,
  Market = 1,
};

enum class TimeInForce : std::uint8_t {
  GoodTillCancel = 0,
  ImmediateOrCancel = 1,
  FillOrKill = 2,
};

enum class OrderStatus : std::uint8_t {
  New = 0,
  PartiallyFilled = 1,
  Filled = 2,
  Cancelled = 3,
  Rejected = 4,
};

enum class RejectReason : std::uint8_t {
  None = 0,
  InvalidSymbol = 1,
  InvalidPrice = 2,
  InvalidQuantity = 3,
  UnknownOrder = 4,
  DuplicateOrderId = 5,
  MarketOrderNoLiquidity = 6,
  FillOrKillNotFilled = 7,
  SelfMatchPrevented = 8,
  PoolExhausted = 9,
};

constexpr std::string_view to_string(Side s) noexcept {
  return s == Side::Buy ? "BUY" : "SELL";
}

constexpr std::string_view to_string(OrderType t) noexcept {
  return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

constexpr std::string_view to_string(OrderStatus s) noexcept {
  switch (s) {
    case OrderStatus::New: return "NEW";
    case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
    case OrderStatus::Filled: return "FILLED";
    case OrderStatus::Cancelled: return "CANCELLED";
    case OrderStatus::Rejected: return "REJECTED";
  }
  return "UNKNOWN";
}

constexpr std::string_view to_string(RejectReason r) noexcept {
  switch (r) {
    case RejectReason::None: return "NONE";
    case RejectReason::InvalidSymbol: return "INVALID_SYMBOL";
    case RejectReason::InvalidPrice: return "INVALID_PRICE";
    case RejectReason::InvalidQuantity: return "INVALID_QUANTITY";
    case RejectReason::UnknownOrder: return "UNKNOWN_ORDER";
    case RejectReason::DuplicateOrderId: return "DUPLICATE_ORDER_ID";
    case RejectReason::MarketOrderNoLiquidity: return "MARKET_ORDER_NO_LIQUIDITY";
    case RejectReason::FillOrKillNotFilled: return "FILL_OR_KILL_NOT_FILLED";
    case RejectReason::SelfMatchPrevented: return "SELF_MATCH_PREVENTED";
    case RejectReason::PoolExhausted: return "POOL_EXHAUSTED";
  }
  return "UNKNOWN";
}

}  // namespace me
