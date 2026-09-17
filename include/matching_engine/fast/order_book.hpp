#pragma once

#include <cstddef>
#include <map>
#include <unordered_map>
#include <vector>

#include "matching_engine/object_pool.hpp"
#include "matching_engine/order.hpp"
#include "matching_engine/trade.hpp"
#include "matching_engine/types.hpp"

namespace me::fast {

// Latency-optimized order book: same price-time-priority semantics and
// public surface as naive::OrderBook, but orders are drawn from a
// preallocated ObjectPool (Phase 4) and FIFO queues at each price level are
// intrusive doubly-linked lists threaded through the pooled nodes — so
// adding, filling, and cancelling an order touches no heap allocator at
// all. Only price *levels* themselves (map nodes, one per distinct price
// currently resting) still come from the default allocator; the number of
// distinct price levels is orders of magnitude smaller than the number of
// orders in any realistic workload, so this is where the naive and fast
// books deliberately still agree — see docs/performance.md.
class OrderBook {
 public:
  // `max_orders` sizes the preallocated order pool. Once it is exhausted,
  // AddOrder rejects the new order with RejectReason::PoolExhausted instead
  // of falling back to the heap — the whole point of pooling is to make the
  // hot path allocation-free, so growing on demand is not an option.
  OrderBook(SymbolId symbol_id, std::size_t max_orders)
      : symbol_id_(symbol_id), pool_(max_orders) {}

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

  AddResult AddOrder(OrderId id, TraderId trader_id, Side side, OrderType type, Price price,
                      Quantity quantity, TimeInForce tif, SequenceNumber seq, Timestamp ts);
  CancelResult CancelOrder(OrderId id);
  ModifyResult ModifyOrder(OrderId id, Price new_price, Quantity new_quantity, SequenceNumber seq,
                            Timestamp ts);

  [[nodiscard]] bool HasOrder(OrderId id) const { return index_.find(id) != index_.end(); }
  [[nodiscard]] bool HasBestBid() const { return !bids_.empty(); }
  [[nodiscard]] bool HasBestAsk() const { return !asks_.empty(); }
  [[nodiscard]] Price BestBid() const { return bids_.empty() ? kInvalidPrice : bids_.begin()->first; }
  [[nodiscard]] Price BestAsk() const { return asks_.empty() ? kInvalidPrice : asks_.begin()->first; }
  [[nodiscard]] Quantity QuantityAt(Side side, Price price) const;
  [[nodiscard]] std::size_t OrderCountAt(Side side, Price price) const;
  [[nodiscard]] std::size_t TotalOrderCount() const { return index_.size(); }
  [[nodiscard]] std::size_t PoolAvailable() const { return pool_.available(); }
  [[nodiscard]] SymbolId symbol_id() const { return symbol_id_; }

 private:
  struct Node {
    Order order;
    Node* prev = nullptr;
    Node* next = nullptr;
  };

  struct PriceLevel {
    Price price = 0;
    Quantity total_quantity = 0;
    std::size_t count = 0;
    Node* head = nullptr;  // oldest (first to match)
    Node* tail = nullptr;  // newest (back of FIFO)

    void PushBack(Node* n) {
      n->prev = tail;
      n->next = nullptr;
      if (tail != nullptr) tail->next = n; else head = n;
      tail = n;
      ++count;
    }
    void Unlink(Node* n) {
      if (n->prev != nullptr) n->prev->next = n->next; else head = n->next;
      if (n->next != nullptr) n->next->prev = n->prev; else tail = n->prev;
      n->prev = n->next = nullptr;
      --count;
    }
  };

  std::map<Price, PriceLevel, std::greater<>> bids_;
  std::map<Price, PriceLevel, std::less<>> asks_;
  std::unordered_map<OrderId, Node*> index_;
  SymbolId symbol_id_;
  ObjectPool<Node> pool_;

  void Match(OrderId id, TraderId trader_id, Side side, OrderType type, Price limit_price,
             Quantity& remaining, SequenceNumber seq, Timestamp ts, std::vector<Trade>& trades);
  [[nodiscard]] Quantity AvailableCrossingQuantity(Side side, OrderType type, Price limit_price) const;
  Node* InsertResting(const Order& order);
  void RemoveFromBook(Node* node, Side side, Price price, OrderId id);
};

}  // namespace me::fast
