# Tab5 Deauth Defence and Forensics Implementation Plan

> **For agentic workers:** Execute inline in the approved current checkout. Track each checkbox, use strict red-green TDD, and do not commit or push.

**Goal:** Add the complete Tab5 Defence controller contract and optional bounded Deauth Forensics model/UI with production-emulator-browser parity.

**Architecture:** Extend the existing extracted detector watch state machine for Defence, while keeping forensic parsing, storage, aggregation, filtering, and rendering in an independent fixed-capacity model. Feed both status protocols before normal event parsing so neither can enter history, CSV, sounds, alarms, or V2 counters.

**Tech Stack:** C11, ESP-IDF/FreeRTOS, LVGL, Node.js ESM, Python unittest, Playwright/browser emulator

**Spec:** `docs/superpowers/specs/2026-11-01-tab5-deauth-defence-forensics-design.md`

## Global Constraints

- Do not modify JanOS or Cardputer ADV.
- Do not build or flash firmware, publish artifacts, commit, or push.
- Preserve exact passive/Trace behavior except where the approved exact-final-Stop rule supersedes the old partial acknowledgement.
- Keep every new implementation, test, and document string in English.
- Host/emulator evidence must not be described as firmware, UART, RF, SD-card, or flashed-device proof.

---

### Task 1: Defence production model and grammar

**Files:**
- Modify: `tests/detector_watch_logic_test.c`
- Modify: `tests/test_detector_watch_logic.py`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: existing `detector_watch_status_payload`, strict MAC helpers, and captured watch session.
- Produces: `detector_defence_state_t`, strict Defence marker parser, Defence counters/state, and exact mode command builder.

- [x] Add failing literal tests for normal and PMKID commands, selection requirements, no active legacy fallback, every valid Defence record, malformed/duplicate/late records, TX success/error counting, release summaries, stopped totals, and exact final Stop.
- [x] Run `python -B -m unittest discover -s tests -p "test_detector_watch_logic.py" -v` and confirm the new assertions fail because Defence fields/functions are absent.
- [x] Add the minimal fixed-size Defence state and strict parser to `main/main.c`; consume Defence markers before event parsing and require exact `All operations stopped.`.
- [x] Re-run the production model suite until all Defence and existing V2/Trace assertions pass.

### Task 2: Defence captured transport and firmware UI

**Files:**
- Modify: `tests/detector_transport_binding_test.c`
- Modify: `tests/detector_watch_logic_test.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: captured tab/port/link generation and selected scan generation.
- Produces: frozen Defence/PMKID controls, confirmation overlay, bound start/stop/retry behavior, and visible uncertainty.

- [x] Add failing Grove/USB/MBus tests proving mode, selection, port, visible-tab independence, USB generation, reconnect generation, write failure, timeout, partial Stop, and exact final Stop behavior.
- [x] Run the watch suite and confirm failures describe the missing bound Defence behavior.
- [x] Implement Defence and PMKID Lab controls, explicit confirmation copy, strict command creation, unsupported state, and bound Stop retry.
- [x] Re-run watch/transport tests and repair only behavior covered by failing assertions.

### Task 3: Defence checkpoint and regression gate

**Files:**
- Modify as required by failures only.

**Interfaces:**
- Produces: a green V2 + Trace + Defence baseline before Forensics begins.

- [x] Run production watch and history suites, `node --test tests/test_emulator_detectors.mjs`, and the current detector browser acceptance suite.
- [x] Diagnose every regression using the failing boundary; add a focused failing test before any production fix.
- [x] Record exact checkpoint results and do not begin Forensics until all available gates are green.

### Task 4: Strict Forensics parser and bounded production model

**Files:**
- Create: `tests/detector_forensic_test.c`
- Create: `tests/test_detector_forensics.py`
- Modify: `main/main.c`

**Interfaces:**
- Produces: `detector_forensic_parse_line`, `detector_forensic_accept`, retained row model, burst/sequence evidence, and bounded summaries.
- Consumes: exact single-prefix normalization and the confirmed captured detector session.

- [x] Add failing table-driven tests with hand-written literals for valid boundaries/unknowns and every malformed grammar class.
- [x] Add failing tests for capacity/eviction/saturation, 20,000,000 us burst boundary, key changes, modular deltas, retry/repeated labels, unknown aggregation, reason/RSSI/identity summaries, and the 23-row fixture.
- [x] Run `python -B -m unittest discover -s tests -p "test_detector_forensics.py" -v` and confirm missing parser/model failures.
- [x] Implement the smallest independent parser/model needed for the tests, using a 128-row fixed capacity and saturating arithmetic.
- [x] Re-run the forensic suite and refactor only after green.

### Task 5: Forensics ownership, isolation, and Stop lifecycle

**Files:**
- Modify: `tests/detector_forensic_test.c`
- Modify: `tests/detector_transport_binding_test.c`
- Modify: `tests/deauth_detector_test.c`
- Modify: `tests/detector_history_test.c`
- Modify: `main/main.c`

**Interfaces:**
- Consumes: active captured detector session and selected BSSIDs.
- Produces: reset-on-confirmed-Start, preserved uncertain summary, final freeze, and late/stale/cross-session rejection.

- [x] Add failing tests for pre-arm, cross-tab, cross-port, stale scan/USB generation, post-final Stop, interleaved V2/Trace/Defence/CSA, and unsupported firmware.
- [x] Add failing isolation assertions for history, CSV, latest observations, counters, sound, and alarm paths.
- [x] Implement lifecycle gating and process forensic lines before normal event parsing.
- [x] Re-run forensic, watch, history, and event parser suites until green.

### Task 6: Forensics LVGL UI

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_deauth_detector_v2_browser.py`

**Interfaces:**
- Consumes: retained forensic model and filter state.
- Produces: summary card, evidence view, four filters, neutral evidence rows, note, and three empty states.

- [x] Add failing browser acceptance for summary values, labels, filters, unsupported/no-row/all-filtered states, isolation from history/alarm, and portrait/landscape geometry.
- [x] Implement the summary and internal evidence view with existing theme/layout patterns and unchanged DEAUTH/CSA cards.
- [x] Run the focused browser tests and fix all functional/geometry failures in one bounded pass.

### Task 7: Emulator and JS parity

**Files:**
- Modify: `tools/ui_emulator/model/device.mjs`
- Modify: `tools/ui_emulator/model/bridge.mjs`
- Modify: `tools/ui_emulator/runtime/detectors.c`
- Modify: `tests/test_emulator_detectors.mjs`
- Modify: `tests/test_deauth_detector_v2_browser.py`

**Interfaces:**
- Produces: exact Defence command/status simulation and deterministic Forensics fixtures on captured transports.

- [x] Add failing Node tests for Defence sequences and every required Forensics fixture, including the 23-row long-run sample, malformed/late/cross-generation rows, overflow, uncertainty recovery, and no-marker firmware.
- [x] Implement deterministic device/bridge streams without RF claims or unbounded queues.
- [x] Extend browser acceptance for captured transport, interleaving, filters, empty states, responsive layout, and normal-history isolation.
- [x] Run Node and browser suites until green.

### Task 8: Documentation, parity inventory, review, and final verification

**Files:**
- Modify: `docs/deauth-detector-v2.md`
- Modify: `tools/ui_emulator/README.md`
- Modify: parity/coverage inventory files only when repository checks require them.

**Interfaces:**
- Produces: exact public grammar, ownership, statistics, evidence limits, compatibility behavior, and validation boundary.

- [x] Document exact Defence and Forensics contracts and the absence-compatible state.
- [x] Run the emulator parity/coverage audit required by the repository.
- [x] Review the full diff against both source prompts, run `git diff --check`, and run the complete requested production/Node/browser verification commands fresh.
- [x] Report changed files, exact results, unresolved risks, and separate host/emulator/browser evidence from pending firmware/UART/RF/JanOS-marker validation.

## Execution record

- Host production extraction: 4/4 Defence/Forensics/history/transport tests passed; the forensic parser/model test ran with ASan/UBSan. Existing DEAUTH/CSA parser regression: 1/1 passed.
- JS device/bridge/model parity: 35/35 passed.
- Browser: 23/23 V2/Trace/Defence/Forensics, 10/10 selected-watch, and 12/12 history/header/CSA/Phase 4 detector regressions passed.
- Emulator parity: native WebAssembly build passed; slice 3/3, Phase 4 audit 15/15, and token regression 1/1 passed. Inventory resolved 541/541 callback sites with zero syntax diagnostics.
- Review: two read-only review passes completed; all Critical/Important findings were resolved before the final verification.
- Boundary: no ESP-IDF firmware build, physical UART, radio, SD-card hardware, flash, publication, commit, or push was performed. Current JanOS still does not emit `[DEAUTH_FORENSIC/1]`.
