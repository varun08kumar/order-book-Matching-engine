#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "matching_engine/persistence/journal.hpp"
#include "matching_engine/persistence/snapshot.hpp"

using namespace me;
using namespace me::persistence;

namespace {

class TempFile {
 public:
  explicit TempFile(std::string name)
      : path_((std::filesystem::temp_directory_path() / std::move(name)).string()) {
    std::filesystem::remove(path_);
  }
  ~TempFile() { std::filesystem::remove(path_); }
  [[nodiscard]] const std::string& path() const { return path_; }

 private:
  std::string path_;
};

Command MakeNewOrder(OrderId id, TraderId trader, SymbolId symbol, Side side, Price price,
                      Quantity qty, Timestamp ts) {
  NewOrderCommand c;
  c.order_id = id;
  c.trader_id = trader;
  c.symbol_id = symbol;
  c.side = side;
  c.type = OrderType::Limit;
  c.time_in_force = TimeInForce::GoodTillCancel;
  c.price = price;
  c.quantity = qty;
  c.timestamp = ts;
  return Command{c};
}

}  // namespace

TEST(Journal, ReadMissingFileReturnsEmpty) {
  auto commands = ReadJournal("/nonexistent/path/for/matching_engine_tests.journal");
  EXPECT_TRUE(commands.empty());
}

TEST(Journal, WriteThenReadRoundTripsCommandsInOrder) {
  TempFile f("me_test_journal_roundtrip.bin");
  {
    JournalWriter writer(f.path());
    writer.Append(MakeNewOrder(1, 1, 1, Side::Buy, 1000, 5, 1));
    writer.Append(MakeNewOrder(2, 2, 1, Side::Sell, 1000, 5, 2));
    writer.Append(Command{CancelCommand{1, 1, 1, 3}});
  }
  auto commands = ReadJournal(f.path());
  ASSERT_EQ(commands.size(), 3u);
  EXPECT_NE(std::get_if<NewOrderCommand>(&commands[0]), nullptr);
  EXPECT_NE(std::get_if<NewOrderCommand>(&commands[1]), nullptr);
  EXPECT_NE(std::get_if<CancelCommand>(&commands[2]), nullptr);
}

TEST(Journal, TornTrailingWriteIsDiscardedNotFatal) {
  TempFile f("me_test_journal_torn.bin");
  {
    JournalWriter writer(f.path());
    writer.Append(MakeNewOrder(1, 1, 1, Side::Buy, 1000, 5, 1));
  }
  // Simulate a crash mid-append: append a few extra bytes of a
  // never-completed second record (a length prefix promising more body
  // than actually follows).
  {
    std::ofstream out(f.path(), std::ios::binary | std::ios::app);
    std::uint8_t partial[6] = {50, 0, 0, 0, 0xAA, 0xBB};  // claims 50-byte body, only 2 bytes present
    out.write(reinterpret_cast<const char*>(partial), sizeof(partial));
  }
  auto commands = ReadJournal(f.path());
  ASSERT_EQ(commands.size(), 1u);  // only the complete first record survives
}

TEST(Journal, CorruptOversizedLengthThrows) {
  TempFile f("me_test_journal_corrupt.bin");
  {
    std::ofstream out(f.path(), std::ios::binary);
    std::uint32_t bogus = 10 * 1024 * 1024;  // exceeds kMaxFrameBytes
    std::uint8_t bytes[4];
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(bogus >> (8 * i));
    out.write(reinterpret_cast<const char*>(bytes), 4);
  }
  EXPECT_THROW(ReadJournal(f.path()), std::runtime_error);
}

TEST(Snapshot, RoundTripsRestingOrdersAndSequenceNumbers) {
  TempFile f("me_test_snapshot_roundtrip.bin");
  PartitionedEngine engine(2);
  engine.RegisterSymbolOnPartition(1, 0);
  engine.RegisterSymbolOnPartition(2, 1);

  std::vector<Event> events;
  engine.Dispatch(MakeNewOrder(1, 100, 1, Side::Buy, 990, 5, 1), events);
  engine.Dispatch(MakeNewOrder(2, 100, 1, Side::Buy, 1000, 3, 2), events);
  engine.Dispatch(MakeNewOrder(3, 200, 1, Side::Buy, 1000, 7, 3), events);  // same level, after #2
  engine.Dispatch(MakeNewOrder(4, 300, 2, Side::Sell, 2000, 4, 4), events);

  WriteSnapshot(f.path(), engine);
  PartitionedEngine restored = LoadSnapshot(f.path());

  ASSERT_EQ(restored.PartitionCount(), 2u);
  const auto* book1 = restored.Partition(0).FindBook(1);
  const auto* book2 = restored.Partition(1).FindBook(2);
  ASSERT_NE(book1, nullptr);
  ASSERT_NE(book2, nullptr);

  EXPECT_EQ(book1->BestBid(), 1000);
  EXPECT_EQ(book1->QuantityAt(Side::Buy, 1000), 10u);
  EXPECT_EQ(book1->QuantityAt(Side::Buy, 990), 5u);
  EXPECT_EQ(book2->BestAsk(), 2000);

  EXPECT_EQ(restored.Partition(0).next_sequence_number(), engine.Partition(0).next_sequence_number());
  EXPECT_EQ(restored.Partition(1).next_sequence_number(), engine.Partition(1).next_sequence_number());

  // Time priority at the 1000 level must be preserved: order 2 was resting
  // first, so a matching sell should trade against it first.
  std::vector<Event> restored_events;
  restored.Dispatch(MakeNewOrder(5, 999, 1, Side::Sell, 1000, 3, 5), restored_events);
  bool found_trade_against_order2 = false;
  for (const auto& e : restored_events) {
    if (auto* t = std::get_if<Trade>(&e)) {
      EXPECT_EQ(t->resting_order_id, 2u);
      found_trade_against_order2 = true;
    }
  }
  EXPECT_TRUE(found_trade_against_order2);
}

TEST(Recovery, SnapshotPlusJournalReplayReconstructsIdenticalState) {
  TempFile snap("me_test_recovery_snapshot.bin");
  TempFile pre_journal("me_test_recovery_journal_pre.bin");
  TempFile post_journal("me_test_recovery_journal_post.bin");

  PartitionedEngine live(1);
  live.RegisterSymbolOnPartition(1, 0);

  std::vector<Event> events;
  {
    JournalWriter writer(pre_journal.path());
    auto apply = [&](const Command& cmd) {
      writer.Append(cmd);
      live.Dispatch(cmd, events);
    };
    apply(MakeNewOrder(1, 1, 1, Side::Sell, 1000, 10, 1));
    apply(MakeNewOrder(2, 2, 1, Side::Sell, 1005, 5, 2));
    apply(MakeNewOrder(3, 3, 1, Side::Buy, 1000, 4, 3));  // partial fill against order 1
  }

  // Checkpoint.
  WriteSnapshot(snap.path(), live);

  // More activity after the snapshot, journaled separately (log rotation).
  {
    JournalWriter writer(post_journal.path());
    auto apply = [&](const Command& cmd) {
      writer.Append(cmd);
      live.Dispatch(cmd, events);
    };
    apply(MakeNewOrder(4, 4, 1, Side::Buy, 1010, 20, 4));  // sweeps both remaining asks
    apply(Command{CancelCommand{99999, 1, 1, 5}});          // rejected: unknown order, no-op
    apply(MakeNewOrder(5, 5, 1, Side::Sell, 1020, 3, 6));
  }

  // Recovery from scratch.
  PartitionedEngine recovered = LoadSnapshot(snap.path());
  auto replay_commands = ReadJournal(post_journal.path());
  std::vector<Event> discard;
  for (const auto& cmd : replay_commands) {
    recovered.Dispatch(cmd, discard);
  }

  const auto* live_book = live.Partition(0).FindBook(1);
  const auto* recovered_book = recovered.Partition(0).FindBook(1);
  ASSERT_NE(live_book, nullptr);
  ASSERT_NE(recovered_book, nullptr);

  EXPECT_EQ(live_book->HasBestBid(), recovered_book->HasBestBid());
  EXPECT_EQ(live_book->HasBestAsk(), recovered_book->HasBestAsk());
  if (live_book->HasBestBid()) EXPECT_EQ(live_book->BestBid(), recovered_book->BestBid());
  if (live_book->HasBestAsk()) EXPECT_EQ(live_book->BestAsk(), recovered_book->BestAsk());
  EXPECT_EQ(live_book->TotalOrderCount(), recovered_book->TotalOrderCount());
  EXPECT_EQ(live.Partition(0).next_sequence_number(), recovered.Partition(0).next_sequence_number());

  for (auto [price, qty] : live_book->AskLevels()) {
    EXPECT_EQ(recovered_book->QuantityAt(Side::Sell, price), qty);
  }
  for (auto [price, qty] : live_book->BidLevels()) {
    EXPECT_EQ(recovered_book->QuantityAt(Side::Buy, price), qty);
  }
}
