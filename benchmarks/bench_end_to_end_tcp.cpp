// Benchmark 5: end-to-end TCP -> decode -> match. A real TCP client sends
// NewOrder messages one at a time over a loopback socket to a real
// TcpGateway-fronted engine and waits for the Accepted/Trade response
// before sending the next — measuring the full pipeline's round-trip
// latency (recv -> frame -> decode -> route -> match -> encode -> send).
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "bench_harness.hpp"
#include "matching_engine/net/frame_decoder.hpp"
#include "matching_engine/net/protocol.hpp"
#include "matching_engine/net/tcp_gateway.hpp"
#include "matching_engine/net/tcp_socket.hpp"
#include "matching_engine/partitioned_engine.hpp"

using namespace me;
using namespace me::net;
using namespace me::bench;

namespace {

class Server {
 public:
  Server() : engine_(1), gateway_(0) {
    engine_.RegisterSymbol(1);
    // Pre-seed one resting sell at every price 1..N so each incoming buy
    // trades immediately without exhausting the book.
    std::vector<Event> discard;
    for (int i = 0; i < 1000000; ++i) {
      engine_.Dispatch(
          NewOrderCommand{static_cast<OrderId>(1'000'000 + i), 1, 1, Side::Sell, OrderType::Limit,
                           static_cast<Price>(i) + 1, 1, TimeInForce::GoodTillCancel,
                           static_cast<Timestamp>(i)},
          discard);
    }
  }

  void Start() {
    gateway_.Start([this](std::shared_ptr<GatewaySession> session) {
      std::lock_guard<std::mutex> lock(mutex_);
      sessions_.push_back(std::move(session));
    });
    running_ = true;
    dispatch_thread_ = std::thread([this] { DispatchLoop(); });
  }

  void Stop() {
    running_ = false;
    if (dispatch_thread_.joinable()) dispatch_thread_.join();
    gateway_.Stop();
  }

  [[nodiscard]] std::uint16_t port() const { return gateway_.port(); }

 private:
  void DispatchLoop() {
    while (running_) {
      std::vector<std::shared_ptr<GatewaySession>> copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = sessions_;
      }
      bool did_work = false;
      for (auto& s : copy) {
        Command cmd;
        if (s->TryPopInbound(cmd)) {
          did_work = true;
          std::vector<Event> events;
          engine_.Dispatch(cmd, events);
          for (const auto& e : events) s->TryPushOutbound(EncodeEvent(e));
        }
      }
      if (!did_work) std::this_thread::yield();
    }
  }

  PartitionedEngine engine_;
  TcpGateway gateway_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<GatewaySession>> sessions_;
  std::atomic<bool> running_{false};
  std::thread dispatch_thread_;
};

}  // namespace

int main() {
  constexpr std::size_t kWarmup = 2000;
  constexpr std::size_t kIterations = 20000;

  Server server;
  server.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  TcpSocket client = ConnectLoopback(server.port());
  if (!client.valid()) {
    std::fprintf(stderr, "failed to connect to benchmark server\n");
    return 1;
  }

  FrameDecoder decoder;
  auto recv_one_event = [&]() -> std::optional<Event> {
    std::vector<std::uint8_t> body;
    for (;;) {
      bool err = false;
      if (decoder.TryExtractFrame(body, err)) return err ? std::nullopt : DecodeEvent(body);
      std::uint8_t buf[4096];
      int n = client.Recv(buf, sizeof(buf));
      if (n <= 0) return std::nullopt;
      decoder.Feed(buf, static_cast<std::size_t>(n));
    }
  };

  // trades/sec must reflect only the timed region; RunBenchmark's internal
  // warmup shares whatever accumulator the lambda closes over, so warmup is
  // driven manually here with a throwaway counter first.
  auto step = [&](std::size_t i, std::size_t& trade_sink) {
    NewOrderCommand cmd;
    cmd.order_id = static_cast<OrderId>(i) + 1;
    cmd.trader_id = 2;
    cmd.symbol_id = 1;
    cmd.side = Side::Buy;
    cmd.type = OrderType::Market;
    cmd.time_in_force = TimeInForce::ImmediateOrCancel;
    cmd.price = 0;
    cmd.quantity = 1;
    cmd.timestamp = i;

    auto frame = FrameMessage(EncodeCommand(Command{cmd}));
    client.SendAll(frame.data(), frame.size());

    // Accepted, then Trade, then a BookUpdate come back for every one of
    // these (liquidity-guaranteed) market buys.
    for (int k = 0; k < 3; ++k) {
      auto event = recv_one_event();
      if (event.has_value() && std::holds_alternative<Trade>(*event)) ++trade_sink;
    }
  };

  std::size_t warmup_trades_discarded = 0;
  for (std::size_t i = 0; i < kWarmup; ++i) step(i, warmup_trades_discarded);

  std::size_t trades = 0;
  auto rec = RunBenchmark("end_to_end_tcp / gateway->decode->match", 0, kIterations,
                           [&](std::size_t i) { step(i, trades); });
  rec.Report(trades);

  server.Stop();
  return 0;
}
