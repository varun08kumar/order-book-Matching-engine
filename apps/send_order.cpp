// Minimal CLI client for manually poking a running gateway_server.
// Usage: send_order <port> <symbol_id> <side:buy|sell> <price> <quantity> [order_id]
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "matching_engine/net/frame_decoder.hpp"
#include "matching_engine/net/protocol.hpp"
#include "matching_engine/net/tcp_socket.hpp"

using namespace me;
using namespace me::net;

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr,
                  "usage: %s <port> <symbol_id> <buy|sell> <price> <quantity> [order_id]\n",
                  argv[0]);
    return 1;
  }
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[1]));
  SymbolId symbol = static_cast<SymbolId>(std::atoi(argv[2]));
  Side side = (std::strcmp(argv[3], "buy") == 0) ? Side::Buy : Side::Sell;
  Price price = std::atoll(argv[4]);
  Quantity qty = static_cast<Quantity>(std::atoll(argv[5]));
  OrderId order_id = argc > 6 ? static_cast<OrderId>(std::atoll(argv[6])) : 1;

  TcpSocket sock = ConnectLoopback(port);
  if (!sock.valid()) {
    std::fprintf(stderr, "failed to connect to 127.0.0.1:%u\n", port);
    return 1;
  }

  NewOrderCommand cmd;
  cmd.order_id = order_id;
  cmd.trader_id = 1;
  cmd.symbol_id = symbol;
  cmd.side = side;
  cmd.type = OrderType::Limit;
  cmd.time_in_force = TimeInForce::GoodTillCancel;
  cmd.price = price;
  cmd.quantity = qty;
  cmd.timestamp = 0;

  auto frame = FrameMessage(EncodeCommand(Command{cmd}));
  if (!sock.SendAll(frame.data(), frame.size())) {
    std::fprintf(stderr, "send failed\n");
    return 1;
  }
  std::printf("sent NewOrder id=%llu symbol=%u side=%s price=%lld qty=%llu\n",
              static_cast<unsigned long long>(order_id), symbol, argv[3],
              static_cast<long long>(price), static_cast<unsigned long long>(qty));

  // Read whatever execution reports arrive, then stop once the server has
  // gone quiet for a bit rather than blocking forever on a fixed count.
  sock.SetRecvTimeout(300);
  FrameDecoder decoder;
  std::uint8_t buf[4096];
  for (;;) {
    int n = sock.Recv(buf, sizeof(buf));
    if (n <= 0) break;  // timeout or connection closed: no more reports pending
    decoder.Feed(buf, static_cast<std::size_t>(n));
    std::vector<std::uint8_t> body;
    bool err = false;
    while (decoder.TryExtractFrame(body, err)) {
      auto event = DecodeEvent(body);
      if (!event.has_value()) continue;
      std::visit(
          [](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, AcceptedEvent>) {
              std::printf("Accepted: order=%llu filled=%llu remaining=%llu status=%s\n",
                          static_cast<unsigned long long>(e.order_id),
                          static_cast<unsigned long long>(e.filled_quantity),
                          static_cast<unsigned long long>(e.remaining_quantity),
                          std::string(to_string(e.status)).c_str());
            } else if constexpr (std::is_same_v<T, Trade>) {
              std::printf("Trade: price=%lld qty=%llu resting_order=%llu\n",
                          static_cast<long long>(e.price),
                          static_cast<unsigned long long>(e.quantity),
                          static_cast<unsigned long long>(e.resting_order_id));
            } else if constexpr (std::is_same_v<T, BookUpdateEvent>) {
              std::printf("BookUpdate: side=%s price=%lld qty=%llu\n", to_string(e.side).data(),
                          static_cast<long long>(e.price),
                          static_cast<unsigned long long>(e.aggregate_quantity));
            } else if constexpr (std::is_same_v<T, RejectedEvent>) {
              std::printf("Rejected: reason=%s\n", std::string(to_string(e.reason)).c_str());
            }
          },
          *event);
    }
  }
  return 0;
}
