#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "matching_engine/net/frame_decoder.hpp"
#include "matching_engine/net/protocol.hpp"
#include "matching_engine/net/tcp_gateway.hpp"
#include "matching_engine/net/tcp_socket.hpp"
#include "matching_engine/partitioned_engine.hpp"

using namespace me;
using namespace me::net;

namespace {

// Drives a PartitionedEngine from GatewaySession inbound/outbound queues on
// a dedicated thread, mirroring how a real deployment decouples socket I/O
// from the matching hot path (see docs/protocol.md).
class TestServer {
 public:
  explicit TestServer(std::size_t partitions) : engine_(partitions), gateway_(0) {}

  void RegisterSymbol(SymbolId sym) { engine_.RegisterSymbol(sym); }

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
      std::vector<std::shared_ptr<GatewaySession>> sessions_copy;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_copy = sessions_;
      }
      bool did_work = false;
      for (auto& session : sessions_copy) {
        Command cmd;
        if (session->TryPopInbound(cmd)) {
          did_work = true;
          std::vector<Event> events;
          engine_.Dispatch(cmd, events);
          for (const auto& e : events) {
            session->TryPushOutbound(EncodeEvent(e));
          }
        }
      }
      if (!did_work) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  PartitionedEngine engine_;
  TcpGateway gateway_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<GatewaySession>> sessions_;
  std::atomic<bool> running_{false};
  std::thread dispatch_thread_;
};

// Sends `frame` split into small pieces with tiny sleeps in between, to
// exercise the server's ability to reassemble fragmented messages.
void SendFragmented(const TcpSocket& sock, const std::vector<std::uint8_t>& frame,
                     std::size_t chunk_size) {
  std::size_t offset = 0;
  while (offset < frame.size()) {
    std::size_t n = std::min(chunk_size, frame.size() - offset);
    ASSERT_TRUE(sock.SendAll(frame.data() + offset, n));
    offset += n;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

// Blocks (with a timeout) until at least one full frame is available, then
// decodes it into an Event.
std::optional<Event> RecvOneEvent(const TcpSocket& sock, FrameDecoder& decoder) {
  std::vector<std::uint8_t> body;
  bool err = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (decoder.TryExtractFrame(body, err)) {
      if (err) return std::nullopt;
      return DecodeEvent(body);
    }
    std::uint8_t buf[4096];
    int n = sock.Recv(buf, sizeof(buf));
    if (n > 0) decoder.Feed(buf, static_cast<std::size_t>(n));
  }
  return std::nullopt;
}

}  // namespace

TEST(TcpGatewayEndToEnd, FragmentedNewOrderProducesAcceptedResponse) {
  TestServer server(1);
  server.RegisterSymbol(1);
  server.Start();

  TcpSocket client = ConnectLoopback(server.port());
  ASSERT_TRUE(client.valid());

  NewOrderCommand cmd;
  cmd.order_id = 1;
  cmd.trader_id = 42;
  cmd.symbol_id = 1;
  cmd.side = Side::Buy;
  cmd.type = OrderType::Limit;
  cmd.time_in_force = TimeInForce::GoodTillCancel;
  cmd.price = 1000;
  cmd.quantity = 10;
  cmd.timestamp = 1;

  auto frame = FrameMessage(EncodeCommand(Command{cmd}));
  SendFragmented(client, frame, 3);  // deliberately tiny chunks

  FrameDecoder client_decoder;
  auto event = RecvOneEvent(client, client_decoder);
  ASSERT_TRUE(event.has_value());
  auto* accepted = std::get_if<AcceptedEvent>(&*event);
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->order_id, 1u);
  EXPECT_EQ(accepted->status, OrderStatus::New);

  server.Stop();
}

TEST(TcpGatewayEndToEnd, TwoOrdersOverTcpProduceTrade) {
  TestServer server(1);
  server.RegisterSymbol(1);
  server.Start();

  TcpSocket seller = ConnectLoopback(server.port());
  TcpSocket buyer = ConnectLoopback(server.port());
  ASSERT_TRUE(seller.valid());
  ASSERT_TRUE(buyer.valid());

  NewOrderCommand sell;
  sell.order_id = 1;
  sell.trader_id = 1;
  sell.symbol_id = 1;
  sell.side = Side::Sell;
  sell.type = OrderType::Limit;
  sell.time_in_force = TimeInForce::GoodTillCancel;
  sell.price = 500;
  sell.quantity = 10;
  sell.timestamp = 1;

  auto sell_frame = FrameMessage(EncodeCommand(Command{sell}));
  ASSERT_TRUE(seller.SendAll(sell_frame.data(), sell_frame.size()));

  FrameDecoder seller_decoder;
  auto sell_ack = RecvOneEvent(seller, seller_decoder);
  ASSERT_TRUE(sell_ack.has_value());
  ASSERT_NE(std::get_if<AcceptedEvent>(&*sell_ack), nullptr);

  NewOrderCommand buy = sell;
  buy.order_id = 2;
  buy.trader_id = 2;
  buy.side = Side::Buy;
  buy.timestamp = 2;

  auto buy_frame = FrameMessage(EncodeCommand(Command{buy}));
  SendFragmented(buyer, buy_frame, 5);

  FrameDecoder buyer_decoder;
  auto buy_ack = RecvOneEvent(buyer, buyer_decoder);
  ASSERT_TRUE(buy_ack.has_value());
  ASSERT_NE(std::get_if<AcceptedEvent>(&*buy_ack), nullptr);

  auto trade_event = RecvOneEvent(buyer, buyer_decoder);
  ASSERT_TRUE(trade_event.has_value());
  auto* trade = std::get_if<Trade>(&*trade_event);
  ASSERT_NE(trade, nullptr);
  EXPECT_EQ(trade->price, 500);
  EXPECT_EQ(trade->quantity, 10u);

  server.Stop();
}
