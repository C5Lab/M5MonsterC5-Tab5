# Distributed Worker Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded per-worker retries/reporting on Tab5 and idempotent start plus health-rich status on JanOS 1.7.5.

**Architecture:** Tab5 owns a three-attempt stage policy and never retries an unsafe USB stream. JanOS classifies start requests against the retained job identity, replays acceptance without spawning duplicate work, and reports its phase/progress age as optional CRACK/1 status tokens.

**Tech Stack:** C11/C17 host tests, ESP-IDF C integration, CRACK/1 protocol 4.

**Spec:** `docs/superpowers/specs/2026-09-20-worker-reliability.md`

## Global Constraints

- Keep JanOS and the Tab5 required version at exactly `1.7.5`; keep CRACK protocol 4.
- Run host tests only. Do not compile or flash firmware.
- Preserve both dirty worktrees; do not commit, stage, reset, clean, or modify binary artifacts.
- A stage has at most three top-level attempts. Existing USB ACK32 data/FIN retransmissions remain independently bounded by their wire protocol.

### Task 1: Add portable retry and start-classification contracts

**Files:**
- Modify: `main/hs_crack_remote_core.h`
- Modify: `main/hs_crack_remote_core.c`
- Modify: `tests/hs_crack_remote_core_test.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_core.h`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker_core.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/tests/crack_worker_core_test.c`

- [x] Add failing tests for the three-attempt/backoff policy and optional STATUS health parsing.
- [x] Add failing JanOS tests for new, replay, conflict, and busy start classifications.
- [x] Implement the smallest portable helpers and run both core host tests green.

### Task 2: Implement JanOS idempotent start and health status

**Files:**
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker.c`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/docs/crack_worker_protocol.md`
- Create or modify: JanOS production contract test under `tests/`

- [x] Test production behavior by compiling/extracting the relevant decision/format path where practical.
- [x] Retain job identity, classify start twice (before and after file validation), and replay acceptance without task creation.
- [x] Track the current phase and last safe-progress timestamp; append `phase` and `progress_age_ms` to STATUS.
- [x] Verify version remains `1.7.5` and run JanOS host regressions.

### Task 3: Implement Tab5 stage reporting and bounded retries

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_fast_uart_sync_fallback_contract.py`
- Create: `tests/test_worker_stage_retry_contract.py`
- Modify: `tests/README.md`

- [x] Add worker stage/attempt/reason fields and one UI/log publishing helper.
- [x] Make capabilities, file sync, and start use three top-level attempts with 250/1000 ms backoff.
- [x] Remove the nested USB probe retry; reuse the existing terminal recovery gate before retrying sync.
- [x] Keep Grove/M-BUS fast attempt followed by 115200 attempts without repeatedly switching baud.
- [x] Show runtime status misses as 1/3, 2/3, then lost/fallback; clear them on a correlated response.
- [x] Run focused production-contract tests and all Tab5 host regressions.

### Task 4: Cross-repository audit and handoff

- [x] Verify start identity, accepted replay fields, status health fields, retry counts, delays, and fallback semantics match across repositories.
- [x] Run the focused crack/worker host suites and record exact results. The unrelated legacy `run_uart_transfer_tests.py` harness remains stale against pre-existing transfer API changes.
- [x] Request independent code review, address findings, and hand off expected hardware log markers. Do not build firmware.
