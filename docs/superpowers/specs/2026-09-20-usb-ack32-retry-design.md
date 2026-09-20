# CRACK/1 USB ACK32 reliable retry design

Date: 2026-09-20

Status: revised after audit, awaiting re-audit

Base protocol: `docs/superpowers/specs/2026-09-20-usb-ack32-design.md`

This document supersedes the base design and debug handoff wherever they say
JanOS `1.7.5` or CRACK protocol 3. The development version is restored to
`1.7.4`; protocol 4 is the compatibility gate. No release, commit, firmware
build, or flash is part of the agent work.

## Problem and evidence

Matching the Tab5 USB BULK IN transfer request to the CH34X endpoint MPS of 32
bytes removed the deterministic first-block failure. A hardware run then
accepted 79 consecutive 1024-byte blocks before one ACK32 was lost.

JanOS had committed that block (`offset=86016`, next `block=80`) but waited only
`rx_ms=1273` for the next header. It left raw mode before Tab5's retry window.
Tab5 consumed the first 32 bytes of textual `SYNC_ERROR` as ACK32; the remaining
55 USB bytes exactly matched the rest of the error, END marker, and prompt.

A final-block ACK has an additional race: the current receiver immediately
finalizes and returns to text mode, leaving nowhere to replay the final block.
The protocol therefore needs idempotent data replay and a framed terminal
exchange before either peer changes back to text.

## Goals

- Recover transparently when an ACK32 is delayed, fragmented, or lost.
- Never write, checkpoint, hash, count, or advance an accepted block twice.
- Tolerate any number of valid late replies for the immediately previous block.
- Make final-block completion idempotent before returning to the CLI.
- Bound preparation, retries, and terminal waiting, retaining resume/fallback
  after exhaustion.
- Leave Grove and M-BUS byte acknowledgements and timing unchanged.
- Restore all three active JanOS version requirements to `1.7.4`.

## Capability and compatibility

JanOS advertises:

```text
[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32 ...
```

For protocol 4, `sync=ftb1`, exact comma-list membership of `frame32` in
`ack=byte,frame32`, `replay=last_block`, and `finish=fin32` are mandatory
semantics. Tab5 rejects a distributed worker below protocol 4 or one whose
protocol-4 capability line lacks any mandatory token.
Protocol 3 workers are not used for distributed cracking.

Set/restore these exact values to `1.7.4`:

- JanOS `main/main.c` (`JANOS_VERSION`);
- JanOS `CMakeLists.txt` (`JANOS_VERSION`);
- Tab5 `main/main.c` (`JANOS_VERSION_REQUIRED`).

The version string is informational for this feature; capability protocol 4 is
the wire-compatibility gate.

## ACK32 READY timing negotiation

An ACK32 READY line includes four mandatory unsigned decimal values:

```text
ack_wait_ms=2000 next_header_ms=7000 prepare_ms=<calculated> finish_linger_ms=7000
```

`ack_wait_ms` is Tab5's absolute deadline for one transmission attempt.
`next_header_ms` is JanOS's wait for the first byte of the next FTB header after
any ACK32 or NAK32 is enqueued. Three transmissions (initial plus two retries)
fit inside 7000 ms. Header remainder and payload reads retain the calculated
`rx_ms`; partially delivered FTB data therefore still fails promptly.

`prepare_ms` covers READY to the first byte of the first FTB header while Tab5
validates a resumable prefix. JanOS calculates:

```text
prepare_ms = 10000 + ceil(resume_offset / 262144) * 1000
```

The accepted range is 10000..600000 ms. If the calculated value would exceed
600000, JanOS offers offset zero for this session and uses 10000 ms. That
fallback opens/truncates the `.part` file at zero and resets the running/prefix
CRC, file position, checkpoint state, expected index, and last-block session
metadata before READY; it must not retain state from the abandoned resume.

Tab5 starts a local PREPARING deadline from receipt of READY, reserving 1000 ms
as a first-header send margin. It checks the deadline while scanning the prefix
and again immediately before its first transport write. If validation cannot
finish before `prepare_ms - 1000`, Tab5 aborts without sending an FTB header and
enters the bounded recovery drain below. A validation mismatch or local read
error discovered while at least 1000 ms remains sends CAN at the header
boundary. Thus Tab5 never begins binary FTB output after JanOS may have left
raw mode.

`finish_linger_ms` is the quiet period JanOS observes after the most recent
valid FIN before it emits textual completion and returns to the CLI. Tab5's
completion deadline is `finish_linger_ms + ack_wait_ms + 3000`, measured after
it accepts the FIN ACK. It must not stop on an earlier two-second quiet read.

Protocol 4 accepts exactly `ack_wait_ms=2000`, `next_header_ms=7000`, and
`finish_linger_ms=7000`; other values are negotiation errors. Only
`prepare_ms` varies according to the formula and range above.

In USB ACK32 mode missing, zero, overflowing, out-of-range, or internally
inconsistent timing fields are fatal negotiation errors. Legacy byte mode does
not require or use these fields and retains existing behavior.

## ACK32 receiver states in JanOS

Every receive command starts with empty replay/FIN metadata even when resuming;
the block index restarts at zero. FTB magic/version and safe nonzero/max buffer
length validation happen before reading a data payload.

### PREPARING

After READY, JanOS waits up to `prepare_ms` for the first header byte. Once a
header starts, the remaining header and payload use `rx_ms`. A CAN during this
state cancels cleanly.

### DATA

JanOS retains metadata for exactly the last successfully committed data block:

- block index;
- payload length;
- payload CRC32 from the header;
- committed absolute offset after that block.

A data header is classified as follows:

1. **Expected:** `index == expected_index`, length is nonzero and within both
   the buffer and remaining-file bounds. Read payload, validate CRC, perform
   the existing write/running-CRC/checkpoint sequence, advance once, remember
   metadata only after that sequence succeeds, and send ACK32.
2. **Duplicate last block:** ACK32 mode, history exists, index is exactly one
   less than `expected_index` without unsigned underflow, and length/header CRC
   match saved metadata. The duplicate uses the saved-length/buffer bound, not
   the current remaining-file bound. Consume exactly that payload, perform no
   write, payload CRC update, checkpoint, count, offset, or index change, and
   resend the saved ACK32.
3. **Invalid:** malformed magic/version, zero/oversized length, stale/future
   index, or metadata mismatch. Send CAN and terminate the existing invalid
   header path.

The committed original is authoritative. A duplicate whose matching header
declares the saved length is consumed and re-ACKed without revalidating payload
CRC. A short/timed-out duplicate remains fatal because framing is unknown.

After every ACK32 or NAK32, waiting for the next first header byte uses
`next_header_ms`; the timer begins after the reply has been enqueued. This also
applies after a lost NAK. Replayed headers reset that window.

When committed offset reaches expected size, JanOS remains in DATA/FIN_WAIT and
still accepts an identical replay of the last data block. It does not finalize
or emit text until FIN is received.

### FIN_WAIT and FIN_LINGER

FIN is an FTB control header with the normal 16-byte magic/version, index equal
to `expected_index`, length zero, and CRC32 zero. It has no payload. Length zero
is legal only for this exact FIN tuple after all expected bytes are committed.

On the first valid FIN, JanOS finalizes and atomically publishes the file once.
Only after successful finalization does it send an ACK32 whose index is the FIN
index and whose committed offset is the expected file size. It then enters
FIN_LINGER. Finalization failure sends CAN/error and never acknowledges FIN.

During FIN_LINGER, an identical FIN is idempotent: do not rename/finalize again,
resend the same FIN ACK32, and restart the `finish_linger_ms` quiet timer. Any
other binary frame is invalid. After one uninterrupted quiet period, JanOS
emits `SYNCED`, the normal END marker/prompt, and returns to the CLI. Thus text
cannot precede the sender's last possible FIN replay.

If all FIN acknowledgements are lost, Tab5 exhausts its bounded FIN retries and
uses the existing diagnostic/resume path. A file already finalized remains a
valid subsequent cache hit; it is never rolled back or duplicated.

## Cancellation and diagnostic recovery

A single CAN byte is recognized only at an FTB header boundary in PREPARING,
DATA, FIN_WAIT, and FIN_LINGER. In PREPARING/DATA/FIN_WAIT it cancels the active
raw receive and emits the normal terminal error/END/prompt. In FIN_LINGER the
file is already final: CAN only ends the linger early and emits the successful
`SYNCED`/END/prompt; it never removes or rolls back the published file.

Tab5 may send CAN only when it knows no partial FTB header or payload has been
written and the negotiated receiver deadline has not expired. After a partial
forward write, hard transport error, or uncertain deadline, it injects neither
CAN nor text into the stream; it waits through the applicable bounded receiver
timeout (or reconnects after a disconnect).

Before issuing DIAG, Tab5 runs a bounded recovery drain until terminal END and
prompt are observed, using at most the current state deadline plus 3000 ms.
The drain accepts the same residual complete ACK32 frames as the normal stream
parser. If no terminal boundary appears, DIAG is not written into the uncertain
stream; the worker is marked lost and the existing local fallback is used.

## Tab5 streaming ACK reader

The ACK32 accumulator belongs to the entire USB upload, not one read call or
one retry. It retains 0..31 bytes across per-attempt deadlines. A deadline does
not flush or reset it; the next wait completes the same frame. The accumulator
resets only after a complete 32-byte frame is consumed or the exchange ends.

Reads distinguish:

- deadline with zero or partial progress: eligible for identical retransmission;
- hard transport/disconnect error: terminal, with no blind replay;
- cancellation: terminal;
- complete frame: parse and classify.

All fragments within an attempt share one absolute `ack_wait_ms` deadline.
Ignoring stale complete frames does not extend that deadline. No input flush is
performed between attempts.

For each data block, at most three identical transmissions are made:

- ACK for current index/post-offset: accept and advance once;
- NAK for current index/pre-offset: retransmit if an attempt remains;
- ACK for the immediately previous index with committed offset equal to the
  current pre-offset: delayed stale reply; consume and keep waiting;
- CAN: terminate;
- future index, impossible offset, bad magic/CRC/reserved bytes, or any other
  complete frame: terminate and query DIAG;
- deadline without a decisive frame: retransmit if an attempt remains.

There is no one-stale-frame limit. This covers the race in which Tab5 sends a
duplicate, accepts the original ACK, advances, and later receives the re-ACK.

After all data blocks are acknowledged, Tab5 sends the immutable FIN header
with the same three-attempt policy and accepts only its exact ACK tuple (while
still ignoring valid stale ACKs for the final data block). It then sends no
more binary frames and waits through the negotiated completion deadline.

During completion wait, the stream parser recognizes and discards complete,
valid duplicate FIN ACK32 frames before feeding subsequent bytes to the text
line parser. This handles an original FIN ACK arriving after a replay was
already sent. The text reader waits until `SYNCED`/END or the full negotiated
deadline; an intermediate quiet read does not terminate it.

After data or FIN retry exhaustion, Tab5 sends CAN only while JanOS can still
safely interpret it, performs the bounded terminal drain, queries USB DIAG only
after the receiver returns to CLI, and
uses the existing failed-worker resume/fallback path. Progress advances only
for a matching current data ACK, never for FIN or stale frames.

## Portable production state machines

JanOS portable code exposes the actual header/state classifier used by the
production receive loop, including expected data, duplicate data, FIN, duplicate
FIN, and invalid outcomes. The ESP-IDF wrapper performs I/O from those actions.
Injected host callbacks/counters verify the real duplicate branch performs no
write, CRC, checkpoint, progress, or finalization twice.

Tab5 portable code owns the real persistent 32-byte accumulator plus ACK action
classification and retry/FIN state transitions. The production upload loop uses
these interfaces; tests must not merely reimplement the intended algorithm.

## Host tests

JanOS tests cover:

- expected block and matching duplicate at normal and EOF offsets;
- malformed magic, zero/oversized data, index-zero/no-history, wrong duplicate
  length/CRC, stale/future indices, and unsigned boundaries;
- duplicate payload consumption with unchanged write, running CRC, checkpoint,
  checked/progress, expected index, offset, and finalization counters;
- single-block and short-final-block files;
- lost final-data ACK followed by replay then FIN;
- lost FIN ACK, repeated FIN, original FIN ACK/replay race, one finalization,
  bounded terminal quiet exit, and no text before that exit;
- nonzero resume whose preparation exceeds `rx_ms`, prefix mismatch CAN, timing
  calculation/range/reset behavior (including truncation and CRC/checkpoint
  reset on offset-zero fallback), and lost NAK keeping raw mode;
- protocol 4 capability and all READY fields in production-facing coverage.

Tab5 tests cover:

- current ACK, current NAK retry, previous exact stale ACK ignored repeatedly,
  wrong-offset stale/future ACK rejected, CAN and transport error termination;
- ACK fragmentation at every byte split, including an attempt deadline between
  fragments, stale full frame followed by partial current frame, and malformed
  full frames;
- exactly three transmissions on no ACK, immutable replay bytes, one absolute
  deadline per attempt, and no flushing/reset of partial bytes;
- PREPARING deadline checks during prefix scanning and before first write,
  1000 ms send margin, no late first FTB header, and safe offset-zero restart;
- READY timing values: valid boundaries plus missing, zero, overflow,
  out-of-range, and inconsistent negative cases; byte mode remains unchanged;
- FIN retry/ACK, stale final-data ACK during FIN, duplicate FIN ACK before text,
  full completion wait, and completion timeout;
- header-boundary CAN in every receiver state, finalized-file preservation,
  partial-forward-write no-injection behavior, bounded END/prompt recovery drain,
  and suppression of DIAG when the CLI boundary remains uncertain;
- protocol 3 rejection and protocol 4 mandatory-token acceptance/rejection.

Cross-repository audit verifies protocol 4, mandatory capability tokens, the
four timing fields, retry cap, FIN tuple, replay rules, and all three `1.7.4`
values. It also confirms USB remains 1024-byte/ACK32 while Grove and M-BUS
remain 8192-byte/byte-ACK.

## Hardware acceptance

The user flashes JanOS `1.7.4`/protocol 4 first and Tab5 second, then forces a
USB cache miss with a changed wordlist. Expected behavior:

1. READY reports ACK32 and the four negotiated timing fields.
2. Consecutive ACK32 replies advance monotonically through the entire file.
3. Suppressing/delaying a data ACK replays one immutable block, increments a
   JanOS duplicate diagnostic, and advances progress only once.
4. Suppressing/delaying the final data or FIN ACK still produces one finalized
   file followed by `SYNCED`; the next run is a cache hit.
5. Grove/M-BUS retain 8192-byte blocks and byte replies.

Firmware compilation, flashing, and hardware acceptance belong to the user.
