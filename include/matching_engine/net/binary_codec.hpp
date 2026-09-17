#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace me::net {

// Portable little-endian primitive encode/decode. We do not reinterpret_cast
// structs onto the wire (that would bake in host endianness, struct padding,
// and enum underlying-type layout), so every field is written and read
// explicitly, one primitive at a time.
class BufferWriter {
 public:
  explicit BufferWriter(std::vector<std::uint8_t>& buf) : buf_(buf) {}

  void WriteU8(std::uint8_t v) { buf_.push_back(v); }

  void WriteU32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }

  void WriteU64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }

  void WriteI64(std::int64_t v) { WriteU64(static_cast<std::uint64_t>(v)); }

 private:
  std::vector<std::uint8_t>& buf_;
};

class BufferReader {
 public:
  BufferReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  [[nodiscard]] bool HasBytes(std::size_t n) const { return pos_ + n <= size_; }

  [[nodiscard]] bool ReadU8(std::uint8_t& out) {
    if (!HasBytes(1)) return false;
    out = data_[pos_];
    pos_ += 1;
    return true;
  }

  [[nodiscard]] bool ReadU32(std::uint32_t& out) {
    if (!HasBytes(4)) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) out |= static_cast<std::uint32_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 4;
    return true;
  }

  [[nodiscard]] bool ReadU64(std::uint64_t& out) {
    if (!HasBytes(8)) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) out |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 8;
    return true;
  }

  [[nodiscard]] bool ReadI64(std::int64_t& out) {
    std::uint64_t u = 0;
    if (!ReadU64(u)) return false;
    out = static_cast<std::int64_t>(u);
    return true;
  }

  [[nodiscard]] std::size_t remaining() const { return size_ - pos_; }
  [[nodiscard]] std::size_t position() const { return pos_; }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

}  // namespace me::net
