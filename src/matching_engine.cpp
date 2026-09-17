#include "matching_engine/matching_engine.hpp"

#include <utility>

namespace me {

namespace {
// Small helper: dedupes (side, price) pairs touched by an operation so we
// don't emit redundant BookUpdateEvents for the same level.
void AddTouched(std::vector<std::pair<Side, Price>>& touched, Side side, Price price) {
  for (const auto& [s, p] : touched) {
    if (s == side && p == price) return;
  }
  touched.emplace_back(side, price);
}
}  // namespace

naive::OrderBook* MatchingEngine::FindBookMutable(SymbolId symbol) {
  auto it = books_.find(symbol);
  return it == books_.end() ? nullptr : &it->second;
}

const naive::OrderBook* MatchingEngine::FindBook(SymbolId symbol) const {
  auto it = books_.find(symbol);
  return it == books_.end() ? nullptr : &it->second;
}

SequenceNumber MatchingEngine::Process(const Command& cmd, std::vector<Event>& out_events) {
  const SequenceNumber seq = next_seq_++;
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, NewOrderCommand>) {
          ProcessNewOrder(c, seq, out_events);
        } else if constexpr (std::is_same_v<T, CancelCommand>) {
          ProcessCancel(c, seq, out_events);
        } else if constexpr (std::is_same_v<T, ModifyCommand>) {
          ProcessModify(c, seq, out_events);
        }
      },
      cmd);
  return seq;
}

void MatchingEngine::ProcessNewOrder(const NewOrderCommand& c, SequenceNumber seq,
                                      std::vector<Event>& out) {
  naive::OrderBook* book = FindBookMutable(c.symbol_id);
  if (book == nullptr) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 RejectReason::InvalidSymbol});
    return;
  }

  auto result = book->AddOrder(c.order_id, c.trader_id, c.side, c.type, c.price, c.quantity,
                                c.time_in_force, seq, c.timestamp);
  if (!result.accepted) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 result.reject_reason});
    return;
  }

  out.push_back(AcceptedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id, c.side,
                               c.type, c.price, c.quantity, result.filled_quantity,
                               result.remaining_quantity, result.final_status});

  std::vector<std::pair<Side, Price>> touched;
  for (const Trade& t : result.trades) {
    AddTouched(touched, opposite(c.side), t.price);
    out.push_back(t);
  }
  const bool rests = result.remaining_quantity > 0 && result.final_status != OrderStatus::Cancelled;
  if (rests) AddTouched(touched, c.side, c.price);

  for (const auto& [side, price] : touched) {
    out.push_back(BookUpdateEvent{seq, c.timestamp, c.symbol_id, side, price,
                                   book->QuantityAt(side, price)});
  }
}

void MatchingEngine::ProcessCancel(const CancelCommand& c, SequenceNumber seq,
                                    std::vector<Event>& out) {
  naive::OrderBook* book = FindBookMutable(c.symbol_id);
  if (book == nullptr) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 RejectReason::InvalidSymbol});
    return;
  }

  const Order* existing = book->FindOrder(c.order_id);
  Side side = Side::Buy;
  Price price = 0;
  bool had_order = existing != nullptr;
  if (had_order) {
    side = existing->side;
    price = existing->price;
  }

  auto result = book->CancelOrder(c.order_id);
  if (!result.success) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 result.reject_reason});
    return;
  }

  out.push_back(CancelledEvent{seq, c.timestamp, c.symbol_id, c.order_id, result.trader_id});
  if (had_order) {
    out.push_back(BookUpdateEvent{seq, c.timestamp, c.symbol_id, side, price,
                                   book->QuantityAt(side, price)});
  }
}

void MatchingEngine::ProcessModify(const ModifyCommand& c, SequenceNumber seq,
                                    std::vector<Event>& out) {
  naive::OrderBook* book = FindBookMutable(c.symbol_id);
  if (book == nullptr) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 RejectReason::InvalidSymbol});
    return;
  }

  const Order* existing = book->FindOrder(c.order_id);
  if (existing == nullptr) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 RejectReason::UnknownOrder});
    return;
  }
  const Side side = existing->side;
  const Price old_price = existing->price;

  auto result = book->ModifyOrder(c.order_id, c.new_price, c.new_quantity, seq, c.timestamp);
  if (!result.success) {
    out.push_back(RejectedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id,
                                 result.reject_reason});
    return;
  }

  out.push_back(ModifiedEvent{seq, c.timestamp, c.symbol_id, c.order_id, c.trader_id, c.new_price,
                               result.remaining_quantity, result.lost_priority});

  std::vector<std::pair<Side, Price>> touched;
  for (const Trade& t : result.trades) {
    AddTouched(touched, opposite(side), t.price);
    out.push_back(t);
  }
  AddTouched(touched, side, old_price);
  if (c.new_price != old_price) AddTouched(touched, side, c.new_price);

  for (const auto& [tside, tprice] : touched) {
    out.push_back(BookUpdateEvent{seq, c.timestamp, c.symbol_id, tside, tprice,
                                   book->QuantityAt(tside, tprice)});
  }
}

}  // namespace me
