#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "matching_engine/command.hpp"
#include "matching_engine/events.hpp"

namespace me::net {

enum class MessageType : std::uint8_t {
  NewOrder = 0x01,
  Cancel = 0x02,
  Modify = 0x03,

  Accepted = 0x81,
  Rejected = 0x82,
  Cancelled = 0x83,
  Modified = 0x84,
  TradeMsg = 0x85,
  BookUpdateMsg = 0x86,
};

// Decodes a frame body (as produced by FrameDecoder) into a Command. Returns
// std::nullopt if the body is malformed (wrong type byte, truncated
// payload) — the caller should treat that as a protocol violation.
std::optional<Command> DecodeCommand(const std::vector<std::uint8_t>& body);

// Encodes a Command into a frame body (type byte + payload), matching the
// wire layout documented in docs/protocol.md.
std::vector<std::uint8_t> EncodeCommand(const Command& cmd);

// Encodes an outbound Event into a frame body.
std::vector<std::uint8_t> EncodeEvent(const Event& event);

// Decodes a frame body produced by EncodeEvent back into an Event. Used by
// test clients and any consumer that needs to parse execution reports.
std::optional<Event> DecodeEvent(const std::vector<std::uint8_t>& body);

}  // namespace me::net
