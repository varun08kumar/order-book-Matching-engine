#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "matching_engine/net/gateway_session.hpp"
#include "matching_engine/net/tcp_socket.hpp"

namespace me::net {

// Accepts inbound TCP connections and hands each one off as a started
// GatewaySession via `on_new_session`. Runs its own accept thread; never
// touches the matching engine.
class TcpGateway {
 public:
  using NewSessionCallback = std::function<void(std::shared_ptr<GatewaySession>)>;

  explicit TcpGateway(std::uint16_t port) : listener_(port) {}
  ~TcpGateway() { Stop(); }

  void Start(NewSessionCallback on_new_session) {
    callback_ = std::move(on_new_session);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
  }

  void Stop() {
    if (stopped_.exchange(true)) return;
    listener_.Close();  // unblocks Accept()
    if (accept_thread_.joinable()) accept_thread_.join();
  }

  [[nodiscard]] std::uint16_t port() const { return listener_.port(); }

 private:
  void AcceptLoop() {
    std::uint64_t next_session_id = 1;
    while (!stopped_.load(std::memory_order_acquire)) {
      TcpSocket sock = listener_.Accept();
      if (!sock.valid()) break;  // listener closed
      auto session = std::make_shared<GatewaySession>(std::move(sock), next_session_id++);
      session->Start();
      if (callback_) callback_(session);
    }
  }

  TcpListener listener_;
  std::thread accept_thread_;
  std::atomic<bool> stopped_{false};
  NewSessionCallback callback_;
};

}  // namespace me::net
