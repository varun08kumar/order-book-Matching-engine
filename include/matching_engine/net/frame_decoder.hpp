#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace me::net {

// Reassembles length-prefixed frames from an arbitrary sequence of byte
// chunks handed in via Feed(). A single TCP recv() may deliver less than one
// frame, exactly one frame, or several frames back to back (coalescing) —
// this decoder makes no assumption about where chunk boundaries fall
// relative to frame boundaries.
//
// Usage: call Feed() with whatever recv() returned, then call
// TryExtractFrame() repeatedly until it returns false to drain every frame
// that is now fully buffered.
class FrameDecoder {
 public:
  static constexpr std::size_t kLengthPrefixBytes = 4;
  static constexpr std::size_t kMaxFrameBytes = 64 * 1024;

  void Feed(const std::uint8_t* data, std::size_t len) {
    buffer_.insert(buffer_.end(), data, data + len);
  }

  // Extracts the next fully-buffered frame body into `out` (replacing its
  // contents). Returns false if no complete frame is available yet.
  // Sets `protocol_error` to true (and returns false) if a declared frame
  // length exceeds kMaxFrameBytes — the caller should close the connection.
  bool TryExtractFrame(std::vector<std::uint8_t>& out, bool& protocol_error) {
    protocol_error = false;
    const std::size_t available = buffer_.size() - read_offset_;
    if (available < kLengthPrefixBytes) {
      Compact();
      return false;
    }

    const std::uint8_t* base = buffer_.data() + read_offset_;
    std::uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= static_cast<std::uint32_t>(base[i]) << (8 * i);

    if (length > kMaxFrameBytes) {
      protocol_error = true;
      return false;
    }

    if (available < kLengthPrefixBytes + length) {
      Compact();
      return false;  // body not fully arrived yet
    }

    out.assign(base + kLengthPrefixBytes, base + kLengthPrefixBytes + length);
    read_offset_ += kLengthPrefixBytes + length;
    Compact();
    return true;
  }

  [[nodiscard]] std::size_t BufferedBytes() const { return buffer_.size() - read_offset_; }

 private:
  // Avoids unbounded growth / O(n^2) re-scans: once consumed bytes make up
  // more than half the buffer (or nothing remains), drop them.
  void Compact() {
    if (read_offset_ == buffer_.size()) {
      buffer_.clear();
      read_offset_ = 0;
    } else if (read_offset_ > 0 && read_offset_ * 2 > buffer_.size()) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
      read_offset_ = 0;
    }
  }

  std::vector<std::uint8_t> buffer_;
  std::size_t read_offset_ = 0;
};

// Prepends a 4-byte little-endian length prefix to `body` and returns the
// full frame ready to send().
inline std::vector<std::uint8_t> FrameMessage(const std::vector<std::uint8_t>& body) {
  std::vector<std::uint8_t> frame;
  frame.reserve(FrameDecoder::kLengthPrefixBytes + body.size());
  const std::uint32_t length = static_cast<std::uint32_t>(body.size());
  for (int i = 0; i < 4; ++i) frame.push_back(static_cast<std::uint8_t>(length >> (8 * i)));
  frame.insert(frame.end(), body.begin(), body.end());
  return frame;
}

}  // namespace me::net
