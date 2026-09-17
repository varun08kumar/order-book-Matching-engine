#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "matching_engine/order.hpp"
#include "matching_engine/trade.hpp"
#include "matching_engine/types.hpp"

namespace me::naive {

// Correctness-first order book: std::map price levels, std::list FIFO queues,
// std::unordered_map order-id index. No pooling, no lock-free structures.
// Used as the reference implementation that optimized (Phase 10) engines are
// checked against, and as the baseline in comparative benchmarks.
class OrderBook {
 public:
  explicit OrderBook(SymbolId symbol_id) : symbol_id_(symbol_id) {}

  struct AddResult {
    bool accepted = false;
    RejectReason reject_reason = RejectReason::None;
    OrderStatus final_status = OrderStatus::Rejected;
    Quantity filled_quantity = 0;
    Quantity remaining_quantity = 0;
    std::vector<Trade> trades;
  };

  struct CancelResult {
    bool success = false;
    RejectReason reject_reason = RejectReason::None;
    SymbolId symbol_id = 0;
    TraderId trader_id = 0;
  };

  struct ModifyResult {
    bool success = false;
    RejectReason reject_reason = RejectReason::None;
    bool lost_priority = false;
    OrderStatus final_status = OrderStatus::Rejected;
    Quantity filled_quantity = 0;
    Quantity remaining_quantity = 0;
    std::vector<Trade> trades;
  };

  // Adds a new order and runs it through the matching loop immediately.
  // `seq`/`ts` are assigned by the caller (matching engine) so that ordering
  // is deterministic and reproducible from the journal.
  AddResult AddOrder(OrderId id, TraderId trader_id, Side side, OrderType type,
                      Price price, Quantity quantity, TimeInForce tif,
                      SequenceNumber seq, Timestamp ts);

  CancelResult CancelOrder(OrderId id);

  // Cancel/replace semantics: a quantity-only decrease keeps time priority;
  // any price change or quantity increase loses priority (re-queued at the
  // back of the new price level, or matched immediately if now marketable).
  ModifyResult ModifyOrder(OrderId id, Price new_price, Quantity new_quantity,
                            SequenceNumber seq, Timestamp ts);

  [[nodiscard]] bool HasOrder(OrderId id) const {
    return index_.find(id) != index_.end();
  }
  [[nodiscard]] const Order* FindOrder(OrderId id) const;

  [[nodiscard]] bool HasBestBid() const { return !bids_.empty(); }
  [[nodiscard]] bool HasBestAsk() const { return !asks_.empty(); }
  [[nodiscard]] Price BestBid() const { return bids_.empty() ? kInvalidPrice : bids_.begin()->first; }
  [[nodiscard]] Price BestAsk() const { return asks_.empty() ? kInvalidPrice : asks_.begin()->first; }
  [[nodiscard]] Quantity QuantityAt(Side side, Price price) const;
  [[nodiscard]] std::size_t OrderCountAt(Side side, Price price) const;
  [[nodiscard]] std::size_t TotalOrderCount() const { return index_.size(); }
  [[nodiscard]] SymbolId symbol_id() const { return symbol_id_; }

  // Snapshot of top-of-book levels: {price, aggregate_quantity}. `depth==0`
  // returns all levels.
  [[nodiscard]] std::vector<std::pair<Price, Quantity>> BidLevels(std::size_t depth = 0) const;
  [[nodiscard]] std::vector<std::pair<Price, Quantity>> AskLevels(std::size_t depth = 0) const;

  // Every resting order, bids (best-to-worst price) then asks (best-to-worst
  // price), FIFO order preserved within each level. Used to serialize a
  // snapshot (Phase 8).
  [[nodiscard]] std::vector<Order> AllRestingOrders() const;

  // Re-inserts an order that was already resting (as recorded by a
  // snapshot) directly at the back of its price level's FIFO queue, with no
  // matching performed. Callers must restore orders for a given level in
  // their original FIFO order for time priority to be reconstructed
  // correctly. Not for use on the live matching path.
  void RestoreOrder(const Order& order);

 private:
  using OrderList = std::list<Order>;

  struct PriceLevel {
    Price price = 0;
    Quantity total_quantity = 0;
    OrderList orders;
  };

  struct Locator {
    Side side;
    Price price;
    OrderList::iterator it;
  };

  // Bids are ordered highest-first (best bid = greatest price); asks are
  // ordered lowest-first (best ask = smallest price).
  std::map<Price, PriceLevel, std::greater<>> bids_;
  std::map<Price, PriceLevel, std::less<>> asks_;
  std::unordered_map<OrderId, Locator> index_;
  SymbolId symbol_id_;

  template <typename Compare>
  static bool Crosses(Side aggressor_side, OrderType type, Price limit_price, Price level_price);

  // Matches an incoming (not-yet-resting) order against the opposite side of
  // the book. Mutates `remaining` and appends generated trades.
  void Match(OrderId id, TraderId trader_id, Side side, OrderType type,
             Price limit_price, Quantity& remaining, SequenceNumber seq,
             Timestamp ts, std::vector<Trade>& trades);

  // Computes how much opposite-side quantity is available at prices that
  // cross `limit_price` (or all liquidity, for market orders), without
  // mutating the book. Used for FillOrKill pre-checks.
  [[nodiscard]] Quantity AvailableCrossingQuantity(Side side, OrderType type, Price limit_price) const;

  void InsertResting(Order&& order);
  void RemoveFromBook(const Locator& loc, OrderId id);
};

}  // namespace me::naive
