# CRACK/1 USB ACK32 design

Date: 2026-09-20

## Problem

The Tab5 can send a complete 1024-byte FTB block through the CH34x USB-UART
bridge and JanOS commits it, but the one-byte `ACK` (`0x06`) does not reach the
Tab5 reader. Diagnostics prove that the forward path succeeds: the worker
advances its persisted offset and waits for the next block while Tab5 times out
waiting for the reply. Text responses remain usable.

The attached bridge exposes a 32-byte bulk-IN endpoint. A one-byte UART reply is
therefore not a safe synchronization primitive for this transport even after
enabling the CH34x short-packet baud-register bit.

## Compatibility and negotiation

CRACK/1 capability protocol 3 adds fixed-size block replies. JanOS advertises:

```text
[CRACK/1] CAPABILITIES protocol=3 sync=ftb1 ack=byte,frame32 ...
```

Tab5 requires protocol 3 for distributed workers after this change. The Tab5
required JanOS development version remains 1.7.5.

The receive command gains an optional reply-mode token:

```text
crack_worker receive <kind> <size> <crc32> <block-size> [ack32]
```

Tab5 includes `ack32` only for the USB worker. Grove and M-BUS omit it and keep
the existing one-byte ACK/NAK/CAN behavior. JanOS rejects unknown reply-mode
tokens instead of silently selecting a different framing mode.

`READY` reports the selected reply size:

```text
[CRACK/1] READY ... bsize=1024 rx_ms=... ack_size=32
```

Tab5 verifies that the returned size matches the requested mode before sending
binary data.

## ACK32 frame

Every USB block reply is exactly 32 bytes:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII `FTA` followed by version byte `0x01` |
| 4 | 4 | block index, little-endian |
| 8 | 1 | status: ACK `0x06`, NAK `0x15`, or CAN `0x18` |
| 9 | 3 | reserved, zero |
| 12 | 8 | committed file offset, little-endian |
| 20 | 4 | CRC32 of bytes 0 through 19 |
| 24 | 8 | reserved, zero |

The endpoint-sized reply forces CH34x to submit a complete bulk-IN packet and
also lets Tab5 reject a stale, truncated, or corrupted acknowledgement. For an
ACK, the index and committed offset must match the block just sent. For NAK or
CAN, the index identifies the block being rejected and the offset remains the
last committed file boundary.

## JanOS changes

- Accept and validate the optional `ack32` receive argument.
- Return `ack_size` in `READY`.
- Add a pure helper that builds an ACK32 frame.
- Use that helper for every ACK, NAK, and CAN while ACK32 mode is active.
- Keep the legacy one-byte replies when ACK32 was not requested.
- Advance the reported offset only after a block is written and its checkpoint
  requirements are satisfied.
- Advertise protocol 3 and document the new frame.

## Tab5 changes

- Request `ack32` only for USB receive commands.
- Parse and validate `ack_size` from `READY`.
- Add a pure ACK32 decoder which checks magic, reserved bytes, CRC, status,
  block index, and committed offset.
- Read a complete 32-byte reply for USB and retain the one-byte read for Grove
  and M-BUS.
- Treat a malformed or timed-out frame as a synchronization failure and keep
  the existing automatic DIAG query and resumable recovery path.
- Require the JanOS 1.7.5 development build / CRACK capability protocol 3.

## Tests

JanOS host tests cover ACK, NAK, and CAN frame construction, little-endian
index/offset encoding, CRC generation, receive argument selection, and legacy
one-byte behavior.

Tab5 host tests cover USB command formatting, `READY ack_size` parsing, valid
ACK32 decoding, wrong magic, wrong CRC, stale index, wrong committed offset,
invalid status, and the unchanged one-byte Grove/M-BUS mode.

Firmware builds and hardware flashing remain the user's responsibility. Host
tests may be compiled and run locally.

## Acceptance criteria

With a partially synchronized USB wordlist, the next block produces a valid
ACK32 frame, Tab5 advances immediately without an eight-second timeout, and the
transfer reaches `SYNCED`. Re-running the same job must produce a cache hit.
Grove and M-BUS transfers must continue using 8192-byte blocks and one-byte
replies without behavioral changes.
