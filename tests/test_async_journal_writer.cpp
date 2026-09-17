#include "matching_engine/persistence/async_journal_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace me;
using namespace me::persistence;

namespace {
Command MakeCancel(OrderId id, SequenceNumber tag) {
  return Command{CancelCommand{id, 1, 1, tag}};
}
}  // namespace

TEST(AsyncJournalWriter, StopDrainsQueueAndAllCommandsLandOnDisk) {
  auto path = (std::filesystem::temp_directory_path() / "me_test_async_journal.bin").string();
  std::filesystem::remove(path);

  constexpr int kCount = 5000;
  {
    AsyncJournalWriter writer(path, 1024);
    for (int i = 0; i < kCount; ++i) {
      while (!writer.TryAppend(MakeCancel(1, static_cast<SequenceNumber>(i)))) {
        std::this_thread::yield();
      }
    }
    // Destructor calls Stop(), which drains the queue to disk.
  }

  auto commands = ReadJournal(path);
  ASSERT_EQ(commands.size(), static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    auto* c = std::get_if<CancelCommand>(&commands[static_cast<std::size_t>(i)]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->timestamp, static_cast<Timestamp>(i));
  }

  std::filesystem::remove(path);
}

TEST(AsyncJournalWriter, EngineThreadNeverBlocksOnFullQueueWhenNotWaiting) {
  auto path = (std::filesystem::temp_directory_path() / "me_test_async_journal_full.bin").string();
  std::filesystem::remove(path);

  AsyncJournalWriter writer(path, 4);  // tiny capacity
  int accepted = 0;
  for (int i = 0; i < 1000; ++i) {
    if (writer.TryAppend(MakeCancel(1, static_cast<SequenceNumber>(i)))) ++accepted;
  }
  // With such a tiny queue and a fast background drain, most attempts should
  // still land, but the call must never block regardless of outcome; a
  // hung test here would indicate TryAppend blocked.
  EXPECT_GT(accepted, 0);
  writer.Stop();
  std::filesystem::remove(path);
}
