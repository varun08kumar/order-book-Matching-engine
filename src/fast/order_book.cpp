#include "matching_engine/fast/order_book.hpp"

#include <algorithm>

namespace me::fast {

Quantity OrderBook::QuantityAt(Side side, Price price) const {
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    return it == bids_.end() ? 0 : it->second.total_quantity;
  }
  auto it = asks_.find(price);
  return it == asks_.end() ? 0 : it->second.total_quantity;
}

std::size_t OrderBook::OrderCountAt(Side side, Price price) const {
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    return it == bids_.end() ? 0 : it->second.count;
  }
  auto it = asks_.find(price);
  return it == asks_.end() ? 0 : it->second.count;
}

namespace {
bool PriceCrosses(Side aggressor_side, Price limit_price, Price level_price, bool is_market) {
  if (is_market) return true;
  return aggressor_side == Side::Buy ? level_price <= limit_price : level_price >= limit_price;
}
}  // namespace

Quantity OrderBook::AvailableCrossingQuantity(Side side, OrderType type, Price limit_price) const {
  Quantity total = 0;
  const bool is_market = (type == OrderType::Market);
  if (side == Side::Buy) {
    for (const auto& [price, level] : asks_) {
      if (!PriceCrosses(side, limit_price, price, is_market)) break;
      total += level.total_quantity;
    }
  } else {
    for (const auto& [price, level] : bids_) {
      if (!PriceCrosses(side, limit_price, price, is_market)) break;
      total += level.total_quantity;
    }
  }
  return total;
}

void OrderBook::Match(OrderId id, TraderId trader_id, Side side, OrderType type,
                       Price limit_price, Quantity& remaining, SequenceNumber seq, Timestamp ts,
                       std::vector<Trade>& trades) {
  const bool is_market = (type == OrderType::Market);

  auto match_against = [&](auto& levels_map) {
    auto level_it = levels_map.begin();
    while (remaining > 0 && level_it != levels_map.end()) {
      if (!PriceCrosses(side, limit_price, level_it->first, is_market)) break;
      PriceLevel& level = level_it->second;
      Node* node = level.head;
      while (remaining > 0 && node != nullptr) {
        Node* next = node->next;
        Order& resting = node->order;
        Quantity trade_qty = std::min(remaining, resting.remaining_quantity);

        Trade trade;
        trade.sequence_number = seq;
        trade.timestamp = ts;
        trade.symbol_id = symbol_id_;
        trade.aggressor_order_id = id;
        trade.resting_order_id = resting.id;
        trade.aggressor_trader_id = trader_id;
        trade.resting_trader_id = resting.trader_id;
        trade.aggressor_side = side;
        trade.price = resting.price;
        trade.quantity = trade_qty;
        trades.push_back(trade);

        remaining -= trade_qty;
        resting.remaining_quantity -= trade_qty;
        level.total_quantity -= trade_qty;

        if (resting.remaining_quantity == 0) {
          resting.status = OrderStatus::Filled;
          level.Unlink(node);
          index_.erase(resting.id);
          pool_.Release(node);
        } else {
          resting.status = OrderStatus::PartiallyFilled;
        }
        node = next;
      }
      if (level.count == 0) {
        level_it = levels_map.erase(level_it);
      } else {
        ++level_it;
      }
    }
  };

  if (side == Side::Buy) {
    match_against(asks_);
  } else {
    match_against(bids_);
  }
}

OrderBook::Node* OrderBook::InsertResting(const Order& order) {
  Node* node = pool_.Acquire();
  if (node == nullptr) return nullptr;
  node->order = order;
  node->prev = node->next = nullptr;

  if (order.side == Side::Buy) {
    PriceLevel& level = bids_[order.price];
    level.price = order.price;
    level.total_quantity += order.remaining_quantity;
    level.PushBack(node);
  } else {
    PriceLevel& level = asks_[order.price];
    level.price = order.price;
    level.total_quantity += order.remaining_quantity;
    level.PushBack(node);
  }
  index_[order.id] = node;
  return node;
}

void OrderBook::RemoveFromBook(Node* node, Side side, Price price, OrderId id) {
  if (side == Side::Buy) {
    auto level_it = bids_.find(price);
    if (level_it == bids_.end()) return;
    level_it->second.total_quantity -= node->order.remaining_quantity;
    level_it->second.Unlink(node);
    if (level_it->second.count == 0) bids_.erase(level_it);
  } else {
    auto level_it = asks_.find(price);
    if (level_it == asks_.end()) return;
    level_it->second.total_quantity -= node->order.remaining_quantity;
    level_it->second.Unlink(node);
    if (level_it->second.count == 0) asks_.erase(level_it);
  }
  index_.erase(id);
  pool_.Release(node);
}

OrderBook::AddResult OrderBook::AddOrder(OrderId id, TraderId trader_id, Side side,
                                          OrderType type, Price price, Quantity quantity,
                                          TimeInForce tif, SequenceNumber seq, Timestamp ts) {
  AddResult result;

  if (id == kInvalidOrderId) {
    result.reject_reason = RejectReason::UnknownOrder;
    return result;
  }
  if (index_.find(id) != index_.end()) {
    result.reject_reason = RejectReason::DuplicateOrderId;
    return result;
  }
  if (quantity == 0) {
    result.reject_reason = RejectReason::InvalidQuantity;
    return result;
  }
  if (type == OrderType::Limit && price <= 0) {
    result.reject_reason = RejectReason::InvalidPrice;
    return result;
  }
  if (type == OrderType::Market) {
    const bool opposite_empty = (side == Side::Buy) ? asks_.empty() : bids_.empty();
    if (opposite_empty) {
      result.reject_reason = RejectReason::MarketOrderNoLiquidity;
      return result;
    }
  }
  if (tif == TimeInForce::FillOrKill) {
    if (AvailableCrossingQuantity(side, type, price) < quantity) {
      result.reject_reason = RejectReason::FillOrKillNotFilled;
      return result;
    }
  }

  Quantity remaining = quantity;
  Match(id, trader_id, side, type, price, remaining, seq, ts, result.trades);

  Quantity filled = quantity - remaining;
  result.filled_quantity = filled;

  const bool can_rest = (type == OrderType::Limit) && (tif == TimeInForce::GoodTillCancel) &&
                         (remaining > 0);

  if (can_rest) {
    Order order;
    order.id = id;
    order.trader_id = trader_id;
    order.symbol_id = symbol_id_;
    order.sequence_number = seq;
    order.timestamp = ts;
    order.price = price;
    order.quantity = quantity;
    order.remaining_quantity = remaining;
    order.side = side;
    order.type = type;
    order.time_in_force = tif;
    order.status = filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::New;

    if (InsertResting(order) == nullptr) {
      // Pool exhausted: whatever already matched stands (trades already
      // committed), but the remainder cannot rest.
      result.accepted = filled > 0;
      result.reject_reason = RejectReason::PoolExhausted;
      result.remaining_quantity = 0;
      result.final_status = filled > 0 ? OrderStatus::Filled : OrderStatus::Rejected;
      return result;
    }
    result.accepted = true;
    result.final_status = order.status;
    result.remaining_quantity = remaining;
  } else {
    result.accepted = true;
    result.remaining_quantity = 0;
    result.final_status = (filled == quantity) ? OrderStatus::Filled : OrderStatus::Cancelled;
  }

  return result;
}

OrderBook::CancelResult OrderBook::CancelOrder(OrderId id) {
  CancelResult result;
  auto it = index_.find(id);
  if (it == index_.end()) {
    result.reject_reason = RejectReason::UnknownOrder;
    return result;
  }
  Node* node = it->second;
  const Order& order = node->order;
  result.symbol_id = order.symbol_id;
  result.trader_id = order.trader_id;
  RemoveFromBook(node, order.side, order.price, id);
  result.success = true;
  return result;
}

OrderBook::ModifyResult OrderBook::ModifyOrder(OrderId id, Price new_price, Quantity new_quantity,
                                                SequenceNumber seq, Timestamp ts) {
  ModifyResult result;
  auto it = index_.find(id);
  if (it == index_.end()) {
    result.reject_reason = RejectReason::UnknownOrder;
    return result;
  }
  if (new_quantity == 0) {
    result.reject_reason = RejectReason::InvalidQuantity;
    return result;
  }
  if (new_price <= 0) {
    result.reject_reason = RejectReason::InvalidPrice;
    return result;
  }

  Node* node = it->second;
  Order snapshot = node->order;
  const bool price_unchanged = (new_price == snapshot.price);
  const bool qty_decrease_only = (new_quantity <= snapshot.remaining_quantity);

  if (price_unchanged && qty_decrease_only) {
    Quantity delta = snapshot.remaining_quantity - new_quantity;
    node->order.remaining_quantity = new_quantity;
    node->order.quantity = snapshot.quantity - delta;
    if (snapshot.side == Side::Buy) {
      bids_[snapshot.price].total_quantity -= delta;
    } else {
      asks_[snapshot.price].total_quantity -= delta;
    }
    result.success = true;
    result.lost_priority = false;
    result.remaining_quantity = new_quantity;
    result.final_status = new_quantity == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    return result;
  }

  RemoveFromBook(node, snapshot.side, snapshot.price, id);

  AddResult add = AddOrder(id, snapshot.trader_id, snapshot.side, snapshot.type, new_price,
                            new_quantity, snapshot.time_in_force, seq, ts);
  result.success = add.accepted;
  result.reject_reason = add.reject_reason;
  result.lost_priority = true;
  result.filled_quantity = add.filled_quantity;
  result.remaining_quantity = add.remaining_quantity;
  result.final_status = add.final_status;
  result.trades = std::move(add.trades);
  return result;
}

}  // namespace me::fast
