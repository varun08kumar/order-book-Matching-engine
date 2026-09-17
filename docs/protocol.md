# Wire Protocol

Transport is plain TCP. Each client holds one persistent connection
("session"). All multi-byte integers are little-endian. There is no
compression or TLS - this is an educational protocol.

## Framing

Every message on the wire is a length-prefixed frame:

```
+-------------------+---------------------------+
| length : u32 (LE) | body : `length` bytes      |
+-------------------+---------------------------+
```

`length` counts only the bytes in `body` (it does not include itself). The
decoder (`FrameDecoder`) buffers incoming bytes across an arbitrary number of
`recv()` calls and only yields a frame once all `length` body bytes have
arrived - a single `recv()` may contain zero, one, a partial, or several
frames, and the decoder makes no assumption either way. Frames larger than
`FrameDecoder::kMaxFrameBytes` (64 KiB) are treated as a protocol violation
and the connection is closed.

## Body

```
+-----------+----------------------------------+
| type : u8 | type-specific payload             |
+-----------+----------------------------------+
```

`type` selects one of the message layouts below. Enum-valued fields are
encoded as their underlying `u8` value (see `types.hpp`).

## Client -> Server messages

| type | name      | payload fields (in order)                                                                                   |
|------|-----------|--------------------------------------------------------------------------------------------------------------|
| 0x01 | NewOrder  | order_id:u64, trader_id:u64, symbol_id:u32, side:u8, order_type:u8, time_in_force:u8, price:i64, quantity:u64, timestamp:u64 |
| 0x02 | Cancel    | order_id:u64, trader_id:u64, symbol_id:u32, timestamp:u64                                                    |
| 0x03 | Modify    | order_id:u64, trader_id:u64, symbol_id:u32, new_price:i64, new_quantity:u64, timestamp:u64                   |

## Server -> Client messages

| type | name       | payload fields (in order) |
|------|------------|----------------------------|
| 0x81 | Accepted   | sequence_number:u64, timestamp:u64, symbol_id:u32, order_id:u64, trader_id:u64, side:u8, order_type:u8, price:i64, quantity:u64, filled_quantity:u64, remaining_quantity:u64, status:u8 |
| 0x82 | Rejected   | sequence_number:u64, timestamp:u64, symbol_id:u32, order_id:u64, trader_id:u64, reason:u8 |
| 0x83 | Cancelled  | sequence_number:u64, timestamp:u64, symbol_id:u32, order_id:u64, trader_id:u64 |
| 0x84 | Modified   | sequence_number:u64, timestamp:u64, symbol_id:u32, order_id:u64, trader_id:u64, new_price:i64, new_remaining_quantity:u64, lost_priority:u8 |
| 0x85 | Trade      | sequence_number:u64, timestamp:u64, symbol_id:u32, aggressor_order_id:u64, resting_order_id:u64, aggressor_trader_id:u64, resting_trader_id:u64, aggressor_side:u8, price:i64, quantity:u64 |
| 0x86 | BookUpdate | sequence_number:u64, timestamp:u64, symbol_id:u32, side:u8, price:i64, aggregate_quantity:u64 |

A `BookUpdate` with `aggregate_quantity == 0` means that price level was
fully removed.

## Pipeline

```
socket recv() -> FrameDecoder (framing) -> OrderCodec::DecodeCommand (Phase 3 decoder)
             -> GatewaySession::inbound SPSC queue
             -> [consumed by the engine-driving thread, never the socket thread]
             -> PartitionedEngine::Dispatch (per-partition matching, single-threaded)
             -> OrderCodec::EncodeEvent
             -> GatewaySession::outbound SPSC queue
             -> writer thread -> socket send()
```

The socket-facing reader/writer threads never touch the matching engine, and
the engine-driving thread never touches a socket - they only exchange data
through the bounded SPSC queues documented in `spsc_queue.hpp`.
