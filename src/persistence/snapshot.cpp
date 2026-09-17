#include "matching_engine/persistence/snapshot.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "matching_engine/net/binary_codec.hpp"

namespace me::persistence {

namespace {
constexpr char kMagic[4] = {'M', 'E', 'S', 'N'};
constexpr std::uint8_t kVersion = 1;

void WriteOrder(net::BufferWriter& w, const Order& o) {
  w.WriteU64(o.id);
  w.WriteU64(o.trader_id);
  w.WriteU32(o.symbol_id);
  w.WriteU64(o.sequence_number);
  w.WriteU64(o.timestamp);
  w.WriteI64(o.price);
  w.WriteU64(o.quantity);
  w.WriteU64(o.remaining_quantity);
  w.WriteU8(static_cast<std::uint8_t>(o.side));
  w.WriteU8(static_cast<std::uint8_t>(o.type));
  w.WriteU8(static_cast<std::uint8_t>(o.time_in_force));
  w.WriteU8(static_cast<std::uint8_t>(o.status));
}

bool ReadOrder(net::BufferReader& r, Order& o) {
  std::uint64_t id, trader_id, seq, ts, quantity, remaining;
  std::uint32_t symbol_id;
  std::int64_t price;
  std::uint8_t side, type, tif, status;
  if (!r.ReadU64(id) || !r.ReadU64(trader_id) || !r.ReadU32(symbol_id) || !r.ReadU64(seq) ||
      !r.ReadU64(ts) || !r.ReadI64(price) || !r.ReadU64(quantity) || !r.ReadU64(remaining) ||
      !r.ReadU8(side) || !r.ReadU8(type) || !r.ReadU8(tif) || !r.ReadU8(status)) {
    return false;
  }
  o.id = id;
  o.trader_id = trader_id;
  o.symbol_id = symbol_id;
  o.sequence_number = seq;
  o.timestamp = ts;
  o.price = price;
  o.quantity = quantity;
  o.remaining_quantity = remaining;
  o.side = static_cast<Side>(side);
  o.type = static_cast<OrderType>(type);
  o.time_in_force = static_cast<TimeInForce>(tif);
  o.status = static_cast<OrderStatus>(status);
  return true;
}
}  // namespace

void WriteSnapshot(const std::string& path, const PartitionedEngine& engine) {
  std::vector<std::uint8_t> buf;
  net::BufferWriter w(buf);

  for (char c : kMagic) w.WriteU8(static_cast<std::uint8_t>(c));
  w.WriteU8(kVersion);

  const std::size_t partition_count = engine.PartitionCount();
  w.WriteU32(static_cast<std::uint32_t>(partition_count));

  for (std::size_t p = 0; p < partition_count; ++p) {
    const MatchingEngine& part = engine.Partition(p);
    w.WriteU64(part.next_sequence_number());
    const auto& books = part.books();
    w.WriteU32(static_cast<std::uint32_t>(books.size()));
    for (const auto& [symbol_id, book] : books) {
      w.WriteU32(symbol_id);
      auto orders = book.AllRestingOrders();
      w.WriteU32(static_cast<std::uint32_t>(orders.size()));
      for (const Order& o : orders) WriteOrder(w, o);
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("WriteSnapshot: failed to open " + path);
  out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  out.flush();
  if (!out) throw std::runtime_error("WriteSnapshot: write failed for " + path);
}

PartitionedEngine LoadSnapshot(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("LoadSnapshot: failed to open " + path);
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

  net::BufferReader r(data.data(), data.size());
  for (char expected : kMagic) {
    std::uint8_t got;
    if (!r.ReadU8(got) || got != static_cast<std::uint8_t>(expected)) {
      throw std::runtime_error("LoadSnapshot: bad magic in " + path);
    }
  }
  std::uint8_t version;
  if (!r.ReadU8(version) || version != kVersion) {
    throw std::runtime_error("LoadSnapshot: unsupported version in " + path);
  }

  std::uint32_t partition_count;
  if (!r.ReadU32(partition_count)) throw std::runtime_error("LoadSnapshot: truncated header");

  PartitionedEngine engine(partition_count);

  for (std::uint32_t p = 0; p < partition_count; ++p) {
    std::uint64_t next_seq;
    std::uint32_t symbol_count;
    if (!r.ReadU64(next_seq) || !r.ReadU32(symbol_count)) {
      throw std::runtime_error("LoadSnapshot: truncated partition header");
    }
    for (std::uint32_t s = 0; s < symbol_count; ++s) {
      std::uint32_t symbol_id, order_count;
      if (!r.ReadU32(symbol_id) || !r.ReadU32(order_count)) {
        throw std::runtime_error("LoadSnapshot: truncated symbol header");
      }
      engine.RegisterSymbolOnPartition(symbol_id, p);
      for (std::uint32_t i = 0; i < order_count; ++i) {
        Order o;
        if (!ReadOrder(r, o)) throw std::runtime_error("LoadSnapshot: truncated order record");
        engine.Partition(p).RestoreOrder(symbol_id, o);
      }
    }
    engine.Partition(p).SetNextSequenceNumber(next_seq);
  }

  return engine;
}

}  // namespace me::persistence
