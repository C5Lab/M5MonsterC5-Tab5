# Handshake Cracker Worker Auto-Recovery Design

## Status

Proposed for implementation after user review.

## Goal

Make Tab5 the authoritative scheduler for distributed dictionary shards so a
lost Monster can be detected, queried, reattached after a transient link
failure, restarted from its last confirmed safe offset after a reboot, or
replaced by another available worker without leaving any part of the wordlist
uncovered.

## Scope

This change applies to active distributed cracking after capture and wordlist
preparation have completed. It covers Grove, USB, and M-BUS workers using JanOS
1.7.5 and CRACK/1 protocol 4.

The implementation belongs to Tab5. JanOS already provides the required
operations: CLI `ping`, `crack_worker capabilities`, cache `probe`/`receive`,
`start`, `status`, and `cancel`.

This stage does not rebalance healthy running shards merely to equalize finish
times. A healthy worker keeps its current assignment. Work stealing happens
when a worker becomes idle or when a shard loses its lease.

## Correctness Invariants

1. Tab5, not a Monster, owns the authoritative list of unfinished byte ranges.
2. A range is complete only when a correlated terminal result confirms a
   `safe_offset` at or beyond its end, or Tab5 processes the range locally.
3. Recovery and migration always begin at the greatest confirmed safe offset,
   clamped to the original shard bounds.
4. Silence never proves that the old job stopped. When its state is ambiguous,
   duplicate work is permitted but missing coverage is not.
5. A remote password is accepted only after local cryptographic verification.
6. A late response from a retired assignment can improve its confirmed safe
   offset or provide a password, but cannot reclaim a range already leased to
   a newer assignment.
7. `checked` is accounting information, not proof of byte-range coverage.
8. Cancellation stops recovery ticks, cancels every known active assignment,
   and does not record a partial run as completed `not_found`.

## Data Model

### Worker state

Each physical transport keeps its existing transport and cache information plus
the following scheduler state:

- `health`: `ready`, `active`, `suspect`, `recovering`, or `offline`;
- current assignment identifier, if any;
- retired assignment identifier retained for late status reconciliation;
- consecutive status misses;
- recovery attempt count and next recovery deadline;
- whether the worker is eligible to accept queued work.

Worker identity remains the transport slot (`Grove`, `USB`, or `M-BUS`) for
Stage 0. Reconnection on the same slot is treated as the same physical worker,
but every restarted assignment receives a new CRACK job ID.

### Shard state

Tab5 maintains a small fixed-size shard ledger. Each entry contains:

- immutable original `range_start` and `range_end`;
- monotonic `confirmed_safe_offset`;
- `state`: `pending`, `leased`, `recovering`, `done`, or `local`;
- current owner transport and assignment generation;
- current job ID;
- retired job ID, when recovery must reconcile a previous assignment;
- per-assignment `checked` and already-accounted values.

The initial local and remote partitions are inserted into this ledger. A lost
assignment does not create an unrelated range: its existing entry moves to
`recovering`, and only its remaining suffix can be leased again.

## Scheduler Tick

The existing two-second remote polling cadence remains the main scheduler tick.
Normal active workers receive correlated `status` queries as today.

After three consecutive missed status responses:

1. Record the last confirmed safe offset.
2. Revoke the assignment lease and retain its job ID as retired.
3. Mark the shard `recovering` and the physical worker `recovering`.
4. Make the remaining suffix eligible for another idle worker without stopping
   healthy assignments.
5. Schedule a liveness recovery probe no more often than every five seconds.

An idle recovered or healthy worker takes the oldest pending/recovering suffix.
Healthy workers are never interrupted to steal work. When no remote worker can
accept a remaining suffix, Tab5 processes it locally after remote scheduling
can no longer make progress.

## Recovery Protocol

### Step 1: Liveness

At a recovery deadline Tab5 sends `ping` through the transport already owned by
the cracker and waits for `pong` within a bounded window. The recovery helper
must not invoke global boot board detection or mutate unrelated detection/UI
state. It may snoop and discard boot-banner lines while looking for `pong`.

No `pong` leaves the worker in `recovering` and schedules the next tick. It does
not reset or reduce the shard's confirmed safe offset.

### Step 2: Old job reconciliation

After `pong`, Tab5 sends `crack_worker status <retired_job>`:

- Correlated `STATUS state=running`: reattach only if the shard suffix has not
  already been leased to another assignment. Restore the worker and shard to
  `active`, preserving monotonic safe offset and accounting.
- Correlated terminal `DONE`/`STATUS`: consume the result normally. A found
  password is locally verified. `not_found` completes the shard only when the
  reported safe offset covers its end.
- Ordered `REJECTED code=unknown_job`: the reboot/lost-job case is proven; move
  to worker reinitialization.
- Silence, malformed, uncorrelated, or ambiguous response: keep recovering and
  retry on a later tick. Never start a replacement job on that same worker from
  an ambiguous boundary.

If an old running job returns after its suffix was leased elsewhere, Tab5 sends
`cancel <old_job>`. A late found result is still eligible for local verification.
Other progress from that retired generation cannot reclaim ownership.

### Step 3: Worker reinitialization after `unknown_job`

Tab5 performs the normal bounded worker preparation on the recovered transport:

1. Validate CRACK/1 protocol 4 capabilities.
2. Probe the capture cache.
3. Probe the selected wordlist cache.
4. Synchronize only missing content, using the existing resumable transfer and
   transport-specific baud settings.
5. Mark the worker `ready`.

A failure keeps the worker in recovery; it does not invalidate the shard or
discard its safe offset.

### Step 4: Reassignment

If unfinished work remains, create a new job ID and assign exactly:

```text
[max(confirmed_safe_offset, range_start), range_end)
```

The new assignment increments the shard generation. Its `checked` counters
start from zero, while the run total retains already-accounted checks from the
retired generation. The shard returns to `leased`/`active` only after a
correlated start confirmation.

## Migration to Another Worker

Once a lease is revoked, an idle healthy Monster may take the remaining suffix
without waiting for the original transport to return. This provides
at-least-once coverage:

- overlap after the last confirmed safe offset is allowed;
- gaps are forbidden;
- whichever valid assignment first confirms the shard end marks it complete;
- later terminal records from retired generations cannot change a completed
  shard back to active.

If no Monster is available, the suffix remains queued while other remote work
continues. At the final drain, Tab5 consumes every remaining ledger entry
locally from its confirmed safe offset. The current one-shot end-of-run fallback
therefore becomes a consumer of the same pending-shard ledger rather than a
separate source of truth.

## Progress and Accounting

Coverage progress is derived from shard safe offsets and states. Candidate
throughput may include duplicate checks performed during an ambiguity window,
because those computations actually occurred, but it must not be used to decide
that a byte range is complete.

Every assignment generation has independent `checked` and `accounted` values.
Reattaching the same job preserves those values. Starting a new job resets only
the new generation's counters. This prevents a reset worker from making the
global counter move backward or from re-adding the previous generation's total.

## UI and Logs

The existing worker row gains explicit recovery states without opening another
screen:

- `lost, probing`;
- `alive, checking job`;
- `reattached`;
- `rebuilding cache`;
- `queued`;
- `reassigned to USB/Grove/M-BUS`;
- `local fallback`.

Required machine-readable log events:

```text
LOST <worker> job=<id> safe_offset=<n> reason=<reason>
RECOVERY_TICK <worker> attempt=<n> action=ping
RECOVERY_ALIVE <worker> result=pong
RECOVERY_STATUS <worker> job=<id> result=<running|done|unknown|ambiguous>
REATTACH <worker> job=<id> range=<start>-<end> safe_offset=<n>
REQUEUE <old_worker> old_job=<id> range=<safe>-<end>
REASSIGN <new_worker> old_job=<id> new_job=<id> range=<safe>-<end>
RECOVERED <worker> state=ready
```

## Failure Handling

- Failed ping: retry on the next recovery deadline.
- Pong but ambiguous job status: retry status later; do not reuse that physical
  worker yet.
- `unknown_job` with missing cache: use existing resumable synchronization.
- Recovery preparation failure: leave the worker offline/recovering and keep the
  shard queued.
- Reassignment start ambiguity: use the existing status-first idempotent start
  reconciliation; do not create another assignment until ordered
  `unknown_job` permits it.
- All remote workers unavailable: process every queued suffix locally.
- User Cancel: stop scheduler ticks, reconcile/cancel known jobs, then close.

## Implementation Boundaries

Pure scheduler transitions and range decisions belong in a small host-testable
module under `main/`, separate from LVGL and UART I/O. `main/main.c` remains the
orchestrator for transport calls, cache preparation, password verification, and
UI updates.

No JanOS source or protocol-version change is planned. If implementation
discovers that `ping`, correlated `status unknown_job`, or responsive `cancel`
is unavailable during a real hardware state, that is a protocol finding and
must stop reassignment on that transport rather than guessing.

## Automated Tests

Host tests must cover:

1. Three misses revoke a lease without losing the confirmed safe offset.
2. No pong schedules another recovery tick without changing coverage.
3. Pong plus running status reattaches the same job and generation.
4. Pong plus terminal found preserves the password for local verification.
5. Pong plus complete `not_found` finishes the shard.
6. Pong plus incomplete `not_found` queues the remaining suffix.
7. Ordered `unknown_job` permits cache preparation and a new generation from
   the confirmed safe offset.
8. Ambiguous status never starts a replacement job on the same worker.
9. An idle different worker can lease a revoked suffix.
10. A late old job cannot reclaim a migrated shard.
11. A late found password remains verifiable.
12. Per-generation accounting never double-adds previous `checked` values.
13. Final local fallback consumes every remaining suffix exactly from the
    ledger's safe offset.
14. Cancel stops recovery and never stores partial work as `not_found`.

Production-path harnesses must exercise the real recovery orchestration for
Grove, USB, and M-BUS. Firmware compilation remains the user's step.

## Hardware Acceptance

1. Restart Grove during active cracking. Observe three misses, `LOST`, recovery
   ping, `unknown_job`, warm-cache preparation, and a new assignment beginning
   at the recorded safe offset.
2. Temporarily interrupt Grove RX/TX without restarting JanOS. Restore the link
   and observe reattachment to the same job rather than a duplicate start.
3. Restart USB and verify CDC reconnection, liveness, cache reuse, reassignment,
   and continued ACK32 operation for later file sync if needed.
4. Restart M-BUS and verify the same state transitions as Grove.
5. Keep one worker offline until another finishes its shard. Verify the idle
   worker steals the queued suffix.
6. Keep all Monsters offline. Verify Tab5 completes every queued suffix locally.
7. Return a retired worker after its range was migrated. Verify cancellation or
   harmless late-result handling with no ownership rollback.

Stage acceptance requires no uncovered byte ranges, monotonic safe offsets,
locally verified remote passwords, and successful completion without rebooting
Tab5.
