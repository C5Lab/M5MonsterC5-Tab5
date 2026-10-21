# WPA PSK Auditor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zbudować wspólny fundament WPA PSK Auditor: strukturalny preflight capture, trwałe rozproszone Resume, historię, dwa wejścia UI oraz kompatybilną inwentaryzację JanOS.

**Architecture:** Tab5 pozostaje źródłem prawdy dla walidacji i ledgeru audytu. JanOS zachowuje `CRACK/1 protocol=4` i dodaje niezależne `ARTIFACT/1`; starsze firmware działają przez fallback. Jeden silnik obsługuje szybkie wejście oraz dashboard.

**Tech Stack:** ESP-IDF 5.4.1, C17, LVGL, FatFS/SD, FreeRTOS, istniejące hostowe testy C/Python/CMake.

**Spec:** `docs/superpowers/specs/2026-09-21-wpa-psk-auditor-design.md`

## Global Constraints

- Nie kompilować firmware; dozwolone są wyłącznie testy hostowe i statyczne walidacje.
- JanOS pozostaje w wersji rozwojowej `1.7.5` do czasu merge'a i zachowuje `CRACK/1 protocol=4`.
- Invalid capture pozostaje na dysku i w katalogu z kodem powodu.
- Awaria transportu nigdy nie oznacza `invalid`.
- Jeden aktywny lub resumowalny audyt naraz; batch jest poza zakresem.
- `not_found` wymaga pełnego pokrycia ledgeru; duplikaty pracy są dozwolone, luki nie.
- Dotychczasowe `Crack`, `Crack latest`, cache wyników i worker recovery pozostają kompatybilne.
- Testy powstają przed kodem produkcyjnym i każdy nowy test musi najpierw wykazać oczekiwaną porażkę.

---

### Task 1: Structured capture qualification on Tab5

**Files:**
- Create: `main/hs_capture_analyzer.h`
- Create: `main/hs_capture_analyzer.c`
- Create: `tests/hs_capture_analyzer_test.c`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/check_hs_crack_cmake.cmake`

**Interfaces:**
- Produces: `hs_capture_analyze_pcap()`, `hs_capture_reason_name()`, `hs_capture_report_t`, and validated HCCAPX records for the existing verifier.
- Consumes: `pcap_reader` sequential iteration and the current record-validation rules.

- [x] Write fixtures and failing host tests for ready M1+M2, no EAPOL, unmatched AP nonce, malformed EAPOL, unsupported key version, truncated PCAP, unsupported link type, cancellation, and missing SSID.
- [x] Run the isolated host test and confirm failures are caused by the missing analyzer API.
- [x] Move the parser into the new transport/UI-independent module and return stable structured reason codes plus evidence counters.
- [x] Run the analyzer test and existing crypto/cache/parser tests.
- [x] Replace the late generic `nrecs <= 0` branch with the preflight report; prove invalid inputs create no active session or worker start.

### Task 2: Durable distributed session codec and store

**Files:**
- Create: `main/hs_crack_session.h`
- Create: `main/hs_crack_session.c`
- Create: `tests/hs_crack_session_test.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/check_hs_crack_cmake.cmake`

**Interfaces:**
- Produces: `hs_session_encode/decode`, `hs_session_load_latest`, `hs_session_save_next`, `hs_session_tombstone`, and versioned payload types containing local and remote shard snapshots.
- Consumes: capture/wordlist identities and scheduler owner/state enums.

- [x] Write failing round-trip, bounds, malformed TLV, corrupt header/payload/trailer, and A/B highest-valid-sequence tests.
- [x] Run the isolated test and confirm the missing codec/store is the failure.
- [x] Implement a field-wise versioned codec; never persist raw C padding or pointers.
- [x] Implement alternating slots using injected filesystem paths so host tests exercise real files.
- [x] Run session, scheduler, cache and CMake registration tests.

### Task 3: Persist and restore the distributed ledger

**Files:**
- Modify: `main/hs_crack_scheduler.h`
- Modify: `main/hs_crack_scheduler.c`
- Modify: `main/main.c`
- Modify: `tests/hs_crack_scheduler_test.c`
- Create: `tests/test_distributed_resume_contract.py`

**Interfaces:**
- Consumes: Task 2 snapshots.
- Produces: reserved deterministic job IDs, persisted `STARTING` assignments, local-shard accounting, checkpoint callbacks, resume reconciliation and idempotent finalization.

- [ ] Write failing scheduler/runtime tests for persist-before-start, restore/reattach, unknown-job suffix generation, local shard restoration, stale identity rejection, and idempotent finalization.
- [ ] Run the targeted tests and confirm each fails for the intended missing behavior.
- [ ] Add an explicit persisted reservation state and deterministic session/shard/generation job IDs.
- [ ] Integrate checkpoints at safe transitions and periodic drained local progress; remove the `!distributed` checkpoint gap.
- [ ] Implement Resume reconstruction and finalization replay while preserving current worker-recovery invariants.
- [ ] Run all crack scheduler, remote-core, recovery, cache and new distributed-resume tests.

### Task 4: Audit history and presentation model

**Files:**
- Create: `main/hs_audit_history.h`
- Create: `main/hs_audit_history.c`
- Create: `tests/hs_audit_history_test.c`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Produces: append-only immutable history records and presentation structs for active/resumable cards.
- Consumes: terminal or interrupted `hs_crack_session` snapshots.

- [x] Write failing tests for found/not-found/cancelled/stale/error records, exact CSV/text escaping, missing RTC, ordering sequence, corrupt tail recovery and bounded listing.
- [x] Run the isolated tests and confirm expected failures.
- [x] Implement history writes through unique temporary files and terminal rename; preserve existing `crack_attempts.csv` as result cache.
- [x] Add idempotent history creation keyed by session ID.
- [x] Run history, cache and finalization tests.

### Task 5: JanOS ARTIFACT/1 inventory foundation

**Files (repo `C:/Users/mati/Documents/GitHub/projectZero`):**
- Create: `ESP32C5/main/artifact_inventory_core.h`
- Create: `ESP32C5/main/artifact_inventory_core.c`
- Create: `ESP32C5/tests/artifact_inventory_core_test.c`
- Create: `ESP32C5/tests/test_artifact_inventory_contract.py`
- Modify: `ESP32C5/main/main.c`
- Modify: `ESP32C5/main/CMakeLists.txt`
- Modify: `ESP32C5/CMakeLists.txt`
- Modify: `ESP32C5/tests/test_crack_worker_diagnostics.py`
- Modify: `ESP32C5/tests/test_crack_worker_job_replay.py`
- Modify: `ESP32C5/docs/crack_worker_protocol.md`

**Interfaces:**
- Produces: `artifact_inventory capabilities/list/inspect/cancel`, `[ARTIFACT/1]` records, safe scoped paths, paging, CRC fingerprinting and HCCAPX reason codes.
- Preserves: `CRACK/1 protocol=4`; advertises additive `artifact_inventory=1`.

- [x] Write failing pure-C tests for strict parsing, scope confinement, paging, filename hex encoding, request correlation, numeric overflow, HCCAPX reasons, cancellation and proof that inspection never deletes source bytes.
- [x] Run JanOS host tests and confirm intended failures.
- [x] Implement the pure core protocol and bounded data structures without FreeRTOS/LVGL dependencies.
- [x] Wire CLI handlers with one terminal `END` per accepted request, progress/cancel, low-priority yielding and a dedicated non-destructive cache.
- [x] Keep the unmerged JanOS development version at `1.7.5` while leaving `CRACK/1 protocol=4` unchanged.
- [x] Run the new and existing JanOS host tests; do not build firmware.

### Task 6: Tab5 ARTIFACT/1 client and merged catalog

**Files:**
- Create: `main/hs_artifact_inventory.h`
- Create: `main/hs_artifact_inventory.c`
- Create: `tests/hs_artifact_inventory_test.c`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: JanOS `ARTIFACT/1` and legacy `list_dir -s`.
- Produces: merged `audit_capture_asset` records, exact/provisional identity, source badges and sync eligibility.

- [x] Write failing fragmented-stream parser tests for BEGIN/ITEM/RESULT/END, bad hex, stale request IDs, missing terminal records, pagination, old-JanOS fallback and duplicate/provisional merge rules.
- [x] Run targeted tests and confirm failures come from the missing client/catalog.
- [x] Implement strict line parsing and bounded catalog structures independent of LVGL.
- [x] Add serialized discovery for LOCAL/GROVE/USB/MBUS without interleaving file transfer.
- [ ] Reuse resumable transfer for `Sync to Tab5`, perform final local validation and retain invalid material with its report.
- [ ] Run inventory, UART transfer, capture analyzer and cache tests.

### Task 7: WPA PSK Auditor UI and shared launch flow

**Files:**
- Modify: `main/main.c`
- Create: `tests/test_wpa_psk_auditor_contract.py`
- Modify: `docs/Handshake_Crack_Manager_Roadmap.md`
- Modify: `docs/Handshake_Dictionary_Check.md`
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: catalog, preflight report, active session/history and existing worker-row rendering.
- Produces: main-menu tile, dashboard, capture details, Resume/Start over and shared launch adapter used by legacy Crack/Crack latest.

- [ ] Write failing UI/runtime contract tests for both entry flows, singleton enforcement, invalid capture diagnostics, active/resume card, history summary and navigation back to a running audit.
- [ ] Run the contracts and confirm missing UI integration is the failure.
- [ ] Add the `WPA PSK Auditor` tile and page-independent status/source/history components using existing theme helpers.
- [ ] Route legacy file actions and Crack latest through the shared capture-detail/preflight/session adapter.
- [ ] Add `Resume`, confirmed `Start over`, `Stop & preserve progress`, history details and the merged catalog actions without batch controls.
- [ ] Run UI contracts, host emulator checks applicable to the modified surfaces, all crack tests and static CMake checks.

### Task 8: Cross-repo validation and acceptance documentation

**Files:**
- Modify: `docs/Handshake_Crack_Stage0_Acceptance.md`
- Create: `docs/WPA_PSK_Auditor_Acceptance.md`
- Modify: `tests/README.md`
- Modify: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/docs/crack_worker_protocol.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: reproducible host gate and exact hardware log checklist for user compilation/flashing.

- [x] Run the full allowed Tab5 host gate and record exact commands/results.
- [x] Run the full allowed JanOS host gate and record exact commands/results.
- [x] Run `git diff --check` in both repos and inspect scoped diffs for accidental binary or unrelated changes.
- [x] Document hardware tests for reset/recovery, invalid PCAP, old firmware fallback, all transports, sync resume and A/B power-cut recovery.
- [x] Perform a final cross-repo protocol/version review; firmware compilation remains explicitly pending for the user.
