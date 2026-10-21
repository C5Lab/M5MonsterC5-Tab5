# Distributed crack workers specification

Tab5 is the source of truth for captures and wordlists. During a dictionary
run it discovers compatible JanOS devices on Grove, USB, and M-BUS, copies a
content-addressed HCCAPX and wordlist only when missing, and assigns disjoint
byte ranges to local and remote workers. Generic candidates remain local.

The JanOS protocol is `CRACK/1`: `capabilities`, `probe`, `receive`, `reset`,
`start`, `status`, and `cancel`. File transfer uses `FTB\x01` blocks with CRC32,
ACK/NAK/CAN, resumable `.part` files, and an atomic final rename. Tab5 must
verify the worker's `prefix_crc` before resuming and issue `reset` on mismatch.
The wire prefix remains `CRACK/1`, while capability version 2 requires STATUS
and DONE to carry a monotonic `safe_offset`. That checkpoint advances only
after a line has been skipped completely or a candidate verification finishes.

Tab5 requires the JanOS 1.7.5 development build and capability protocol 3 or newer. Protocol 3 adds
negotiated block replies: USB appends `ack32` to `receive`, requires
`READY ack_size=32`, and accumulates exactly 32 bytes within one ACK deadline.
The `FTA\x01` reply contains a block index, ACK/NAK/CAN status, committed offset,
CRC32, and zero reserved bytes. Only a valid ACK for the sent index and end
offset advances progress. A NAK retries only when its index matches and its
offset is the previous committed boundary; CAN, malformed/stale replies, and
timeouts fail synchronization and trigger the existing automatic USB DIAG query.
Partial-file prefix validation and resumable recovery remain in place. Grove
and M-BUS omit `ack32`, keep 8192-byte blocks and one-byte replies, and accept
only `READY ack_size=0` or `1` (omitted defaults to `1`). See the
[ACK32 wire design](2026-09-20-usb-ack32-design.md) for the frame layout.

All available compatible workers are used automatically. Failure of one remote
worker degrades capacity but does not invalidate results from other workers.
One missed status is transient; three consecutive missed polls lose the lease.
Any valid response resets that counter. A lost or failed lease is resumed
locally from its last confirmed `safe_offset`.
The first found password cancels every remaining worker. User cancellation also
cancels all remotes. A wordlist is exhausted only when every assigned shard
finishes `not_found`; incomplete remote shards must not be journaled as complete.

No firmware build or commit is performed by Codex; the user builds firmware.
