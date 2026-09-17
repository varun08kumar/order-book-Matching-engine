// Runnable gateway server: wires together the TCP gateway, symbol-routed
// matching engine, async journal, and market-data publisher exactly as
// documented in docs/architecture.md. Every inbound Command is journaled
// (asynchronously) before being applied; every resulting Event is fanned
// out to the market-data publisher (also asynchronously) and echoed back
// to the originating session as an execution report.
//
// Usage: gateway_server [port] [num_partitions] [num_symbols] [snapshot_path]
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "matching_engine/market_data/market_data_publisher.hpp"
#include "matching_engine/net/protocol.hpp"
#include "matching_engine/net/tcp_gateway.hpp"
#include "matching_engine/partitioned_engine.hpp"
#include "matching_engine/persistence/async_journal_writer.hpp"
#include "matching_engine/persistence/snapshot.hpp"

using namespace me;
using namespace me::net;
using namespace me::persistence;
using namespace me::market_data;

namespace {
std::atomic<bool> g_shutdown{false};
void HandleSignal(int) { g_shutdown.store(true, std::memory_order_release); }
}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = argc > 1 ? static_cast<std::uint16_t>(std::atoi(argv[1])) : 9000;
  std::size_t num_partitions = argc > 2 ? static_cast<std::size_t>(std::atoi(argv[2])) : 4;
  std::uint32_t num_symbols = argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 16;
  std::string journal_path = argc > 4 ? argv[4] : "gateway_journal.bin";

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  PartitionedEngine engine(num_partitions);
  for (SymbolId s = 1; s <= num_symbols; ++s) engine.RegisterSymbol(s);

  AsyncJournalWriter journal(journal_path);

  MarketDataPublisher market_data;
  market_data.Start([](const Event& e) {
    // Educational sink: a real deployment would multicast/broadcast this to
    // subscribers. See docs/protocol.md for the wire encoding used here.
    (void)e;
  });

  TcpGateway gateway(port);
  std::mutex sessions_mutex;
  std::vector<std::shared_ptr<GatewaySession>> sessions;

  gateway.Start([&](std::shared_ptr<GatewaySession> session) {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions.push_back(std::move(session));
    std::printf("[gateway] session connected (total=%zu)\n", sessions.size());
  });

  std::printf("[gateway] listening on 127.0.0.1:%u, %zu partitions, %u symbols, journal=%s\n",
              gateway.port(), num_partitions, num_symbols, journal_path.c_str());

  while (!g_shutdown.load(std::memory_order_acquire)) {
    std::vector<std::shared_ptr<GatewaySession>> copy;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex);
      copy = sessions;
    }
    bool did_work = false;
    for (auto& session : copy) {
      Command cmd;
      if (session->TryPopInbound(cmd)) {
        did_work = true;
        journal.TryAppend(cmd);  // non-blocking; durable write happens off this thread

        std::vector<Event> events;
        engine.Dispatch(cmd, events);
        for (const auto& e : events) {
          market_data.Publish(e);  // non-blocking
          session->TryPushOutbound(EncodeEvent(e));
        }
      }
    }
    if (!did_work) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }

  std::printf("[gateway] shutting down, writing final snapshot...\n");
  WriteSnapshot(journal_path + ".snapshot", engine);
  gateway.Stop();
  market_data.Stop();
  journal.Stop();
  return 0;
}
