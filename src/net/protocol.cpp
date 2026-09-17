#include "matching_engine/net/protocol.hpp"

#include "matching_engine/net/binary_codec.hpp"

namespace me::net {

namespace {

std::optional<Command> DecodeNewOrder(BufferReader& r) {
  NewOrderCommand c;
  std::uint64_t order_id, trader_id, quantity, timestamp;
  std::uint32_t symbol_id;
  std::uint8_t side, type, tif;
  std::int64_t price;
  if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadU32(symbol_id) ||
      !r.ReadU8(side) || !r.ReadU8(type) || !r.ReadU8(tif) || !r.ReadI64(price) ||
      !r.ReadU64(quantity) || !r.ReadU64(timestamp)) {
    return std::nullopt;
  }
  c.order_id = order_id;
  c.trader_id = trader_id;
  c.symbol_id = symbol_id;
  c.side = static_cast<Side>(side);
  c.type = static_cast<OrderType>(type);
  c.time_in_force = static_cast<TimeInForce>(tif);
  c.price = price;
  c.quantity = quantity;
  c.timestamp = timestamp;
  return Command{c};
}

std::optional<Command> DecodeCancel(BufferReader& r) {
  CancelCommand c;
  std::uint64_t order_id, trader_id, timestamp;
  std::uint32_t symbol_id;
  if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadU32(symbol_id) ||
      !r.ReadU64(timestamp)) {
    return std::nullopt;
  }
  c.order_id = order_id;
  c.trader_id = trader_id;
  c.symbol_id = symbol_id;
  c.timestamp = timestamp;
  return Command{c};
}

std::optional<Command> DecodeModify(BufferReader& r) {
  ModifyCommand c;
  std::uint64_t order_id, trader_id, new_quantity, timestamp;
  std::uint32_t symbol_id;
  std::int64_t new_price;
  if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadU32(symbol_id) ||
      !r.ReadI64(new_price) || !r.ReadU64(new_quantity) || !r.ReadU64(timestamp)) {
    return std::nullopt;
  }
  c.order_id = order_id;
  c.trader_id = trader_id;
  c.symbol_id = symbol_id;
  c.new_price = new_price;
  c.new_quantity = new_quantity;
  c.timestamp = timestamp;
  return Command{c};
}

}  // namespace

std::optional<Command> DecodeCommand(const std::vector<std::uint8_t>& body) {
  if (body.empty()) return std::nullopt;
  BufferReader r(body.data(), body.size());
  std::uint8_t type_byte = 0;
  if (!r.ReadU8(type_byte)) return std::nullopt;
  switch (static_cast<MessageType>(type_byte)) {
    case MessageType::NewOrder:
      return DecodeNewOrder(r);
    case MessageType::Cancel:
      return DecodeCancel(r);
    case MessageType::Modify:
      return DecodeModify(r);
    default:
      return std::nullopt;
  }
}

std::vector<std::uint8_t> EncodeCommand(const Command& cmd) {
  std::vector<std::uint8_t> body;
  BufferWriter w(body);
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, NewOrderCommand>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::NewOrder));
          w.WriteU64(c.order_id);
          w.WriteU64(c.trader_id);
          w.WriteU32(c.symbol_id);
          w.WriteU8(static_cast<std::uint8_t>(c.side));
          w.WriteU8(static_cast<std::uint8_t>(c.type));
          w.WriteU8(static_cast<std::uint8_t>(c.time_in_force));
          w.WriteI64(c.price);
          w.WriteU64(c.quantity);
          w.WriteU64(c.timestamp);
        } else if constexpr (std::is_same_v<T, CancelCommand>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Cancel));
          w.WriteU64(c.order_id);
          w.WriteU64(c.trader_id);
          w.WriteU32(c.symbol_id);
          w.WriteU64(c.timestamp);
        } else if constexpr (std::is_same_v<T, ModifyCommand>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Modify));
          w.WriteU64(c.order_id);
          w.WriteU64(c.trader_id);
          w.WriteU32(c.symbol_id);
          w.WriteI64(c.new_price);
          w.WriteU64(c.new_quantity);
          w.WriteU64(c.timestamp);
        }
      },
      cmd);
  return body;
}

std::vector<std::uint8_t> EncodeEvent(const Event& event) {
  std::vector<std::uint8_t> body;
  BufferWriter w(body);
  std::visit(
      [&](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, AcceptedEvent>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Accepted));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU64(e.order_id);
          w.WriteU64(e.trader_id);
          w.WriteU8(static_cast<std::uint8_t>(e.side));
          w.WriteU8(static_cast<std::uint8_t>(e.type));
          w.WriteI64(e.price);
          w.WriteU64(e.quantity);
          w.WriteU64(e.filled_quantity);
          w.WriteU64(e.remaining_quantity);
          w.WriteU8(static_cast<std::uint8_t>(e.status));
        } else if constexpr (std::is_same_v<T, RejectedEvent>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Rejected));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU64(e.order_id);
          w.WriteU64(e.trader_id);
          w.WriteU8(static_cast<std::uint8_t>(e.reason));
        } else if constexpr (std::is_same_v<T, CancelledEvent>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Cancelled));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU64(e.order_id);
          w.WriteU64(e.trader_id);
        } else if constexpr (std::is_same_v<T, ModifiedEvent>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::Modified));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU64(e.order_id);
          w.WriteU64(e.trader_id);
          w.WriteI64(e.new_price);
          w.WriteU64(e.new_remaining_quantity);
          w.WriteU8(e.lost_priority ? 1 : 0);
        } else if constexpr (std::is_same_v<T, Trade>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::TradeMsg));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU64(e.aggressor_order_id);
          w.WriteU64(e.resting_order_id);
          w.WriteU64(e.aggressor_trader_id);
          w.WriteU64(e.resting_trader_id);
          w.WriteU8(static_cast<std::uint8_t>(e.aggressor_side));
          w.WriteI64(e.price);
          w.WriteU64(e.quantity);
        } else if constexpr (std::is_same_v<T, BookUpdateEvent>) {
          w.WriteU8(static_cast<std::uint8_t>(MessageType::BookUpdateMsg));
          w.WriteU64(e.sequence_number);
          w.WriteU64(e.timestamp);
          w.WriteU32(e.symbol_id);
          w.WriteU8(static_cast<std::uint8_t>(e.side));
          w.WriteI64(e.price);
          w.WriteU64(e.aggregate_quantity);
        }
      },
      event);
  return body;
}

std::optional<Event> DecodeEvent(const std::vector<std::uint8_t>& body) {
  if (body.empty()) return std::nullopt;
  BufferReader r(body.data(), body.size());
  std::uint8_t type_byte = 0;
  if (!r.ReadU8(type_byte)) return std::nullopt;

  std::uint64_t seq, ts;
  std::uint32_t symbol;
  if (!r.ReadU64(seq) || !r.ReadU64(ts) || !r.ReadU32(symbol)) return std::nullopt;

  switch (static_cast<MessageType>(type_byte)) {
    case MessageType::Accepted: {
      AcceptedEvent e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint64_t order_id, trader_id, quantity, filled, remaining;
      std::uint8_t side, type, status;
      std::int64_t price;
      if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadU8(side) || !r.ReadU8(type) ||
          !r.ReadI64(price) || !r.ReadU64(quantity) || !r.ReadU64(filled) ||
          !r.ReadU64(remaining) || !r.ReadU8(status)) {
        return std::nullopt;
      }
      e.order_id = order_id;
      e.trader_id = trader_id;
      e.side = static_cast<Side>(side);
      e.type = static_cast<OrderType>(type);
      e.price = price;
      e.quantity = quantity;
      e.filled_quantity = filled;
      e.remaining_quantity = remaining;
      e.status = static_cast<OrderStatus>(status);
      return Event{e};
    }
    case MessageType::Rejected: {
      RejectedEvent e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint64_t order_id, trader_id;
      std::uint8_t reason;
      if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadU8(reason)) return std::nullopt;
      e.order_id = order_id;
      e.trader_id = trader_id;
      e.reason = static_cast<RejectReason>(reason);
      return Event{e};
    }
    case MessageType::Cancelled: {
      CancelledEvent e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint64_t order_id, trader_id;
      if (!r.ReadU64(order_id) || !r.ReadU64(trader_id)) return std::nullopt;
      e.order_id = order_id;
      e.trader_id = trader_id;
      return Event{e};
    }
    case MessageType::Modified: {
      ModifiedEvent e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint64_t order_id, trader_id, new_remaining;
      std::int64_t new_price;
      std::uint8_t lost_priority;
      if (!r.ReadU64(order_id) || !r.ReadU64(trader_id) || !r.ReadI64(new_price) ||
          !r.ReadU64(new_remaining) || !r.ReadU8(lost_priority)) {
        return std::nullopt;
      }
      e.order_id = order_id;
      e.trader_id = trader_id;
      e.new_price = new_price;
      e.new_remaining_quantity = new_remaining;
      e.lost_priority = lost_priority != 0;
      return Event{e};
    }
    case MessageType::TradeMsg: {
      Trade e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint64_t aggressor_id, resting_id, aggressor_trader, resting_trader, quantity;
      std::uint8_t side;
      std::int64_t price;
      if (!r.ReadU64(aggressor_id) || !r.ReadU64(resting_id) || !r.ReadU64(aggressor_trader) ||
          !r.ReadU64(resting_trader) || !r.ReadU8(side) || !r.ReadI64(price) ||
          !r.ReadU64(quantity)) {
        return std::nullopt;
      }
      e.aggressor_order_id = aggressor_id;
      e.resting_order_id = resting_id;
      e.aggressor_trader_id = aggressor_trader;
      e.resting_trader_id = resting_trader;
      e.aggressor_side = static_cast<Side>(side);
      e.price = price;
      e.quantity = quantity;
      return Event{e};
    }
    case MessageType::BookUpdateMsg: {
      BookUpdateEvent e;
      e.sequence_number = seq;
      e.timestamp = ts;
      e.symbol_id = symbol;
      std::uint8_t side;
      std::int64_t price;
      std::uint64_t aggregate;
      if (!r.ReadU8(side) || !r.ReadI64(price) || !r.ReadU64(aggregate)) return std::nullopt;
      e.side = static_cast<Side>(side);
      e.price = price;
      e.aggregate_quantity = aggregate;
      return Event{e};
    }
    default:
      return std::nullopt;
  }
}

}  // namespace me::net
