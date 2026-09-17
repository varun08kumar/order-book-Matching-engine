#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "matching_engine/command.hpp"
#include "matching_engine/net/tcp_socket.hpp"
#include "matching_engine/spsc_queue.hpp"

namespace me::net {

// One TCP connection's worth of I/O, decoupled from matching by two bounded
// SPSC queues. The reader thread (owned by this session) is the sole
// producer into `inbound`; the writer thread is the sole consumer of
// `outbound`. Whatever external thread drives the matching engine is the
// sole consumer of `inbound` and sole producer into `outbound` — satisfying
// the single-producer/single-consumer contract on both queues while keeping
// socket I/O completely off the matching hot path.
class GatewaySession : public std::enable_shared_from_this<GatewaySession> {
 public:
  using OutboundFrame = std::vector<std::uint8_t>;

  explicit GatewaySession(TcpSocket socket, std::uint64_t session_id,
                           std::size_t inbound_capacity = 4096,
                           std::size_t outbound_capacity = 4096)
      : socket_(std::move(socket)),
        session_id_(session_id),
        inbound_(inbound_capacity),
        outbound_(outbound_capacity) {}

  ~GatewaySession() { Stop(); }

  void Start();
  void Stop();

  [[nodiscard]] std::uint64_t session_id() const { return session_id_; }
  [[nodiscard]] bool alive() const { return alive_.load(std::memory_order_acquire); }

  // Consumer side (engine-driving thread only).
  bool TryPopInbound(Command& out) { return inbound_.TryPop(out); }
  // Producer side (engine-driving thread only).
  bool TryPushOutbound(OutboundFrame frame) { return outbound_.TryPush(std::move(frame)); }

 private:
  void ReaderLoop();
  void WriterLoop();

  TcpSocket socket_;
  std::uint64_t session_id_;
  std::atomic<bool> alive_{true};
  std::atomic<bool> stopping_{false};

  SpscQueue<Command> inbound_;
  SpscQueue<OutboundFrame> outbound_;

  std::thread reader_thread_;
  std::thread writer_thread_;
};

}  // namespace me::net
