#include "matching_engine/net/tcp_socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace me::net {

void TcpSocket::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

int TcpSocket::Recv(std::uint8_t* buf, std::size_t len) const {
  ssize_t n = ::recv(fd_, buf, len, 0);
  return static_cast<int>(n);
}

bool TcpSocket::SendAll(const std::uint8_t* buf, std::size_t len) const {
  std::size_t sent = 0;
  while (sent < len) {
    ssize_t n = ::send(fd_, buf + sent, len - sent, 0);
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

void TcpSocket::SetNoDelay(bool enable) const {
  int flag = enable ? 1 : 0;
  ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

void TcpSocket::SetRecvTimeout(int milliseconds) const {
  timeval tv{};
  tv.tv_sec = milliseconds / 1000;
  tv.tv_usec = (milliseconds % 1000) * 1000;
  ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

TcpListener::TcpListener(std::uint16_t port, int backlog) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("socket() failed");

  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    throw std::runtime_error(std::string("bind() failed: ") + std::strerror(errno));
  }

  socklen_t addr_len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0) {
    ::close(fd);
    throw std::runtime_error("getsockname() failed");
  }
  port_ = ntohs(addr.sin_port);

  if (::listen(fd, backlog) < 0) {
    ::close(fd);
    throw std::runtime_error("listen() failed");
  }

  socket_ = TcpSocket(fd);
}

TcpSocket TcpListener::Accept() const {
  int fd = ::accept(socket_.fd(), nullptr, nullptr);
  if (fd < 0) return TcpSocket();
  TcpSocket s(fd);
  s.SetNoDelay(true);
  return s;
}

TcpSocket ConnectLoopback(std::uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return TcpSocket();

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return TcpSocket();
  }
  TcpSocket s(fd);
  s.SetNoDelay(true);
  return s;
}

}  // namespace me::net
