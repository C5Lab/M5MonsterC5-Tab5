# Handshake Cracker Stage 0 Transport Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close Stage 0 with reproducible evidence that Grove, USB, and M-BUS synchronize cached inputs, resume interrupted transfers, run together, recover from a lost worker, cancel safely, and persist one verified result.

**Architecture:** Freeze the present transport profile and validate it from host contracts outward to hardware. Synchronization remains sequential; cracking becomes distributed only after preparation. A failed hardware invariant enters a narrow red-green repair loop in the component that owns that boundary, followed by the affected hardware case and the complete host regression gate.

**Tech Stack:** ESP-IDF 5.4.1, FreeRTOS, LVGL, CH34x USB host CDC, JanOS CRACK/1 protocol 4, C host harnesses, Python contract tests, WSL/GCC, SD content-addressed caches.

**Spec:** `docs/Handshake_Crack_Manager_Roadmap.md`; protocol reference: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/docs/crack_worker_protocol.md`

## Global Constraints

- The user compiles and flashes Tab5 and JanOS. The agent does not run an ESP-IDF firmware build.
- JanOS stays at `1.7.5` and CRACK/1 protocol `4` unless hardware evidence proves an incompatibility.
- USB console rate is `115200`; bulk synchronization negotiates `921600`, uses 1024-byte blocks with ACK32/FIN32, then confirms restoration to `115200`.
- USB may fall back to resumable `115200` synchronization only after a confirmed safe protocol boundary.
- Grove and M-BUS retain `2000000`, 8192-byte blocks, byte ACKs, and restoration to `115200`.
- File synchronization stays sequential in Stage 0. Workers crack disjoint ranges concurrently only after preparation.
- Keep three bounded preparation/start attempts. Never add an unbounded retry.
- Resume accepts only a confirmed offset whose prefix CRC matches the local file.
- GPS, dashboard, discovery, and other background readers may not consume USB bytes while CRACK/1 owns CDC.
- Preserve the dirty worktree. Do not stage, commit, reset, clean, or rewrite unrelated files.
- Stage 0 excludes Crack Manager UI, batch cracking, distributed restart journals, quoted `crack_state.csv`, PSRAM wordlist caching, and parallel file synchronization.

---

### Task 1: Freeze the baseline and create the acceptance record

**Files:**
- Create during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read: `main/main.c:5412`
- Read: `main/main.c:58774`
- Read: `main/main.c:60791`
- Read: `main/main.c:61352-62560`
- Read: `main/usb_vcp_config.c`
- Read: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/docs/crack_worker_protocol.md`

**Interfaces:**
- Consumes: the transport profile in Global Constraints.
- Produces: an acceptance matrix with exact evidence, firmware identities, result, and log reference for every case.

- [x] **Step 1: Create the acceptance matrix**

Create `docs/Handshake_Crack_Stage0_Acceptance.md` with these rows:

```markdown
| ID | Scenario | Required evidence | Result | Log reference | Notes |
|---|---|---|---|---|---|
| U1 | USB cold capture | 921600, READY 1024/ACK32, FIN, SYNCED, 115200 restore | pending | | |
| U2 | USB cold 15.6 MB run A | complete without worker loss or fallback | pending | | |
| U3 | USB cold 15.6 MB run B | complete without worker loss or fallback | pending | | |
| U4 | USB warm cache | both inputs present and no data blocks | pending | | |
| U5 | USB interrupted transfer | non-zero offset and matching prefix CRC | pending | | |
| G1 | Grove cold/warm/resume | 2M/8192, cache hit, non-zero resume | pending | | |
| M1 | M-BUS cold/warm/resume | 2M/8192, cache hit, non-zero resume | pending | | |
| D1 | All workers | disjoint coverage and advancing status | pending | | |
| D2 | Lost worker | three misses and safe-offset fallback | pending | | |
| D3 | Cancel | remote cancel and no completed notfound | pending | | |
| R1 | Known password | local verification and exact persistence | pending | | |
```

- [x] **Step 2: Audit frozen constants**

Run:

```powershell
rg -n "JANOS_USB_FT_FAST_BAUD|HS_CRACK_USB_BLOCK_SIZE|HS_CRACK_UART_BLOCK_SIZE|HS_CRACK_STAGE_MAX_ATTEMPTS|finish_linger_ms" main/main.c
```

Expected: USB `921600`/1024 B, hardware UART 8192 B, three attempts, and protocol-4 finish timing.

- [ ] **Step 3: Record firmware identities from the next boot**

Record Tab5 app version and ELF SHA256, JanOS version for each connector, CRACK/1 capability line, and CH34x VID/PID/revision plus 115200 and 921600 register values.

Expected: JanOS `1.7.5` and mandatory protocol-4 tokens. An incompatible worker fails acceptance instead of being silently used.

---

### Task 2: Run the host transport regression gate

**Files:**
- Test: `tests/test_usb_blocking_read_contract.py`
- Test: `tests/test_usb_ack32_upload_contract.py`
- Test: `tests/test_usb_cdc_transfer_config.py`
- Test: `tests/test_usb_fast_baud_contract.py`
- Test: `tests/test_fast_uart_sync_fallback_contract.py`
- Test: `tests/test_usb_probe_recovery_contract.py`
- Test: `tests/test_usb_ready_line_budget_contract.py`
- Test: `tests/test_usb_start_recovery_contract.py`
- Test: `tests/test_worker_stage_retry_contract.py`
- Test: `tests/usb_vcp_config_test.c`
- Test: `tests/hs_crack_remote_core_test.c`
- Test: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/test_crack_worker_job_replay.py`
- Test: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/test_crack_worker_diagnostics.py`
- Test: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/crack_worker_core_test.c`

**Interfaces:**
- Consumes: production functions extracted by existing host contracts.
- Produces: a green baseline before any additional source change.

- [x] **Step 1: Run Tab5 Python contracts independently**

```sh
python3 tests/test_usb_blocking_read_contract.py
python3 tests/test_usb_ack32_upload_contract.py
python3 tests/test_usb_cdc_transfer_config.py
python3 tests/test_usb_fast_baud_contract.py
python3 tests/test_fast_uart_sync_fallback_contract.py
python3 tests/test_usb_probe_recovery_contract.py
python3 tests/test_usb_ready_line_budget_contract.py
python3 tests/test_usb_start_recovery_contract.py
python3 tests/test_worker_stage_retry_contract.py
```

Expected: every command exits zero.

- [x] **Step 2: Compile and run Tab5 C host tests**

```sh
gcc -std=c11 -Wall -Wextra -Werror -I main tests/usb_vcp_config_test.c main/usb_vcp_config.c -o tests/.stage0_usb_vcp_test
./tests/.stage0_usb_vcp_test
gcc -std=c11 -Wall -Wextra -Werror -I main tests/hs_crack_remote_core_test.c main/hs_crack_remote_core.c main/hs_crack_cache.c -o tests/.stage0_remote_core_test
./tests/.stage0_remote_core_test
```

Expected: both exit zero. Remove only these two `.stage0_*` binaries after recording results.

- [x] **Step 3: Run JanOS host contracts**

From `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5` run:

```sh
python3 tests/test_crack_worker_job_replay.py
python3 tests/test_crack_worker_diagnostics.py
```

Compile and run `tests/crack_worker_core_test.c` with the source list documented in that repository. Remove only the temporary binary created for this run.

Expected: idempotent start/replay, diagnostics, cancellation, and safe-offset contracts pass.

- [x] **Step 4: Record commands, exit codes, and test counts**

Any host failure blocks hardware acceptance until Task 8 provides a red-green repair.

---

### Task 3: Qualify USB cold synchronization

**Files:**
- Evidence: user-provided serial logs
- Update during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read on failure: `main/main.c:61352-61650`
- Read on failure: `main/main.c:62380-62560`
- Read on failure: `main/usb_vcp_config.c`

**Interfaces:**
- Consumes: USB CH340/CH341 JanOS 1.7.5, a small capture, and the 15,645,263-byte stress wordlist.
- Produces: U1, U2, and U3 evidence.

- [x] **Step 1: User resets only the selected content identities**

Use size and CRC reported by Tab5:

```text
crack_worker reset capture <size> <crc32-hex>
crack_worker reset wordlist <size> <crc32-hex>
```

Expected: the following probes return `state=missing`. Do not delete unrelated SD data.

- [x] **Step 2: User runs cold small-capture synchronization**

Required sequence:

```text
Local bridge configured: 921600
Console running at 921600
READY off=0 bsize=1024 ... ack_size=32
data/ACK32 for the capture
FIN acknowledged
SYNCED or confirmed terminal boundary
Local bridge configured: 115200
Console back at 115200
WORKER USB stage=capture ... reason=ready
```

Expected: no worker loss, sync failure, or slow fallback.

- [x] **Step 3: User runs cold 15.6 MB pass A**

Capture from `probe ok present=0` through wordlist-ready, start acknowledgement, and five valid status responses.

Expected: full size committed, FIN acknowledged, terminal boundary recovered, 115200 restored, and no local fallback.

- [x] **Step 4: User resets that wordlist identity and repeats pass B**

Expected: a second complete 921600 transfer. Stage 0 does not close on one lucky run.

- [ ] **Step 5: Measure both runs**

Record total bytes, elapsed synchronization time, counts of `transmission=2` and `transmission=3`, fallback count, and representative early/middle/final data-to-ACK times.

Expected: zero worker losses and zero fallbacks. A bounded recovered block retry is recorded but does not by itself fail the run.

---

### Task 4: Qualify USB warm cache and resume

**Files:**
- Evidence: user-provided serial logs
- Update during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read on failure: `main/main.c:61200-61890`
- Read on failure: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.c`

**Interfaces:**
- Consumes: Task 3 verified cache and JanOS `.part.meta` checkpoints.
- Produces: U4 and U5 evidence.

- [x] **Step 1: Run unchanged inputs again**

Expected: capture and wordlist both report `present=1`; no READY or data-block transfer occurs before worker-ready.

- [ ] **Step 2: Create a durable partial transfer**

Reset only the wordlist identity, transfer more than 256 KiB, then power-cycle only the USB JanOS worker while transfer is active.

Expected: Tab5 does not mark the failed worker ready and the completed capture cache remains valid.

- [ ] **Step 3: Restart the same synchronization**

Required sequence:

```text
READY off=<non-zero> ... prefix_crc=<crc>
RESUME USB wordlist at <same offset>/<total> prefix=<same crc>
first ACK32 offset > resume offset
FIN acknowledged
WORKER USB stage=wordlist ... reason=ready
```

Expected: the confirmed prefix is not retransmitted.

- [ ] **Step 4: Exercise prefix mismatch rejection**

Use a changed wordlist identity or a stale partial with another CRC.

Expected: Tab5 rejects the prefix, uses the protocol reset path, and restarts that identity at zero without appending to stale data.

---

### Task 5: Regress Grove and M-BUS

**Files:**
- Evidence: user-provided serial logs
- Update during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read on failure: `main/main.c:62380-62560`
- Read on failure: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.c`

**Interfaces:**
- Consumes: the same capture and wordlist identities as USB.
- Produces: G1 and M1 evidence.

- [ ] **Step 1: Run Grove cold, warm, and interrupted cases**

Expected: `2000000`, 8192-byte blocks, complete cold transfer, `present=1` warm path, non-zero resume, and return to `115200`.

- [ ] **Step 2: Run M-BUS cold, warm, and interrupted cases**

Expected: the same invariants as Grove. The former stray-text reply bytes `0x5B`, `0x43`, and `0x52` must not recur.

- [ ] **Step 3: Record retry and restore counts**

Expected: USB hardening introduced no hardware-UART regression.

---

### Task 6: Qualify all workers, loss recovery, and cancel

**Files:**
- Evidence: user-provided serial logs
- Update during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read on failure: `main/main.c:61930-62340`
- Read on failure: `main/main.c:63740-64070`
- Test on failure: `tests/test_usb_start_recovery_contract.py`
- Test on failure: `tests/test_worker_stage_retry_contract.py`
- Test on failure: `tests/hs_crack_remote_core_test.c`

**Interfaces:**
- Consumes: warm caches on Grove, USB, and M-BUS.
- Produces: D1, D2, and D3 evidence.

- [ ] **Step 1: Start Tab5 plus all three workers**

Record every ASSIGN, ACK, and the first five STATUS lines.

Expected: remote ranges are ordered, non-empty, disjoint, and together with Tab5 cover the remaining wordlist. `checked` and `safe_offset` increase monotonically.

- [ ] **Step 2: Verify preparation UI boundaries**

Expected: `Preparing workers` covers capabilities, cache checks, fingerprints, and synchronization. Password-trying status appears only after cryptographic verification starts.

- [x] **Step 3: Keep the combined job active for at least five minutes**

Expected: a valid response resets its miss counter; no worker is lost before three consecutive misses.

- [ ] **Step 4: Power-cycle one worker during cracking**

Expected: after three misses only `[last safe_offset, shard_end)` enters local fallback. Other workers continue and no gap can be accepted as complete.

- [x] **Step 5: Run a separate cancel case**

Expected: all remote jobs cancel, consoles remain usable, UI exits cleanly, and the partial run is not stored as completed `notfound`.

---

### Task 7: Qualify a known result and persistence

**Files:**
- Evidence: user-provided log and SD output
- Update during execution: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Read on failure: `main/main.c:60590-60780`
- Read on failure: `main/main.c:63900-64110`

**Interfaces:**
- Consumes: a user-owned WPA/WPA2 capture and a small controlled wordlist with the correct password near the end.
- Produces: R1 evidence.

- [ ] **Step 1: Run the controlled list with all workers**

Expected: a worker reports found using hexadecimal fields and Tab5 verifies the candidate locally before acceptance.

- [ ] **Step 2: Verify exact persistence**

Expected: one exact SSID/password row in `/sdcard/lab/handshakes/cracked.txt`, `save_pass: saved|exists` on the source Monster, and a verified completion UI.

- [ ] **Step 3: Repeat the completed case**

Expected: no duplicate local row and no duplicate worker job.

---

### Task 8: Repair only evidence-backed failures

**Files:**
- USB sender/ownership: `main/main.c`, `main/usb_vcp_config.c`, `main/usb_vcp_config.h`
- USB driver, only with driver evidence: `managed_components/espressif__iot_usbh_cdc/iot_usbh_cdc.c`, `managed_components/espressif__iot_usbh_cdc/include/iot_usbh_cdc.h`
- Tab5 lifecycle: `main/hs_crack_remote_core.c`, `main/hs_crack_remote_core.h`, `main/main.c`
- JanOS receive: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.c`, `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_transfer.h`
- JanOS jobs: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker.c`, `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_core.c`
- Test: the matching host contract from Tasks 2 and 6

**Interfaces:**
- Consumes: the complete log around the first failed boundary.
- Produces: one minimal red-green repair, the repeated hardware case, and a repeated full host gate.

- [ ] **Step 1: Classify the first failed boundary**

Use this mapping:

```text
before READY       -> CLI/capability/probe
after READY        -> header/payload transport
CDC rx_done only   -> competing reader/ownership
ACK32 stale/index  -> reply correlation/replay
after FIN ACK      -> FIN_LINGER/terminal boundary
after ACCEPTED     -> idempotent start/status
during STATUS      -> miss accounting/safe offset
```

- [ ] **Step 2: Add an executable host reproduction**

The new test must fail against the extracted production function or receiver core. Use source-string assertions only when executable extraction is impossible.

- [ ] **Step 3: Verify RED, apply the smallest repair, then verify GREEN**

Do not change baud, block size, retry count, or protocol version unless the failing test proves the frozen value incompatible.

- [ ] **Step 4: Run every Task 2 regression**

Expected: focused and full suites pass.

- [ ] **Step 5: Hand off the same hardware case to the user**

The user compiles and flashes. Repeat the failed row first, then the combined-worker smoke test.

---

### Task 9: Clean logging and close Stage 0 documentation

**Files:**
- Modify only after every acceptance row passes: `main/main.c:61400-61650`
- Modify: `tests/README.md`
- Modify: `docs/Handshake_Crack_Manager_Roadmap.md`
- Modify: `docs/USB_ACK32_Debug_Handoff_2026-09-20.md`
- Finalize: `docs/Handshake_Crack_Stage0_Acceptance.md`

**Interfaces:**
- Consumes: a completely passing acceptance matrix.
- Produces: production-level logs and an authoritative Stage 0 closure record.

- [ ] **Step 1: Demote routine success traces**

Change successful per-block data/byte-ACK/ACK32 lines from warning to debug. Keep READY, resume, retry, timeout, DIAG, FIN, rate transition, worker-ready, and transfer summaries visible.

- [ ] **Step 2: Repeat the complete Task 2 host gate**

Expected: log cleanup changes no behavior.

- [ ] **Step 3: Run scoped whitespace verification**

```powershell
git diff --check -- main/main.c main/usb_vcp_config.c main/usb_vcp_config.h tests docs/Handshake_Crack_Manager_Roadmap.md docs/Handshake_Crack_Stage0_Acceptance.md docs/USB_ACK32_Debug_Handoff_2026-09-20.md
```

Expected: no scoped error. Unrelated tracked firmware binaries are not modified.

- [ ] **Step 4: Mark Stage 0 complete only now**

Update the roadmap with verified USB 921600/1024/ACK32/FIN32 plus confirmed 115200 restoration, Grove/M-BUS 2M/8192, and a link to the passing acceptance matrix. Remove the obsolete USB blocker.

- [ ] **Step 5: Review without committing**

Inspect the scoped diff and evidence. Leave every change unstaged and uncommitted for the user.

## Completion Gate

Stage 0 closes only when:

- U1-U5, G1, M1, D1-D3, and R1 are all `pass` with log references.
- Two complete cold USB stress runs finish at 921600 without worker loss or fallback.
- USB, Grove, and M-BUS each prove a warm cache hit and non-zero resume.
- All three remote workers run together with disjoint coverage and monotonic safe offsets.
- One lost worker falls back from its last confirmed safe offset while the others continue.
- Cancel never creates completed `notfound`.
- A remote result is locally verified and persisted exactly once.
- Tab5 and JanOS host suites pass after the last source change.
- The user supplies final compiled-device evidence; the agent does not build firmware.
