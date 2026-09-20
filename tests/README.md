# Host tests

Pure-logic pieces that carry no LVGL or ESP-IDF dependency are tested on the
host, so they can be verified without flashing a Tab5.

Run any of them with a host compiler. On a Windows workstation without one, the
same commands work inside WSL against the `/mnt/c/...` checkout.

## `cgw_parser_test.c`

Covers `main/screens/cgw_parser.c` — the JanOS GITM (`capture_gateway`) status
parser. The fixtures are the exact line shapes emitted by
`capture_gateway_print_status()` in the JanOS firmware, including the
human-readable `MY_LOG_INFO` lines that precede the `[CGW]` block and must be
ignored.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I main/screens -o /tmp/cgw_parser_test \
    tests/cgw_parser_test.c main/screens/cgw_parser.c
/tmp/cgw_parser_test
```

What it pins down:

- a complete block is published only on the exact `[CGW] END` terminator;
- SSIDs containing spaces survive the anchored `ssid=` / ` security=` /
  ` upstream_ssid=` split required by contract section 8.5;
- `drops=` is never harvested out of `rate_queue_drops=`;
- `file_bytes` keeps 64-bit precision;
- recorder loss and shaper loss are reported independently, and adaptive
  throttling on its own is not degradation;
- `[CGW]` appearing inside a log message is not parsed as protocol;
- duplicate `[CGW_CLIENT]` rows upsert by MAC instead of appending;
- `[PCAP_FINAL]` and the legacy `PCAP saved:` marker both yield a file path.

The contract itself lives in the JanOS repository at
`projectZero/ESP32C5/docs/janos-capture-gateway.md`, section 8.

## `boot_melody_osc_test.c`

Covers the shared tone renderer `audio_play_notes()` (main.c), which the startup
melody and the UI alert chime both go through:

- the two-term recurrence oscillator, which replaced a per-sample `sinf()` call
  so a tune can stream instead of being rendered in full before the first sample
  reaches the codec;
- the per-note envelope clamp, which lets an alert tone ask for a 140 ms decay
  on a 70 ms note without the attack and release ramps ever overlapping.

```sh
gcc -std=c11 -Wall -Wextra -O2 -o /tmp/boot_melody_osc_test \
    tests/boot_melody_osc_test.c -lm
/tmp/boot_melody_osc_test
```

For every note in all three melodies it checks that the recurrence holds pitch
within one cent, that its amplitude does not drift over the longest note, and
that the exact `int16_t` conversion the firmware performs (including the 0.85
gain) never wraps.

For the envelope it checks, on every note length the melodies use and on every
note of the four alert tones (join, win, warn, alarm), that the ramps never
overlap, that the release hands over at exactly 1.0, that each note starts and
ends in silence, and that no boot melody note is short enough to be clamped -
i.e. sharing the renderer with the alerts left the startup melody sounding
exactly as it did.

## `pcap_summary_reducers_test.c`

Covers `components/pcap_summary/pcap_summary_reducers.c` - the key normalizers
and the aggregation primitives the PCAP summary is built from. This is the unit
test list of `docs/PCAP_Analysis_and_Implementation_Plan.md` section 22.9:
empty, single element, ties, limits, overflow and deterministic order.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Icomponents/pcap_summary/include -o /tmp/reducers_test \
    tests/pcap_summary_reducers_test.c \
    components/pcap_summary/pcap_summary_reducers.c
/tmp/reducers_test
```

What it pins down:

- a key that does not fit its buffer is refused, never clipped, because a
  clipped key merges two different observations into one row;
- `A -> B` and `B -> A` produce the same host-pair key, with the port stripped
  and MAC case folded;
- a domain loses its root dot and its case, so `WWW.Example.COM.` and
  `www.example.com` are one key;
- ties sort lexicographically, so the table never depends on insertion order;
- an evicting table raises `approximate`, and counters saturate instead of
  wrapping;
- a ratio carries its denominator: zero samples render as `n/a`, a thin sample
  is labelled `low sample`, and `0/100` stays a real zero;
- window buckets hold both edges of the capture span, out-of-span samples are
  counted apart, and the burst score is 1.0 for a flat capture;
- the distinct-key sketch counts a returning key once and admits when its load
  makes the count approximate.

## `pcap_summary_report_test.c`

End-to-end over the analysis stack: it synthesizes PCAP files on disk, runs
`pcap_reader` -> `pcap_summary` -> `pcap_summary_render_report()` and compares
the result with a baseline embedded in the test.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Icomponents/pcap_summary/include -Icomponents/pcap_reader/include \
    -o /tmp/report_test tests/pcap_summary_report_test.c \
    components/pcap_summary/pcap_summary.c \
    components/pcap_summary/pcap_summary_reducers.c \
    components/pcap_summary/pcap_summary_report.c \
    components/pcap_reader/pcap_reader.c
/tmp/report_test            # fixtures go to /tmp, or to the directory in argv[1]
```

The fixtures are the acceptance list of section 22.9: a reference capture whose
whole report is pinned byte for byte, an empty capture, a 10-byte file that must
be refused, a capture whose last record is cut, records with broken protocol
data and a lying `caplen`, an NXDOMAIN burst, one-pair dominance and a
high-cardinality capture. It also renders the same file twice and requires
identical bytes, and renders into a 400-byte buffer to check that a clipped
report says `[report truncated]`.

When a deliberate wording change breaks the baseline, the test prints the actual
report between `----8<----` markers so the new text can be reviewed and pasted
into `reference_report[]`.

## `hs_crack_cache_test.c`

Exercises the handshake cracker's persistent attempt cache, quoted CSV
round-trips, capture/wordlist fingerprints, resume records, deterministic
wordlist ordering, byte progress, ETA, and checkpoint policy.

```sh
gcc -std=c17 -Wall -Wextra -Werror -I main \
    tests/hs_crack_cache_test.c main/hs_crack_cache.c \
    -o /tmp/hs_crack_cache_test
/tmp/hs_crack_cache_test
```

The cache test also verifies the persistent full-file CRC used to identify
wordlists copied to distributed JanOS workers. An entry is reused only when the
path, size, modification time, head CRC, and tail CRC still match.

## `hs_crack_remote_core_test.c`

Exercises `CRACK/1` capability response parsing, password/SSID hex decoding,
malformed messages, deterministic gap-free byte sharding, three-strike lease
loss, monotonic safe checkpoints, and fallback range selection. Also covers
USB `ack32` receive negotiation, `READY ack_size`, ACK32 validation and diagnostic
reasons (length, magic/version, reserved bytes, status and CRC), stale ACKs,
and NAK/CAN frames that must never satisfy ACK matching. Protocol 4 tests cover
mandatory capability tokens, READY timing validation, persistent fragmented
ACK32 accumulation, current/stale ACK classification, and the three-send limit.

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    tests/hs_crack_remote_core_test.c main/hs_crack_remote_core.c main/hs_crack_cache.c \
    -o /tmp/hs_crack_remote_core_test
/tmp/hs_crack_remote_core_test
```

## `usb_vcp_config_test.c`

Exercises the pure CH34x USB-UART configuration helpers used before USB board
detection. It pins the supported CH340/CH341 VID/PID set, the fixed 115200 baud
register for both the inverted-short-packet `0x27` revision (`0xCC03`) and newer
revisions (`0xCC83`), the future 921600/2000000 encodings, 8N1 line control,
and the SERIAL_INIT -> baud/LCR -> MODEM_CTRL request sequence used by the
Linux driver.

```sh
gcc -std=c11 -Wall -Wextra -Werror -I main \
    tests/usb_vcp_config_test.c main/usb_vcp_config.c \
    -o /tmp/usb_vcp_config_test
/tmp/usb_vcp_config_test
```

## `test_usb_cdc_transfer_config.py`

Checks that both the active configuration and clean-build defaults request a
32-byte USB CDC BULK IN transfer. This matches the CH34x endpoint MPS, allowing
one full ACK32 packet to complete the host transfer instead of remaining inside
the component's former 512-byte request until a later short packet arrives.

```sh
python3 tests/test_usb_cdc_transfer_config.py
```

## `test_usb_blocking_read_contract.py`

Host-compiles the production USB transport reader against a deterministic CDC
ring-buffer fixture. It verifies that a read with a 100 ms limit wakes as soon
as data arrives instead of polling the buffered size and sleeping for the full
interval. It also pins timeout and disconnected-device behavior.

```sh
python3 tests/test_usb_blocking_read_contract.py
```

## `test_usb_ack32_upload_contract.py`

Host-compiles the actual `main.c` upload loop and USB reader with deterministic
transport, clock, and SD-read fixtures. The production C code performs all
retry, parsing, FIN, deadline, and recovery decisions. Fixtures exercise the
recorded block-79 ACK loss, a 12+20-byte reply split across a deadline, repeated
stale replies, immutable data/FIN replay, transport errors, partial forward
writes, PREPARING expiry, prefix reset, terminal prompt gating, and the full
12000 ms completion timeout. No ESP-IDF build or device is needed.

```sh
python3 tests/test_usb_ack32_upload_contract.py
```

## `test_usb_probe_recovery_contract.py`

Host-compiles the production command sender and worker probe path. It verifies
that each CLI command, including its CRLF terminator, is submitted as one write;
that every silent FILE probe times out after 5 seconds; and that Tab5 performs
one DIAG-assisted USB CLI recovery before retrying. The separate size-scaled
timeout remains in the receive path while JanOS validates a resumable prefix
before sending `READY`.

```sh
python3 tests/test_usb_probe_recovery_contract.py
```

## `test_fast_uart_sync_fallback_contract.py`

Host-compiles the production Grove/M-BUS/USB file-sync wrapper. It verifies
that USB selects 921600 while hardware UART links retain their configured fast
rate, and that a failed sync restores the console to 115200 and
uses the remaining attempts through the existing resumable upload path.
Disabled sync and a cancelled operation do not trigger another upload. A failed
115200 restore rejects an otherwise successful fast upload, so the worker
cannot be marked ready with an unusable control channel. If the CLI boundary is
lost, Tab5 restores only its local UART and abandons that worker without sending
another peer command.

```sh
python3 tests/test_fast_uart_sync_fallback_contract.py
```

## `test_usb_fast_baud_contract.py`

Host-compiles the production JanOS baud handshake and CH34x control path. It
verifies that only a supported CH34x USB adapter negotiates 921600, that the
bridge changes rate between the JanOS acknowledgement and confirmation, and
that success, negotiation failure, restore, and unsafe-boundary abandonment
all return the local USB bridge to 115200 without sending peer commands after
the boundary is lost. A missing confirmation response is treated as ambiguous:
Tab5 retries confirmation and queries the machine-readable baud status at the
fast rate. It starts a 115200 transfer only after JanOS explicitly reports
`rate=115200 pending=no`; otherwise the worker is rejected instead of sending
commands at a guessed rate.

```sh
python3 tests/test_usb_fast_baud_contract.py
```

## `test_usb_start_recovery_contract.py`

Host-compiles the production remote-worker start path. It verifies that a
missing `ACCEPTED` response is reconciled with a correlated `status` command
before local fallback. A matching `STARTED`, `STATUS`, or `DONE` record adopts
the existing job; only an ordered `unknown_job` response permits another start,
up to the shared three-attempt budget, with the identical job ID and byte range
on USB, Grove, and M-BUS. An ambiguous state is never treated as permission to
start a duplicate job. Uncorrelated `REJECTED` records are ignored during start, while a
terminal `found` recovered through status is retained in Tab5 memory and
consumed before the next transport operation so its password cannot be skipped
by a later fallback.

```sh
python3 tests/test_usb_start_recovery_contract.py
```

## `test_usb_ready_line_budget_contract.py`

Host-compiles the production CRACK text-message reader and reproduces a long
USB `READY` line arriving after part of the per-call wait budget was spent on
JanOS preparation. Waiting for the first byte remains sliced at 500 ms, but an
observed first byte starts a separate, bounded two-second line-completion
window. Otherwise the partial `READY` is discarded and the following `END` is
misidentified as the response to `receive`. Short callers retain their original
limit while no line has started.

```sh
python3 tests/test_usb_ready_line_budget_contract.py
```

## Manual distributed-crack smoke test

1. For hardware acceptance, the operator flashes JanOS 1.7.5 containing
   `crack_worker` capability protocol 4 to the Monsters, then Tab5, and puts an
   SD card in each worker. Capability protocol 4 must advertise `sync=ftb1`,
   `frame32` membership in `ack=byte,frame32`, `replay=last_block`, and
   `finish=fin32`. Tab5 rejects protocol 3 and missing mandatory semantics.
2. Connect workers through any combination of Grove, USB, and M-BUS, then boot
   Tab5 and confirm those transports are detected.
3. Put a small known wordlist in `/lab/wordlist` on the Tab5 SD card. The file
   does not need to exist on worker SD cards.
4. Start dictionary cracking. The popup must say `Preparing workers...` while
   checking or copying worker files; it must not claim to be trying passwords
   during this phase. An older worker cache without a `.verified` marker is
   untrusted and must be transferred once more. Later boots with the unchanged
   list should answer the probe immediately and skip the file copy.
5. During cracking verify the popup shows separate `done` and `fallback`
   counts, and that JanOS answers `STATUS` with a monotonic `safe_offset`. A
   password found by any participant must cancel the others and be saved by
   the normal Tab5 result path.
6. Disconnect one worker during a run. The first two missed polls must keep it
   active; the third must mark it for fallback. Tab5 must resume from the last
   confirmed `safe_offset` and must not mark the wordlist exhausted until the
   fallback completes.

### USB CDC sync check

For CH34x adapters, USB switches the Monster console and local bridge to
921600 during file synchronization. Unsupported USB serial adapters remain at
115200. To exercise the upload path, connect only the USB worker and select a
handshake or change a wordlist so the worker answers `state=missing` rather
than `state=present`. The log must show:

- `[USB] CDC driver config: ... in_xfer=32` and a descriptor with BULK IN
  `wMaxPacketSize 32`;
- `Console running at 921600 baud` before a missing-file transfer and
  `Console back at 115200 baud` after it;
- `[HS-DBG] USB: READY ... bsize=1024 ... rx_ms=... ack_size=32
  ack_wait_ms=2000 next_header_ms=7000 prepare_ms=... finish_linger_ms=7000`;
- an ACK32 for every block (`ACK32 status=0x06 blk=... offset=...`), with the
  sent block index and the committed offset at the end of that block;
- `FIN blk=... transmission=1`, its exact ACK32, then
  `FIN acknowledged, waiting SYNCED`, followed by `SYNCED`, END, the CLI prompt,
  and the USB worker becoming ready.

Repeat the same job and confirm a cache hit skips the upload. Interrupt a USB
copy, restart it, and confirm the validated partial offset resumes with ACK32
without the old eight-second ACK timeout. A timeout or current-block NAK
(`status=0x15`, previous committed offset) retransmits identical bytes, at most
three transmissions total. Every attempt has one absolute 2000 ms deadline;
partial ACK bytes survive it. Valid ACKs for the immediately previous block
are ignored, even repeatedly, without extending that deadline. JanOS logs a
duplicate data block and re-ACKs it without advancing progress a second time.

If the 921600 negotiation fails before binary transfer starts, Tab5 waits for
JanOS's unconfirmed-baud timeout, restores the CH34x bridge to 115200, and uses
the slow resumable upload path. If the raw/CLI boundary is lost during fast
transfer, Tab5 changes only the local bridge back to 115200 and abandons that
worker; it does not risk sending another command into an unknown stream.

The final data block remains replayable until the FIN exchange. FIN uses the
next block index and zero length/CRC, with the same three-send limit. A replayed
FIN must not publish the file twice. Duplicate FIN ACKs can appear before text
and are logged as discarded residual ACK32 frames. Expect about 7000 ms of
quiet after the last FIN; Tab5 waits through quiet reads for up to
`7000 + 2000 + 3000 = 12000` ms after accepting its FIN ACK.

JanOS calculates `prepare_ms = 10000 + ceil(resume_offset / 262144) * 1000`,
within 10000..600000 ms; an excessive resume restarts at zero. Tab5 reserves
1000 ms for the first header, checking the budget during prefix scanning and
immediately before the first FTB write. An expired budget emits no late FTB.

CAN (`0x18`), malformed/impossible ACK tuples, hard transport errors, and retry
exhaustion fail synchronization. Tab5 sends CAN only at a known header boundary
before the receiver deadline, then drains through END and the actual prompt
before issuing `[HS-DIAG]`. Partial forward writes and hard read errors suppress
CAN. If no terminal boundary appears within the receiver state deadline plus
3000 ms, DIAG is suppressed and the worker is lost for local fallback. Malformed
frames log a printable validation reason. Prefix mismatch resets only after a
safe terminal drain; interrupted uploads retain the existing resume path.

An abandoned `.part` up to 1 MiB may resume after prefix validation. A larger
partial must restart from offset zero instead of blocking the JanOS console on
a long CRC scan.

Grove and M-BUS should negotiate `bsize=8192` after switching their hardware
UART to the configured fast transfer rate (the log prints the selected
`Console running at ... baud` value). They continue using one-byte replies
(`got=1 reply=0x06`), accept `ack_size=0` or `1` (a missing field defaults to
`1`), and reject `ack_size=32`. If their control probe or transfer fails at the
fast rate, Tab5 restores 115200 before retrying; a partial copy continues from
its validated resume offset. USB rejects any ACK size other than `32` before
sending binary blocks.

Worker preparation is reported per transport as `capabilities`, `capture`,
`wordlist`, and `start`, with `attempt=N/3` in serial logs and the compact UI.
Each stage has at most three top-level attempts with 250 ms then 1000 ms
backoff. This does not multiply the separate three-transmission ACK/FIN retry
budget. USB retries only after its END/prompt recovery boundary; Grove and
M-BUS also drain END/prompt after CAN so delayed `[CRACK/1]` text cannot be
misread as one-byte ACKs. A missing boundary immediately marks that worker
lost and leaves its unconfirmed suffix for local fallback.

`start` keeps one job ID and range for all attempts. A missing confirmation is
reconciled with `status <job>` first; `start` is resent only after an ordered
`unknown_job`. JanOS 1.7.5 may answer an identical replay with
`ACCEPTED ... replay=1 state=...` without creating another task. During work,
`STATUS` may add `phase` and `progress_age_ms`; three consecutive missed status
responses are shown as `1/3`, `2/3`, then `lost -> local`, while any valid
correlated response resets the miss counter.
