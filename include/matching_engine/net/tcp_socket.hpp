#pragma once

#include <cstddef>
#include <cstdint>

namespace me::net {

// Minimal RAII wrapper around a POSIX socket file descriptor.
class TcpSocket {
 public:
  TcpSocket() = default;
  explicit TcpSocket(int fd) : fd_(fd) {}
  ~TcpSocket() { Close(); }

  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  TcpSocket& operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
      Close();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  void Close();

  [[nodiscard]] bool valid() const { return fd_ >= 0; }
  [[nodiscard]] int fd() const { return fd_; }

  // Returns bytes read (>0), 0 on orderly shutdown, -1 on error.
  int Recv(std::uint8_t* buf, std::size_t len) const;
  // Sends the full buffer, retrying on short writes. Returns false on error.
  bool SendAll(const std::uint8_t* buf, std::size_t len) const;

  void SetNoDelay(bool enable) const;
  // Makes Recv() return -1 (with errno == EAGAIN/EWOULDBLOCK) after waiting
  // this long with nothing to read, instead of blocking forever.
  void SetRecvTimeout(int milliseconds) const;

 private:
  int fd_ = -1;
};

// Listens on 127.0.0.1:`port` (port 0 lets the OS pick a free port).
class TcpListener {
 public:
  explicit TcpListener(std::uint16_t port, int backlog = 16);
  ~TcpListener() { socket_.Close(); }

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  // Blocks until a client connects (or the listener is closed, in which case
  // an invalid TcpSocket is returned).
  TcpSocket Accept() const;

  [[nodiscard]] std::uint16_t port() const { return port_; }
  void Close() { socket_.Close(); }

 private:
  TcpSocket socket_;
  std::uint16_t port_;
};

// Connects to 127.0.0.1:`port`. Returns an invalid socket on failure.
TcpSocket ConnectLoopback(std::uint16_t port);

}  // namespace me::net
