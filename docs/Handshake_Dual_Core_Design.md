# Optional dual-core dictionary checking

Status: implemented and firmware build passed on 2026-09-13 with ESP-IDF 5.4.1
for ESP32-P4. Device validation remains pending; no flashing was performed.
The design below describes the rationale; the implementation details immediately
below take precedence where the original proposal differs. Native C validation
was subsequently authorized and passed: 6 backend and 11 verifier tests, including
the reported password, cancellation and concurrent callers. No device speedup
has been measured yet.

## Implemented behavior

- The dialog offers Single (initial default) and Dual before Start. Selection is
  remembered for the application session and frozen while running.
- Single pins one worker to CPU1; Dual pins one worker to each core. Workers have
  priority 3 and 8,192-byte stacks. The coordinator has priority 4 and 12,288 bytes.
- CPU0 uses approximately 19 ms slices followed by at least 1 ms blocked; CPU1
  uses 49 ms and at least 1 ms. Checkpoints occur every 32 PBKDF2 iterations.
  Higher-priority tasks can always preempt workers. This is an approximate budget.
- A private, namespaced copy of the installed mbedTLS software SHA-1 avoids the
  shared SHA peripheral. HMAC inner/outer states are prepared once per password
  and cloned. No heap allocation occurs inside the KDF loop. Sensitive temporary
  states are wiped. Existing firmware TLS configuration remains unchanged.
- Each candidate reuses PMKs across records with the same SSID. MIC checks remain
  record-specific; distinct SSIDs require separate PMK derivations.
- A four-entry job queue and four-entry result queue connect workers to the
  coordinator. Jobs carry passwords and sources, without sequence numbers.
  Source transitions drain pending work, preserving SSID → built-in → SD priority.
  Within-source completion order is unspecified; first received match wins.
- Only the coordinator publishes progress. Tried counts completed candidates;
  throughput and ETA use aggregate completions and verification wall time.
- Cancel, match and error set an atomic stop flag. The coordinator waits for all
  worker exit notifications before freeing shared data. No worker touches LVGL.
- Allocation failure cleans up partial startup and suggests retrying Single.
- New logs report mode/workers, then `completed`, `core` and candidate microseconds.
  They contain no passwords. Compare Single/Dual using the same lab capture and
  dictionary, and check repeated cancel/restart, RAM and watchdog behavior.
- Windows IDF include paths are normalized before ESP-IDF component registration.

The old device logs measured about 11.05 seconds per candidate, over 99.9% inside
PBKDF2, and reported IDLE1 watchdog starvation. They are baseline evidence only.
Run the synthetic crypto vectors and the device acceptance checks below after
building; source review does not establish runtime correctness or speed.

## Findings from the original single-worker source

- `hs_crack_start_file()` creates one `hs_crack_task` at priority 5.
- The LVGL port defaults to priority 4 with no fixed core affinity.
- Main and ESP timer tasks are configured for CPU0.
- The FreeRTOS tick is configured at 1,000 Hz.
- The checker yields after each candidate. This is insufficient as a responsiveness
  boundary when one candidate takes approximately 11 seconds on the observed device.
- SHA hardware acceleration is enabled. ESP-IDF 5.4.1's SHA-1 implementation
  acquires a shared hardware lock around operations, with DMA setup and cache
  synchronization. Two workers cannot use that single accelerator simultaneously.
- The reported CPU0 1% / CPU1 100% is consistent with one CPU-bound worker. It does
  not establish whether parallel hardware-SHA calls will improve throughput.

## Original user-facing design rationale

Choose **Single** or **Dual** before starting verification. Default to Single until
device comparisons establish the value of Dual. Freeze the selection while running.

| Mode | Worker placement | Scheduling target |
| --- | --- | --- |
| Single | One worker pinned to CPU1 | Use available CPU1 time, with idle/watchdog opportunities |
| Dual | One worker pinned to each core | CPU0 checker budget at most approximately 95%; CPU1 uses available time |

The CPU0 reservation is a minimum scheduling allowance, not a promise that the
entire system will consume exactly 5%. Higher-priority system tasks can always
preempt the workers and may require more than 5%. A worker cap also does not
guarantee that the CPU telemetry will show 5% idle: system tasks can consume the
released time.

## Scheduling and PBKDF2 checkpoints

Give both workers priority 3, below the existing LVGL priority 4. Do not raise the
workers above essential system tasks to make the utilization bars look full.

The current monolithic `mbedtls_pkcs5_pbkdf2_hmac_ext()` call needs a checkpointable
equivalent built from mbedTLS HMAC operations. Preserve PBKDF2's exact two-block,
4,096-iteration calculation and verify identical PMKs and MIC results. Retain the
HMAC context, current U value, XOR accumulator, iteration index and output block
between checkpoints; never restart the computation when yielding.

For CPU0, use measured short processing slices, approximately 19 ms of work followed
by about 1 ms blocked as an initial 95/5 target. Measure actual slice and blocked
durations rather than assuming exact tick timing. Account for preemption and lock
waits when tuning the budget. Long-running primitive calls limit checkpoint latency;
the budget is a best-effort cap, not a hard real-time partition.

Use a blocking delay, not just `taskYIELD()`: yielding alone does not ensure that a
lower-priority idle task gets time. CPU1 must also block periodically for idle-task
cleanup and watchdog service. Check cancellation at the same short boundaries.
Never sleep while holding a hardware crypto lock, queue mutex or display lock.

## Work distribution and state ownership

Keep one coordinator responsible for source enumeration, SD reads and UI/result
publication. Workers receive bounded queue entries containing the candidate, its
source and sequence number. Each worker owns its cryptographic contexts and timing
counters. The current shared PBKDF2 timing fields must become worker-local before
parallel verification is enabled.

Preserve source ordering strictly:

1. Enumerate and finish all SSID candidates.
2. Drain in-flight work before entering the built-in list.
3. Drain that phase before reading SD candidates.

Within a phase, candidates can finish out of order. The first reported match stops
new work; result arbitration must publish exactly one result with the matching
candidate's source and record SSID. A later queued candidate must not overwrite it.

Count completed candidates, not queue submissions, in Tried and ETA. Compute
combined throughput from both workers' completions over the same elapsed interval.
For the current-candidate display, show each worker's candidate or identify the
last completed one; a single producer-side `Now` value becomes misleading.

On cancel/error/match, stop producing, request cooperative worker shutdown and wait
for both workers before freeing shared records, queues or the run state. Avoid
force-deleting a worker that could own an mbedTLS hardware lock. Never wait for
workers while holding the LVGL lock.

## Memory feasibility

The supplied screen showed roughly 43 KiB of free internal byte-addressable heap
and 16 KiB of free DMA-capable heap. Those pools overlap. Blindly adding two
12-KiB worker stacks would consume approximately 24 KiB before queue and task
metadata, leaving limited headroom.

Size stacks from measured high-water marks. Place bounded candidate buffers and
read-only capture records in PSRAM where supported, while preserving the stack
requirements of the installed ESP-IDF configuration. Check both free capacity and
largest allocatable blocks. If the complete Dual allocation fails, release partial
allocations and report that Dual is unavailable; do not leave one worker running
or silently label a Single run as Dual.

## Backend comparison before claiming a speedup

Compare the same synthetic, nonmatching capture and candidate set in four cases:

| Case | Purpose |
| --- | --- |
| Single + hardware SHA | Current-backend baseline |
| Dual + hardware SHA | Quantify shared-accelerator contention |
| Single + software SHA | Measure short-message CPU hashing without peripheral setup |
| Dual + software SHA | Measure actual independent work on the two cores |

A software backend should be isolated to the checker; do not globally disable
hardware SHA for TLS, Wi-Fi and unrelated features just to test this workload.
Use maintained cryptographic code, not an unvalidated replacement hash function.

Record candidates/s, PBKDF2 time, per-core load, UI/Cancel response latency, RAM/DMA
headroom and stack high-water marks. Select the backend using measured useful
throughput, not CPU busy percentage. Hardware acceleration is not automatically
faster for thousands of tiny HMAC messages, but that remains a hypothesis until
the comparison is run on this device.

## Acceptance criteria

- Identical known-vector PMK/MIC results in Single and Dual.
- No duplicated or skipped candidates, with source-phase barriers preserved.
- Accurate completed-count/ETA semantics and matching-source attribution.
- Responsive UI and cooperative cancellation during PBKDF2.
- No watchdog resets or memory leaks on repeat start/cancel, match, error or popup close.
- Partial allocation/task-start failures return to a usable state.
- Measured CPU0 budget is approximately the requested cap when workers are busy;
  system demand is allowed to reduce cracker utilization further.
- Publish measured speedups only after the device comparison.

## References

- [ESP-IDF 5.4.1 FreeRTOS on ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32p4/api-reference/system/freertos_idf.html): affinity, fixed-priority scheduling and cooperative task cleanup.
- Project: `main/main.c`, `sdkconfig`, `components/espressif__esp_lvgl_port/include/esp_lvgl_port.h`.
- Installed ESP-IDF: `components/mbedtls/port/sha/dma/esp_sha1.c` and `sha.c` for shared hardware locking and DMA/cache operations.
