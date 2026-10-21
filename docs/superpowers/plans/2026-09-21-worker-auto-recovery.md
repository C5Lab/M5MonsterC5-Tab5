# Handshake Cracker Worker Auto-Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Tab5 retain authoritative ownership of distributed dictionary ranges, automatically recover restarted Monsters, and migrate unfinished suffixes to recovered, idle, or local workers without losing coverage.

**Architecture:** Add a small pure-C shard scheduler that owns range state and assignment generations independently of UART and LVGL. Keep transport recovery in `main.c`: a periodic recovery tick performs ping, reconciles the retired job with `status`, re-prepares a rebooted worker after ordered `unknown_job`, and leases the pending suffix through the scheduler. Replace the final ad-hoc failed-worker fallback decision with ledger-driven pending work.

**Tech Stack:** ESP-IDF C17, FreeRTOS timing, JanOS CRACK/1 protocol 4, existing UART/USB transport adapters, Python host harnesses, native GCC tests.

**Spec:** `docs/superpowers/specs/2026-09-21-worker-auto-recovery-design.md`

## Global Constraints

- JanOS remains version `1.7.5` and CRACK/1 remains protocol `4` unless hardware evidence proves a protocol change is required.
- Tab5 is the authoritative scheduler; silence never proves a remote job stopped.
- Resume begins at the greatest confirmed safe offset clamped to the shard bounds.
- Duplicate computation is permitted after an ambiguous loss; uncovered byte ranges are forbidden.
- Every remotely found password is verified locally before acceptance.
- Grove/M-BUS keep their configured hardware-UART baud and 8192-byte blocks; USB keeps its independent CH34x baud and 1024-byte ACK32 blocks.
- Firmware compilation and flashing remain the user's step. Only host tests are run during implementation.
- Preserve unrelated dirty-worktree changes. Do not stage or commit the user's files.

---

## Task 1: Pure shard ledger and recovery policy

**Files:**

- Create: `main/hs_crack_scheduler.h`
- Create: `main/hs_crack_scheduler.c`
- Create: `tests/hs_crack_scheduler_test.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/README.md`

**Interfaces:**

- Produces `hs_sched_shard_t`, the transport-independent authoritative shard record.
- Produces `hs_sched_note_loss()`, `hs_sched_note_progress()`, `hs_sched_can_reattach()`, `hs_sched_queue_suffix()`, `hs_sched_lease_pending()`, and `hs_sched_complete()`.
- Produces per-generation accounting helpers used by Task 3.
- Consumes only fixed-width integer and boolean types; no ESP-IDF, UART, LVGL, filesystem, or global UI dependencies.

- [ ] **Step 1: Write the failing scheduler test**

Create literal range cases in `tests/hs_crack_scheduler_test.c`:

```c
hs_sched_shard_t shard;
hs_sched_init(&shard, 100, 1000, HS_SCHED_OWNER_GROVE);
hs_sched_note_progress(&shard, 420, 31);
CHECK(hs_sched_note_loss(&shard, "old-job"));
CHECK(shard.state == HS_SCHED_RECOVERING);
CHECK(shard.confirmed_safe_offset == 420);

hs_sched_assignment_t next;
CHECK(hs_sched_lease_pending(&shard, HS_SCHED_OWNER_USB,
                             "new-job", &next));
CHECK(next.start == 420);
CHECK(next.end == 1000);
CHECK(next.generation == 2);
```

Cover clamping, monotonic progress, same-generation reattachment, refusal of a retired generation to reclaim migrated work, completion only at `range_end`, and independent accounting after a new generation.

- [ ] **Step 2: Run the test and verify RED**

Run:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    tests/hs_crack_scheduler_test.c main/hs_crack_scheduler.c \
    -o /tmp/hs_crack_scheduler_test
```

Expected: compilation fails because `hs_crack_scheduler.h/.c` and their API do not exist.

- [ ] **Step 3: Implement the minimal pure scheduler**

Define explicit states and owners:

```c
typedef enum {
    HS_SCHED_PENDING,
    HS_SCHED_LEASED,
    HS_SCHED_RECOVERING,
    HS_SCHED_DONE,
    HS_SCHED_LOCAL,
} hs_sched_state_t;

typedef enum {
    HS_SCHED_OWNER_NONE = -1,
    HS_SCHED_OWNER_GROVE,
    HS_SCHED_OWNER_USB,
    HS_SCHED_OWNER_MBUS,
    HS_SCHED_OWNER_LOCAL,
} hs_sched_owner_t;
```

The record contains original bounds, confirmed offset, state, owner,
generation, active/retired job IDs, and per-generation accounting. All offset
updates are monotonic and clamped.

- [ ] **Step 4: Run the native test and verify GREEN**

Run the command from Step 2 followed by `/tmp/hs_crack_scheduler_test`.

Expected: `hs_crack_scheduler_test: PASS`, with no compiler or sanitizer warnings.

- [ ] **Step 5: Register and document the module**

Add `hs_crack_scheduler.c` to the existing component source list in
`main/CMakeLists.txt`. Add the exact host command and tested invariants to
`tests/README.md`.

- [ ] **Step 6: Run the existing core tests**

Run `hs_crack_remote_core_test.c`, `transfer_speed_config_test.c`, and
`tests/check_hs_crack_cmake.cmake`. Expected: all existing tests pass and the
new source is registered exactly once.

---

## Task 2: Recovery decision core

**Files:**

- Modify: `main/hs_crack_scheduler.h`
- Modify: `main/hs_crack_scheduler.c`
- Modify: `tests/hs_crack_scheduler_test.c`

**Interfaces:**

- Consumes recovery observations: no pong, running status, terminal status,
  ordered `unknown_job`, or ambiguous response.
- Produces one action: wait, reattach, prepare-and-restart, cancel-retired,
  lease-elsewhere, complete, or local fallback.
- Task 3 maps these pure actions to transport operations.

- [ ] **Step 1: Add failing table-driven recovery tests**

Use hand-derived expected actions:

```c
static const struct {
    hs_sched_recovery_event_t event;
    bool suffix_already_leased;
    hs_sched_action_t want;
} cases[] = {
    {HS_SCHED_NO_PONG, false, HS_SCHED_WAIT},
    {HS_SCHED_OLD_JOB_RUNNING, false, HS_SCHED_REATTACH},
    {HS_SCHED_OLD_JOB_RUNNING, true, HS_SCHED_CANCEL_RETIRED},
    {HS_SCHED_UNKNOWN_JOB, false, HS_SCHED_PREPARE_RESTART},
    {HS_SCHED_AMBIGUOUS, false, HS_SCHED_WAIT},
};
```

Also test that terminal `found` is surfaced even for a retired assignment and
that incomplete `not_found` queues the suffix rather than completing it.

- [ ] **Step 2: Run the scheduler test and verify RED**

Expected: compilation fails on the missing recovery event/action API.

- [ ] **Step 3: Implement the pure transition function**

Add:

```c
hs_sched_action_t hs_sched_recovery_action(
    const hs_sched_shard_t *shard,
    hs_sched_recovery_event_t event,
    uint64_t reported_safe_offset);
```

The function never performs I/O and never infers `unknown_job` from silence.

- [ ] **Step 4: Run the scheduler test and verify GREEN**

Expected: all ledger and recovery-policy cases pass under `-Werror` and
sanitizers.

---

## Task 3: Transport-safe liveness and old-job reconciliation

**Files:**

- Modify: `main/main.c`
- Create: `tests/test_worker_auto_recovery_contract.py`
- Modify: `tests/README.md`

**Interfaces:**

- Consumes a worker-owned Grove/USB/M-BUS transport.
- Produces a bounded liveness result without invoking global board detection.
- Sends `crack_worker status <retired_job>` only after liveness succeeds.
- Returns a parsed recovery observation to the pure scheduler from Task 2.

- [ ] **Step 1: Write a failing production-path liveness harness**

The harness extracts the real recovery functions from `main.c` and supplies
deterministic transport reads. Required cases:

```text
boot banner -> pong
no bytes -> timeout
pong -> correlated running STATUS
pong -> correlated DONE found
pong -> ordered REJECTED unknown_job
pong -> unrelated/ambiguous CRACK record
```

Assert that ping is sent before status, each wait is bounded, and global
`detect_boards()` is never called.

- [ ] **Step 2: Run the harness and verify RED**

Run:

```sh
python3 tests/test_worker_auto_recovery_contract.py
```

Expected: failure because the recovery liveness/reconciliation functions do
not exist.

- [ ] **Step 3: Implement a worker-owned recovery ping**

Add a helper that writes one framed `ping`, reads complete lines through the
already-owned transport, tolerates boot-banner noise, and recognizes `pong`.
Do not call `ping_usb()`, `ping_uart()`, `ping_uart_direct()`, or
`detect_boards()` because those functions mutate detection state and assume
boot-time ownership.

- [ ] **Step 4: Implement correlated retired-job status**

After pong, send exactly:

```text
crack_worker status <retired_job>\r\n
```

Accept only a matching `STATUS`/`DONE` or an ordered
`REJECTED code=unknown_job`. Return ambiguity for silence, malformed frames,
and unrelated jobs.

- [ ] **Step 5: Run the harness and verify GREEN**

Expected: every transport case passes and call ordering is identical for Grove,
USB, and M-BUS.

---

## Task 4: Worker recovery lifecycle

**Files:**

- Modify: `main/main.c`
- Modify: `tests/test_worker_auto_recovery_contract.py`
- Modify: `tests/test_worker_stage_retry_contract.py`

**Interfaces:**

- Consumes the pure scheduler action, capture identity, selected wordlist
  identity/path, and existing cache synchronization functions.
- Produces a ready/active recovered worker or leaves it recovering without
  changing its confirmed safe offset.
- Uses existing `hs_crack_remote_capabilities()`, `hs_crack_remote_sync_file()`,
  and status-first start reconciliation.

- [ ] **Step 1: Add failing lifecycle cases**

Cover:

- running old job reattaches with the same job ID and generation;
- `unknown_job` runs capabilities and both cache probes before restart;
- missing content uses existing resumable sync;
- restart creates a new job ID and range `[safe_offset, shard_end)`;
- ambiguous status does not call start;
- failed preparation retains recovery state and safe offset;
- retired found result remains available for local verification.

- [ ] **Step 2: Run the recovery harness and verify RED**

Expected: lifecycle assertions fail before production integration exists.

- [ ] **Step 3: Extend the worker runtime state**

Add scheduler linkage and bounded recovery timing to
`hs_crack_remote_worker_t`: shard index, health, retired job, recovery attempts,
and next recovery timestamp. Keep the pure range authority in the scheduler
record rather than duplicating mutable bounds in two places.

- [ ] **Step 4: Implement reattach and restart paths**

For `REATTACH`, preserve the assignment generation and accounting, update the
monotonic safe offset, clear misses, and mark the worker active.

For `PREPARE_RESTART`, validate capabilities, probe/sync capture and wordlist,
then call the existing start path with a new job ID and the scheduler-provided
suffix. Do not modify JanOS protocol commands.

- [ ] **Step 5: Add exact recovery logs and compact UI states**

Emit the log records defined in the spec and update only the affected worker's
row with `lost, probing`, `alive, checking job`, `reattached`, `rebuilding
cache`, `queued`, or `reassigned`.

- [ ] **Step 6: Run recovery and stage tests and verify GREEN**

Expected: recovery cases pass; the existing 13 stage/cancel tests remain green.

---

## Task 5: Scheduler tick, work stealing, and local drain

**Files:**

- Modify: `main/main.c`
- Create: `tests/test_worker_reassignment_contract.py`
- Modify: `tests/README.md`

**Interfaces:**

- Consumes worker poll results and the shard ledger.
- Produces periodic recovery attempts, idle-worker reassignment, and final local
  processing of every remaining ledger suffix.
- Replaces the failed-worker-specific fallback source of truth while retaining
  `hs_crack_seek_range_start()` and local password verification.

- [ ] **Step 1: Write failing reassignment tests**

The production-path harness must prove:

1. Three misses create one recovering ledger entry at the confirmed offset.
2. Healthy workers continue polling while recovery is pending.
3. Recovery ping is rate-limited to one attempt per five seconds.
4. An idle different worker leases the oldest pending suffix.
5. A recovered original worker leases pending work when no newer owner exists.
6. A late old running job is cancelled after migration.
7. No active or recoverable remote leaves all pending suffixes for local drain.
8. Local drain starts at each ledger safe offset and ends at its immutable end.
9. Cancel prevents new leases and does not store `not_found`.

- [ ] **Step 2: Run the reassignment harness and verify RED**

Run:

```sh
python3 tests/test_worker_reassignment_contract.py
```

Expected: failure because the main loop does not yet tick recovering workers or
lease pending shards.

- [ ] **Step 3: Insert the scheduler tick into both cracking phases**

During Tab5's local shard and during the remote-drain phase, call one shared
scheduler tick every existing two-second poll interval. The tick:

```text
poll active assignments
advance ledger safe offsets
mark lost leases recovering
run due recovery probes
assign pending suffixes to idle ready workers
publish counts/UI
```

Do not block healthy worker polling behind one recovery attempt.

- [ ] **Step 4: Replace ad-hoc failed-worker fallback selection**

Iterate ledger entries that are not `done`. For each suffix, first offer it to
an idle remote. Once no remote can make progress, process it locally with
`hs_crack_seek_range_start()` and the immutable ledger end.

- [ ] **Step 5: Make terminal completion ledger-authoritative**

Only `hs_sched_complete()` or successful local drain marks a shard done.
Incomplete `not_found`, error, cancellation, and retired generations keep or
queue the suffix.

- [ ] **Step 6: Run reassignment and all remote host tests and verify GREEN**

Expected: reassignment contract passes; USB ACK32, start recovery, stage retry,
cancel teardown, fast-baud fallback, and core parser tests remain green.

---

## Task 6: Acceptance documentation and complete host gate

**Files:**

- Modify: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Modify: `docs/Handshake_Crack_Manager_Roadmap.md`
- Modify: `tests/README.md`
- Modify: `skill-observations/checkpoints.log`

**Interfaces:**

- Consumes passing host evidence from Tasks 1–5.
- Produces an exact hardware checklist for the user; does not claim hardware
  success before serial evidence exists.

- [ ] **Step 1: Run the complete host gate**

Run all existing Python contract tests plus:

```sh
python3 tests/test_worker_auto_recovery_contract.py
python3 tests/test_worker_reassignment_contract.py
```

Compile and execute:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    tests/hs_crack_scheduler_test.c main/hs_crack_scheduler.c \
    -o /tmp/hs_crack_scheduler_test
/tmp/hs_crack_scheduler_test
```

Expected: zero failures and no warnings. Do not run `idf.py build`.

- [ ] **Step 2: Run scoped source checks**

Run native Windows CMake registration validation and `git diff --check` for the
modified source, tests, and docs.

- [ ] **Step 3: Update acceptance status without overstating hardware proof**

Record host auto-recovery as implemented/passing. Keep D2 hardware status
partial until logs demonstrate ping, old-job reconciliation, restart or
reattach, reassignment, and continued progress.

- [ ] **Step 4: Give the user the hardware sequence**

The first acceptance run is Grove reboot during cracking. Required evidence:

```text
LOST Grove ... safe_offset=N
RECOVERY_TICK Grove ... action=ping
RECOVERY_ALIVE Grove result=pong
RECOVERY_STATUS Grove ... result=unknown_job
REASSIGN Grove|USB|MBus ... range=N-end
STATUS <new owner> ... safe_offset>N
```

Follow with transient link-only reattachment, USB reboot, M-BUS reboot, work
stealing to a different worker, and all-workers-offline local drain.

- [ ] **Step 5: Record the task-observer checkpoint**

Flush any reusable workflow observation or append the required no-observation
checkpoint before the final handoff.
