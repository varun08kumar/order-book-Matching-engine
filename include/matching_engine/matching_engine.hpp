#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "matching_engine/command.hpp"
#include "matching_engine/events.hpp"
#include "matching_engine/naive/order_book.hpp"
#include "matching_engine/types.hpp"

namespace me {

// Single-threaded matching engine owning one partition's worth of symbol
// order books. Must only ever be driven from one thread: it holds no
// internal locking, and none is needed as long as that invariant holds.
//
// Commands are processed strictly in the order they are handed to Process(),
// which is what makes replay from the journal deterministic: given the same
// command sequence, the same events (including sequence numbers) are always
// produced.
class MatchingEngine {
 public:
  explicit MatchingEngine(std::size_t partition_index) : partition_index_(partition_index) {}

  void RegisterSymbol(SymbolId symbol) { books_.try_emplace(symbol, symbol); }
  [[nodiscard]] bool OwnsSymbol(SymbolId symbol) const { return books_.find(symbol) != books_.end(); }

  // Processes one command, appending every generated event (accept/reject,
  // trades, cancellations, book updates) to `out_events` in causal order.
  // Returns the sequence number assigned to this command.
  SequenceNumber Process(const Command& cmd, std::vector<Event>& out_events);

  [[nodiscard]] const naive::OrderBook* FindBook(SymbolId symbol) const;
  [[nodiscard]] std::size_t partition_index() const { return partition_index_; }
  [[nodiscard]] SequenceNumber next_sequence_number() const { return next_seq_; }

  // For snapshot/recovery (Phase 8): direct access to owned books.
  [[nodiscard]] const std::unordered_map<SymbolId, naive::OrderBook>& books() const { return books_; }
  // Used by journal replay to keep engine-assigned sequence numbers in sync
  // with what was previously journaled.
  void SetNextSequenceNumber(SequenceNumber n) { next_seq_ = n; }

  // Restores one resting order from a snapshot directly into `symbol`'s
  // book, bypassing matching. `symbol` must already be registered on this
  // partition. Orders for a given price level must be restored in their
  // original FIFO order.
  void RestoreOrder(SymbolId symbol, const Order& order) {
    naive::OrderBook* book = FindBookMutable(symbol);
    if (book != nullptr) book->RestoreOrder(order);
  }

 private:
  std::size_t partition_index_;
  SequenceNumber next_seq_ = 1;
  std::unordered_map<SymbolId, naive::OrderBook> books_;

  naive::OrderBook* FindBookMutable(SymbolId symbol);

  void ProcessNewOrder(const NewOrderCommand& cmd, SequenceNumber seq, std::vector<Event>& out);
  void ProcessCancel(const CancelCommand& cmd, SequenceNumber seq, std::vector<Event>& out);
  void ProcessModify(const ModifyCommand& cmd, SequenceNumber seq, std::vector<Event>& out);
};

}  // namespace me
