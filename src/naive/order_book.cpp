#include "matching_engine/naive/order_book.hpp"

#include <algorithm>

namespace me::naive {

const Order* OrderBook::FindOrder(OrderId id) const {
  auto it = index_.find(id);
  if (it == index_.end()) return nullptr;
  return &(*it->second.it);
}

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
    return it == bids_.end() ? 0 : it->second.orders.size();
  }
  auto it = asks_.find(price);
  return it == asks_.end() ? 0 : it->second.orders.size();
}

std::vector<std::pair<Price, Quantity>> OrderBook::BidLevels(std::size_t depth) const {
  std::vector<std::pair<Price, Quantity>> out;
  out.reserve(depth == 0 ? bids_.size() : depth);
  for (const auto& [price, level] : bids_) {
    out.emplace_back(price, level.total_quantity);
    if (depth != 0 && out.size() >= depth) break;
  }
  return out;
}

std::vector<std::pair<Price, Quantity>> OrderBook::AskLevels(std::size_t depth) const {
  std::vector<std::pair<Price, Quantity>> out;
  out.reserve(depth == 0 ? asks_.size() : depth);
  for (const auto& [price, level] : asks_) {
    out.emplace_back(price, level.total_quantity);
    if (depth != 0 && out.size() >= depth) break;
  }
  return out;
}

std::vector<Order> OrderBook::AllRestingOrders() const {
  std::vector<Order> out;
  out.reserve(index_.size());
  for (const auto& [price, level] : bids_) {
    for (const Order& o : level.orders) out.push_back(o);
  }
  for (const auto& [price, level] : asks_) {
    for (const Order& o : level.orders) out.push_back(o);
  }
  return out;
}

void OrderBook::RestoreOrder(const Order& order) {
  Order copy = order;
  InsertResting(std::move(copy));
}

namespace {
bool PriceCrosses(Side aggressor_side, Price limit_price, Price level_price, bool is_market) {
  if (is_market) return true;
  if (aggressor_side == Side::Buy) {
    // Aggressor is buying: crosses resting asks priced at or below its limit.
    return level_price <= limit_price;
  }
  // Aggressor is selling: crosses resting bids priced at or above its limit.
  return level_price >= limit_price;
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
                       Price limit_price, Quantity& remaining, SequenceNumber seq,
                       Timestamp ts, std::vector<Trade>& trades) {
  const bool is_market = (type == OrderType::Market);

  if (side == Side::Buy) {
    auto level_it = asks_.begin();
    while (remaining > 0 && level_it != asks_.end()) {
      if (!PriceCrosses(side, limit_price, level_it->first, is_market)) break;
      PriceLevel& level = level_it->second;
      auto order_it = level.orders.begin();
      while (remaining > 0 && order_it != level.orders.end()) {
        Order& resting = *order_it;
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
        trade.price = resting.price;  // trades execute at the resting price
        trade.quantity = trade_qty;
        trades.push_back(trade);

        remaining -= trade_qty;
        resting.remaining_quantity -= trade_qty;
        level.total_quantity -= trade_qty;

        if (resting.remaining_quantity == 0) {
          resting.status = OrderStatus::Filled;
          index_.erase(resting.id);
          order_it = level.orders.erase(order_it);
        } else {
          resting.status = OrderStatus::PartiallyFilled;
          ++order_it;
        }
      }
      if (level.orders.empty()) {
        level_it = asks_.erase(level_it);
      } else {
        ++level_it;
      }
    }
  } else {
    auto level_it = bids_.begin();
    while (remaining > 0 && level_it != bids_.end()) {
      if (!PriceCrosses(side, limit_price, level_it->first, is_market)) break;
      PriceLevel& level = level_it->second;
      auto order_it = level.orders.begin();
      while (remaining > 0 && order_it != level.orders.end()) {
        Order& resting = *order_it;
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
          index_.erase(resting.id);
          order_it = level.orders.erase(order_it);
        } else {
          resting.status = OrderStatus::PartiallyFilled;
          ++order_it;
        }
      }
      if (level.orders.empty()) {
        level_it = bids_.erase(level_it);
      } else {
        ++level_it;
      }
    }
  }
}

void OrderBook::InsertResting(Order&& order) {
  const OrderId id = order.id;
  const Side side = order.side;
  const Price price = order.price;
  if (side == Side::Buy) {
    PriceLevel& level = bids_[price];
    level.price = price;
    level.total_quantity += order.remaining_quantity;
    level.orders.push_back(std::move(order));
    index_[id] = Locator{side, price, std::prev(level.orders.end())};
  } else {
    PriceLevel& level = asks_[price];
    level.price = price;
    level.total_quantity += order.remaining_quantity;
    level.orders.push_back(std::move(order));
    index_[id] = Locator{side, price, std::prev(level.orders.end())};
  }
}

void OrderBook::RemoveFromBook(const Locator& loc, OrderId id) {
  if (loc.side == Side::Buy) {
    auto level_it = bids_.find(loc.price);
    if (level_it == bids_.end()) return;
    level_it->second.total_quantity -= loc.it->remaining_quantity;
    level_it->second.orders.erase(loc.it);
    if (level_it->second.orders.empty()) bids_.erase(level_it);
  } else {
    auto level_it = asks_.find(loc.price);
    if (level_it == asks_.end()) return;
    level_it->second.total_quantity -= loc.it->remaining_quantity;
    level_it->second.orders.erase(loc.it);
    if (level_it->second.orders.empty()) asks_.erase(level_it);
  }
  index_.erase(id);
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
    Quantity available = AvailableCrossingQuantity(side, type, price);
    if (available < quantity) {
      result.reject_reason = RejectReason::FillOrKillNotFilled;
      return result;
    }
  }

  Quantity remaining = quantity;
  Match(id, trader_id, side, type, price, remaining, seq, ts, result.trades);

  Quantity filled = quantity - remaining;
  result.accepted = true;
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
    result.final_status = order.status;
    result.remaining_quantity = remaining;
    InsertResting(std::move(order));
  } else {
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
  const Order& order = *it->second.it;
  result.symbol_id = order.symbol_id;
  result.trader_id = order.trader_id;
  Locator loc = it->second;
  RemoveFromBook(loc, id);
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

  Order snapshot = *it->second.it;
  const bool price_unchanged = (new_price == snapshot.price);
  const bool qty_decrease_only = (new_quantity <= snapshot.remaining_quantity);

  if (price_unchanged && qty_decrease_only) {
    // In-place amend: keeps time priority.
    Locator loc = it->second;
    Quantity delta = snapshot.remaining_quantity - new_quantity;
    loc.it->remaining_quantity = new_quantity;
    loc.it->quantity = snapshot.quantity - delta;
    if (loc.side == Side::Buy) {
      bids_[loc.price].total_quantity -= delta;
    } else {
      asks_[loc.price].total_quantity -= delta;
    }
    result.success = true;
    result.lost_priority = false;
    result.remaining_quantity = new_quantity;
    result.final_status = new_quantity == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    return result;
  }

  // Cancel/replace: loses time priority, may re-cross the book.
  Locator loc = it->second;
  RemoveFromBook(loc, id);

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

}  // namespace me::naive
