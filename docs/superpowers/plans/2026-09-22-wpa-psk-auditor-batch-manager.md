# WPA PSK Auditor Batch Manager Implementation Plan

> **For Codex:** Execute this plan with the `executing-plans` workflow and use
> test-driven development for each behavior change.

**Goal:** Add a durable multi-capture audit queue to WPA PSK Auditor while
preserving the existing single-audit and distributed worker flows.

**Architecture:** A host-testable `hs_audit_queue` module owns bounded queue
state and A/B persistence. `main.c` adapts catalog selections into queue items,
launches exactly one existing crack coordinator at a time, and receives a
terminal completion event. Existing sessions remain the detailed resume source.

**Tech stack:** ESP-IDF C17, LVGL, FreeRTOS, host C tests and Python contract
tests.

**Spec:** `docs/superpowers/specs/2026-09-22-wpa-psk-auditor-batch-manager-design.md`

## Task 1: Durable queue core

**Files:**
- Create: `main/hs_audit_queue.h`
- Create: `main/hs_audit_queue.c`
- Create: `tests/hs_audit_queue_test.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/check_hs_crack_cmake.cmake`

Write failing tests for queue bounds, state transition guards, progress,
remove behavior, encode/decode CRCs, alternating snapshots, corrupt-slot
fallback and reboot reconciliation. Implement the smallest pure-C API that
passes them. Keep persistence independent of ESP-IDF.

## Task 2: Shared local/remote crack launcher

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_wpa_psk_auditor_contract.py`

Add contract tests that require an explicit local-capture launcher path and a
single completion notification point. Refactor the current remote-only fetch
boundary so local captures feed the same analyzer and distributed worker setup.
Keep Compromised Data behavior unchanged.

## Task 3: Batch coordinator

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_wpa_psk_auditor_contract.py`

Add a persistent batch runtime that loads/reconciles the queue, launches one
item, records completion, and advances only after persistence succeeds. Wire
pause to cooperative cracker cancellation, resume to existing session IDs,
cancel-current to the active item only, and remove to non-running items.

## Task 4: Catalog selection and filters

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_wpa_psk_auditor_contract.py`

Add catalog multi-select, the seven audit-state filters, selection summary,
Select visible, Clear and Add to queue. Derive state from validation, sessions
and history. Auto-sync selected remote-only captures to Tab5, validate after
copy, and queue only stable valid files.

## Task 5: Batch dashboard UI

**Files:**
- Modify: `main/main.c`
- Modify: `docs/WPA_PSK_Auditor_Acceptance.md`

Add the compact queue card, shared wordlist dropdown, aggregate status, queue
rows and action controls using existing Auditor visual primitives. Keep every
transient/disabled/empty/error state legible and preserve the existing Resume,
History and Catalog sections.

## Task 6: Regression and target verification

Run all queue, WPA Auditor, session, history, artifact, capture and distributed
resume host suites in isolated temporary working directories. Compile
`esp-idf/main/CMakeFiles/__idf_main.dir/main.c.obj`, run CMake source checks,
inspect the diff for accidental binaries/unrelated files, and document manual
device tests. Do not build full firmware; the user owns that step.
