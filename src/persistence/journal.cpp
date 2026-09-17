#include "matching_engine/persistence/journal.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <stdexcept>

#include "matching_engine/net/frame_decoder.hpp"
#include "matching_engine/net/protocol.hpp"

namespace me::persistence {

JournalWriter::JournalWriter(const std::string& path, bool fsync_every_write)
    : fsync_every_write_(fsync_every_write) {
  fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd_ < 0) throw std::runtime_error("JournalWriter: failed to open " + path);
}

JournalWriter::~JournalWriter() {
  if (fd_ >= 0) ::close(fd_);
}

void JournalWriter::Append(const Command& cmd) {
  auto body = net::EncodeCommand(cmd);
  auto frame = net::FrameMessage(body);

  std::size_t written = 0;
  while (written < frame.size()) {
    ssize_t n = ::write(fd_, frame.data() + written, frame.size() - written);
    if (n < 0) throw std::runtime_error("JournalWriter: write() failed");
    written += static_cast<std::size_t>(n);
  }
  if (fsync_every_write_) Flush();
}

void JournalWriter::Flush() {
  if (fd_ >= 0) ::fsync(fd_);
}

std::vector<Command> ReadJournal(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};  // no journal yet is not an error (fresh start)

  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

  net::FrameDecoder decoder;
  decoder.Feed(data.data(), data.size());

  std::vector<Command> commands;
  std::vector<std::uint8_t> body;
  for (;;) {
    bool protocol_error = false;
    if (!decoder.TryExtractFrame(body, protocol_error)) {
      if (protocol_error) {
        throw std::runtime_error("ReadJournal: corrupt record (frame length exceeds maximum) in " +
                                  path);
      }
      break;  // remaining bytes are a partial trailing record from a crash mid-append; discard
    }
    auto cmd = net::DecodeCommand(body);
    if (!cmd.has_value()) {
      throw std::runtime_error("ReadJournal: corrupt record (undecodable command) in " + path);
    }
    commands.push_back(*cmd);
  }
  return commands;
}

}  // namespace me::persistence
