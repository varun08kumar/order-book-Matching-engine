#include "matching_engine/net/gateway_session.hpp"

#include <thread>

#include "matching_engine/net/frame_decoder.hpp"
#include "matching_engine/net/protocol.hpp"

namespace me::net {

void GatewaySession::Start() {
  reader_thread_ = std::thread([self = shared_from_this()] { self->ReaderLoop(); });
  writer_thread_ = std::thread([self = shared_from_this()] { self->WriterLoop(); });
}

void GatewaySession::Stop() {
  stopping_.store(true, std::memory_order_release);
  socket_.Close();  // unblocks a thread parked in recv()
  if (reader_thread_.joinable()) reader_thread_.join();
  if (writer_thread_.joinable()) writer_thread_.join();
}

void GatewaySession::ReaderLoop() {
  FrameDecoder decoder;
  std::vector<std::uint8_t> recv_buf(64 * 1024);
  std::vector<std::uint8_t> frame_body;

  while (!stopping_.load(std::memory_order_acquire)) {
    int n = socket_.Recv(recv_buf.data(), recv_buf.size());
    if (n <= 0) break;  // connection closed or error

    decoder.Feed(recv_buf.data(), static_cast<std::size_t>(n));
    for (;;) {
      bool protocol_error = false;
      if (!decoder.TryExtractFrame(frame_body, protocol_error)) {
        if (protocol_error) {
          alive_.store(false, std::memory_order_release);
          return;
        }
        break;
      }
      auto cmd = DecodeCommand(frame_body);
      if (!cmd.has_value()) continue;  // malformed message: drop and keep going
      while (!inbound_.TryPush(*cmd)) {
        if (stopping_.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
      }
    }
  }
  alive_.store(false, std::memory_order_release);
}

void GatewaySession::WriterLoop() {
  OutboundFrame body;
  while (!stopping_.load(std::memory_order_acquire)) {
    if (outbound_.TryPop(body)) {
      auto frame = FrameMessage(body);
      if (!socket_.SendAll(frame.data(), frame.size())) {
        alive_.store(false, std::memory_order_release);
        return;
      }
    } else {
      std::this_thread::yield();
    }
  }
  // Drain remaining outbound messages before exiting.
  while (outbound_.TryPop(body)) {
    auto frame = FrameMessage(body);
    if (!socket_.SendAll(frame.data(), frame.size())) break;
  }
}

}  // namespace me::net
