# USB ACK32 Reliable Retry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CRACK/1 USB file transfer survive lost, delayed, and fragmented ACK32 replies, including the final block, without duplicating committed data or leaking binary frames into the JanOS CLI.

**Architecture:** JanOS protocol 4 uses a portable receive state machine for expected data, last-block replay, FIN, and FIN replay, with ESP-IDF code supplying file/console callbacks. Tab5 uses a portable persistent ACK stream/retry state machine and keeps its transport loop responsible only for timed reads/writes and UI. A FIN/quiet-linger exchange prevents the receiver from returning to text before the sender has finished all possible replays.

**Tech Stack:** C11/C17 host tests with GCC/WSL, ESP-IDF C integration, CRACK/1 text negotiation, FTB1 binary framing, ACK32.

**Spec:** `docs/superpowers/specs/2026-09-20-usb-ack32-retry-design.md`

## Global Constraints

- Set/restore JanOS `main/main.c`, JanOS `CMakeLists.txt`, and Tab5 `JANOS_VERSION_REQUIRED` to exactly `1.7.4`; do not create a release or version bump.
- Advertise and require CRACK protocol `4` with `sync=ftb1`, exact `frame32` membership in `ack=byte,frame32`, `replay=last_block`, and `finish=fin32`.
- USB remains 1024-byte FTB blocks with ACK32. Grove and M-BUS remain 8192-byte blocks with one-byte ACK/NAK/CAN and their current baud switching.
- Protocol-4 fixed timings are exactly `ack_wait_ms=2000`, `next_header_ms=7000`, and `finish_linger_ms=7000`; `prepare_ms` alone follows the specified formula/range.
- Data and FIN each have at most three identical transmissions including the initial send.
- Tests must be written and observed failing before production code is changed; reports must preserve RED and GREEN commands/output.
- Agents may run host tests only. The user owns ESP-IDF firmware builds, flashing, and hardware acceptance.
- Preserve both dirty worktrees and unrelated user changes. Do not commit, reset, clean, stage, or rewrite history.

---

### Task 1: JanOS portable protocol-4 receive state

**Files:**
- Create: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.h`
- Create: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/crack_worker_core_test.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `crack_worker_crc32_update()` and ACK32 status values `0x06`, `0x15`, `0x18`.
- Produces: `crack_worker_transfer_state_t`, `crack_worker_transfer_classify()`, `crack_worker_transfer_commit_data()`, `crack_worker_transfer_finalize()`, `crack_worker_transfer_cancel()`, and `crack_worker_prepare_ms()` for Task 2.

- [ ] **Step 1: Add failing state-machine tests**

Add literal FTB-header fixtures that require these public definitions:

```c
typedef enum {
    CRACK_TRANSFER_EXPECTED_DATA,
    CRACK_TRANSFER_DUPLICATE_DATA,
    CRACK_TRANSFER_FIN,
    CRACK_TRANSFER_DUPLICATE_FIN,
    CRACK_TRANSFER_CANCEL,
    CRACK_TRANSFER_INVALID,
} crack_worker_transfer_action_t;

typedef struct {
    uint64_t expected_size;
    uint64_t offset;
    uint64_t last_committed_offset;
    uint32_t expected_index;
    uint32_t block_size;
    uint32_t last_index;
    uint32_t last_length;
    uint32_t last_crc32;
    bool ack32;
    bool have_last;
    bool finalized;
} crack_worker_transfer_state_t;
```

Test expected data; a matching duplicate at EOF; wrong duplicate length/CRC;
index zero with no history; stale/future/`UINT32_MAX`; malformed magic/version;
zero and oversized data; short final block; FIN only at exact EOF/index with
zero length/CRC; duplicate FIN only after finalization; and CAN only at a header
boundary. Mutate offset/index/history after `commit_data()` and assert duplicate
classification never mutates them. Require `finalize()` to transition exactly
once and repeated FIN to remain duplicate FIN.

- [ ] **Step 2: Run RED**

Run from `projectZero/ESP32C5` in WSL:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I main -o /tmp/crack_worker_core_test \
  tests/crack_worker_core_test.c main/crack_worker_core.c \
  main/crack_worker_transfer.c
/tmp/crack_worker_core_test
```

Expected: compile failure because `crack_worker_transfer.h/.c` and symbols do not exist.

- [ ] **Step 3: Implement the minimal portable state machine**

Use an explicit header view rather than reading packed integers:

```c
crack_worker_transfer_action_t crack_worker_transfer_classify(
    const crack_worker_transfer_state_t *state,
    const uint8_t header[16]);
void crack_worker_transfer_commit_data(crack_worker_transfer_state_t *state,
                                       uint32_t index, uint32_t length,
                                       uint32_t payload_crc32);
bool crack_worker_transfer_finalize(crack_worker_transfer_state_t *state);
crack_worker_transfer_action_t crack_worker_transfer_cancel(
    crack_worker_transfer_state_t *state);
uint32_t crack_worker_prepare_ms(uint64_t resume_offset,
                                 bool *resume_allowed);
```

`prepare_ms` returns `10000 + ceil(offset/262144)*1000`, accepts at most
`600000`, and returns `10000` with `resume_allowed=false` above that limit.
Use guarded comparisons (`expected_index > 0`) rather than unsigned subtraction.

- [ ] **Step 4: Run GREEN and regression tests**

Run the Step 2 command. Expected: all JanOS core tests pass with sanitizer output pristine.

- [ ] **Step 5: Self-review without committing**

Confirm every realistic mutation (wrong index, remaining-file bound applied to
a duplicate, second finalization, overflowed prepare formula) breaks a test.
Record RED/GREEN evidence in the task report; do not stage or commit.

---

### Task 2: Integrate replay, FIN, timing, and recovery into JanOS

**Files:**
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_core.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_core.h`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.h`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/main.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/CMakeLists.txt`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/docs/crack_worker_protocol.md`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/crack_worker_core_test.c`

**Interfaces:**
- Consumes: all Task 1 state/actions and `crack_worker_ack32_build()`.
- Produces: JanOS protocol-4 CAPABILITIES/READY output and the production receive loop that Tab5 Task 4 consumes.

- [ ] **Step 1: Write failing production-facing tests**

Extend the host test with injected counters used by the production-side action
adapter:

```c
typedef struct {
    unsigned writes, crc_updates, checkpoints, progress_updates;
    unsigned finalizations, replies;
} crack_worker_transfer_effects_t;
```

Expose a portable `crack_worker_transfer_apply()` callback adapter from
`crack_worker_transfer.c`; the ESP wrapper uses the same function. Tests must
prove expected data invokes each required callback once, duplicate data invokes
none of the write/CRC/checkpoint/progress callbacks but sends the saved ACK,
FIN finalizes once, duplicate FIN only replies, and FIN_LINGER CAN preserves the
finalized file. Add READY formatter tests for all four timing fields and a
receive-session fixture for single block, lost final-data ACK/replay/FIN, lost
FIN ACK/repeated FIN, quiet linger, and no text callback before linger expiry.

- [ ] **Step 2: Run RED**

Run the Task 1 host command. Expected: new adapter/session expectations fail because production integration is absent.

- [ ] **Step 3: Rewrite only the ACK32 branch of `cw_receive()`**

Keep byte mode on its existing loop. In ACK32 mode:

```text
READY -> PREPARING(prepare_ms) -> DATA/FIN_WAIT(next_header_ms)
      -> FIN_LINGER(finish_linger_ms) -> SYNCED/END/prompt
```

Use `rx_ms` only for header remainder/payload. Recognize CAN at the first header
byte in every state. Remember last-block metadata only after successful existing
write/running-CRC/checkpoint work. Consume duplicate payload exactly, call the
portable apply adapter, and log a duplicate counter without advancing. At EOF,
accept last-data replay or the exact zero-length FIN. Finalize/rename exactly
once before FIN ACK; repeated FIN only re-ACKs and resets the quiet timer.

If `crack_worker_prepare_ms()` refuses a resume, reopen/truncate `.part` at zero
and reset file position, running/prefix CRC, checkpoint, offset/index, and replay
metadata before READY. Preserve the existing 256 KiB/final-block checkpoint
policy rather than syncing every 1024-byte USB block.

- [ ] **Step 4: Update protocol negotiation and version literals**

Emit exactly:

```text
[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32 ...
```

ACK32 READY must include `ack_wait_ms=2000 next_header_ms=7000
prepare_ms=<calculated> finish_linger_ms=7000`. Restore both JanOS version
literals to `1.7.4` and document protocol 4, replay, FIN, cancellation, and
timings in `docs/crack_worker_protocol.md`.

- [ ] **Step 5: Run GREEN**

Run the Task 1 host command. Expected: all JanOS tests pass with sanitizer output pristine. Do not run an ESP-IDF firmware build.

- [ ] **Step 6: Self-review without committing**

Trace lost NAK, lost final-data ACK, two lost FIN ACKs, FIN_LINGER CAN, partial
payload timeout, and offset-zero fallback. Record test evidence and touched
files in the report; do not stage or commit.

---

### Task 3: Tab5 portable protocol-4 negotiation and ACK stream

**Files:**
- Modify: `main/hs_crack_remote_core.h`
- Modify: `main/hs_crack_remote_core.c`
- Modify: `tests/hs_crack_remote_core_test.c`

**Interfaces:**
- Consumes: existing `hs_remote_message_t`, ACK32 parser, CRC helper, and receive-command formatter.
- Produces: validated protocol-4 capability/READY helpers, persistent ACK accumulator, ACK action classifier, and three-attempt state used by Task 4.

- [ ] **Step 1: Add failing parser/state tests**

Extend `hs_remote_message_t` with capability booleans and timing fields:

```c
bool ack_frame32, replay_last_block, finish_fin32;
uint32_t ack_wait_ms, next_header_ms, prepare_ms, finish_linger_ms;
```

Add table tests for exact comma-token `frame32` membership (reject substrings),
protocol 3 rejection, and protocol 4 missing-token rejection. READY tests accept
only fixed 2000/7000/7000 and `prepare_ms` 10000..600000 for USB ACK32; reject
missing, zero, overflow, out-of-range, or inconsistent fields while preserving
legacy byte-mode defaults.

Define and test:

```c
typedef struct { uint8_t bytes[32]; size_t used; } hs_remote_ack_stream_t;
typedef enum {
    HS_ACK_CURRENT, HS_ACK_RETRY_NAK, HS_ACK_STALE, HS_ACK_CAN, HS_ACK_INVALID
} hs_remote_ack_action_t;
typedef enum {
    HS_TX_WAIT, HS_TX_RETRANSMIT, HS_TX_ACCEPT, HS_TX_FAIL
} hs_remote_tx_action_t;
```

Feed the accumulator at every split position and across a simulated deadline;
complete stale then partial current ACK; verify malformed frames, current ACK,
current NAK, unlimited exact previous ACKs, wrong-offset stale, future ACK, and
CAN. Verify three attempts maximum and that timeouts never clear `used`.

- [ ] **Step 2: Run RED**

Run from the Tab5 repo in WSL:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I main \
  tests/hs_crack_remote_core_test.c main/hs_crack_remote_core.c \
  main/hs_crack_cache.c -o /tmp/hs_crack_remote_core_test
/tmp/hs_crack_remote_core_test
```

Expected: compile/test failure for the new fields and state APIs.

- [ ] **Step 3: Implement the minimal portable APIs**

Add:

```c
bool hs_remote_capabilities_v4_valid(const hs_remote_message_t *message);
bool hs_remote_ready_valid(const hs_remote_message_t *message, bool usb_ack32);
size_t hs_remote_ack_stream_push(hs_remote_ack_stream_t *stream,
                                 const uint8_t *input, size_t length,
                                 hs_remote_ack32_t *complete);
hs_remote_ack_action_t hs_remote_ack_classify(
    const hs_remote_ack32_t *ack, uint32_t current_index,
    uint64_t before_offset, uint64_t after_offset, bool allow_previous);
hs_remote_tx_action_t hs_remote_tx_on_deadline(unsigned transmissions);
```

`push()` consumes only enough bytes to yield one complete parsed frame and
leaves caller-owned surplus for the next call. A deadline changes no accumulator
state. Classification must guard block-zero underflow and never count FIN as
data progress.

- [ ] **Step 4: Run GREEN and regressions**

Run the Step 2 command. Expected: all Tab5 remote-core tests pass with sanitizer output pristine.

- [ ] **Step 5: Self-review without committing**

Mutation-check token boundaries, fixed timings, byte-mode compatibility,
attempt count, and partial-frame retention. Record RED/GREEN evidence; do not
stage or commit.

---

### Task 4: Integrate retries, PREPARING deadline, FIN, and recovery into Tab5

**Files:**
- Modify: `main/main.c`
- Modify: `tests/README.md`
- Modify: `.superpowers/sdd/2026-09-20-usb-ack32/task-4-read-exact-test.py` only if the existing harness must be generalized
- Create: `tests/test_usb_ack32_upload_contract.py` if the production loop cannot be compiled directly on the host

**Interfaces:**
- Consumes: Task 3 capability, READY, accumulator, classifier, and retry APIs plus JanOS Task 2 wire contract.
- Produces: end-to-end Tab5 USB upload behavior; no interface changes to Grove/M-BUS.

- [ ] **Step 1: Add failing production-loop contract tests**

Prefer compiling an extracted production helper. If `main.c` ESP-IDF coupling
prevents that, make `tests/test_usb_ack32_upload_contract.py` compile a narrow C
harness that includes the actual extracted transfer helper; do not grep source
text or duplicate the algorithm in Python. Cover:

- partial ACK retained across attempt deadline;
- stale ACK then current ACK within the same absolute deadline;
- exactly three immutable block transmissions;
- transport error causes no blind replay;
- PREPARING checks while scanning and immediately before first write, with the
  1000 ms margin and no late FTB output;
- FIN retries, stale final-data ACK during FIN, duplicate FIN ACK before text;
- completion wait continues through quiet reads for `7000+2000+3000` ms;
- header-boundary CAN and bounded END/prompt drain before DIAG;
- uncertain/partial forward write suppresses CAN and DIAG.

- [ ] **Step 2: Run RED**

Run the Task 3 C command and the new focused contract test. Expected: contract
test fails because `hs_crack_remote_upload()` still treats an ACK timeout as
terminal, has no FIN, and ends text wait on the first quiet read.

- [ ] **Step 3: Integrate protocol 4 in `main.c`**

Restore `JANOS_VERSION_REQUIRED` to `1.7.4`. Require
`hs_remote_capabilities_v4_valid()`. Replace per-call exact reads with one
upload-owned `hs_remote_ack_stream_t`; do not flush it between attempts. Use
`ack_wait_ms=2000` as one absolute attempt deadline; stale frames do not extend
it. On deadline call the retry state and retransmit identical header/payload,
maximum three sends. A hard read/write/disconnect error terminates without
blind replay.

Make prefix CRC scanning accept an absolute PREPARING deadline and check it
every chunk plus immediately before the first FTB write. Send CAN only at a
known header boundary with at least the 1000 ms margin. Otherwise wait out the
bounded receiver deadline and do not write into an uncertain stream.

After all data ACKs, send FIN (`FTB\x01`, current index, length zero, CRC zero)
with the same retry machinery. Ignore stale final-data ACKs during FIN. After
the FIN ACK, parse and discard duplicate FIN ACK32 frames, then wait the entire
12000 ms completion deadline for `SYNCED`/END; a quiet read continues waiting.

Add a recovery drain that observes END/prompt before DIAG. If the boundary is
not observed in the state deadline plus 3000 ms, mark the worker lost and use
the existing local fallback without sending DIAG. Keep all byte-mode code paths
unchanged.

- [ ] **Step 4: Update operator documentation**

Document JanOS `1.7.4`/protocol 4, all READY fields, duplicate/FIN logs, retry
limit, expected `SYNCED`, and unchanged Grove/M-BUS behavior in `tests/README.md`.

- [ ] **Step 5: Run GREEN and focused regressions**

Run:

```sh
/tmp/hs_crack_remote_core_test
python3 tests/test_usb_cdc_transfer_config.py
python3 tests/test_usb_ack32_upload_contract.py
```

Also run the existing exact-read harness if it remains applicable. Expected:
all pass with pristine output. Do not run an ESP-IDF firmware build.

- [ ] **Step 6: Self-review without committing**

Trace the recorded hardware failure at block 79, lost final-data ACK, lost FIN
ACK, split 12+20-byte ACK across a deadline, timeout after partial forward
write, and completion text transition. Record evidence; do not stage or commit.

---

### Task 5: Cross-repository verification and handoff

**Files:**
- Modify only if a verified mismatch is found in files already scoped by Tasks 1-4.
- Create: `.superpowers/sdd/2026-09-20-usb-ack32-retry/final-verification.md`

**Interfaces:**
- Consumes: completed JanOS and Tab5 implementations.
- Produces: one evidence-backed handoff for the user's firmware builds and hardware test.

- [ ] **Step 1: Run the complete allowed host verification**

Run both core test commands, `tests/test_usb_cdc_transfer_config.py`, the
production upload contract test, and the exact-read harness if retained. Record
exact commands, exit status, and concise output. Do not build firmware.

- [ ] **Step 2: Perform a literal cross-repository contract audit**

Verify all three active version literals are `1.7.4`; capability protocol 4 and
mandatory tokens match; ACK32 remains 32 bytes; fixed timings and prepare
formula/range match; FIN tuple and FIN ACK tuple match; retry cap is three;
completion wait is 12000 ms; and USB/Grove/M-BUS block/ACK modes remain
1024/frame32 and 8192/byte respectively.

- [ ] **Step 3: Review dirty-tree scope**

List touched files in both repos and distinguish this plan's changes from
pre-existing user changes. Confirm no commit, stage, reset, clean, firmware
build, or binary replacement occurred.

- [ ] **Step 4: Write hardware acceptance handoff**

Give the user the flash order (JanOS first, Tab5 second), cache-miss setup,
expected READY line, duplicate and FIN evidence, `SYNCED`/cache-hit check, and
unchanged Grove/M-BUS check. Include the remaining fact that only the user's
firmware compilation and hardware run can prove ESP-IDF integration.
