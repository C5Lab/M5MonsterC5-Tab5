# WPA PSK Auditor Multi-Session Resume Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve and expose multiple independently resumable WPA audit sessions.

**Architecture:** Add a filesystem catalog around the existing A/B session codec, then route cracking and the WPA PSK Auditor dashboard through that catalog. An optional UI-selected session ID makes dashboard Resume exact while the legacy flow continues to choose the newest exact identity match.

**Tech Stack:** C17, ESP-IDF/FatFS, LVGL, Python source-contract tests, host GCC tests with ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-09-21-wpa-psk-auditor-multi-resume-design.md`

## Global Constraints

- Do not compile firmware; the operator performs target builds.
- Preserve the legacy global A/B checkpoint as a readable migration source.
- Never discard another active session when starting a new audit.
- `Start over` and terminal completion affect only the selected/current session.
- Keep JanOS protocol and version unchanged.

---

### Task 1: Multi-session filesystem catalog

**Files:**
- Create: `main/hs_session_catalog.h`
- Create: `main/hs_session_catalog.c`
- Create: `tests/hs_session_catalog_test.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: `hs_session_load_latest`, `hs_session_save_next`, `hs_session_tombstone`.
- Produces: `hs_session_catalog_save`, `hs_session_catalog_list`, `hs_session_catalog_tombstone`, and `hs_session_catalog_find_id`.

- [x] Write a host test creating two session IDs, saving both, listing both, loading each by ID and tombstoning only one.
- [x] Run the test and confirm compilation fails because `hs_session_catalog.h` is absent.
- [x] Implement hexadecimal per-session paths, A/B save/load, legacy inclusion, de-duplication and newest-first bounded listing.
- [ ] Run the catalog and existing session tests under ASan/UBSan.

### Task 2: Runtime catalog integration

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_distributed_resume_contract.py`

**Interfaces:**
- Consumes: catalog APIs from Task 1.
- Produces: exact selected-session resume and newest exact-match resume for the legacy flow.

- [x] Add failing integration assertions that checkpointing uses the catalog and starting a non-matching audit does not tombstone the previous session.
- [x] Run the Python contract and confirm the catalog assertions fail.
- [x] Replace global-slot load/save/tombstone calls with catalog lookup scoped by optional `resume_session_id`.
- [x] Run the distributed-resume contracts and strict RISC-V catalog syntax gate. Native catalog runtime remains pending with the ASan/UBSan item above.

### Task 3: Multi-session dashboard

**Files:**
- Modify: `main/main.c`
- Modify: `tests/test_wpa_psk_auditor_contract.py`

**Interfaces:**
- Consumes: `hs_session_catalog_list` and exact target selection from Task 2.
- Produces: up to eight compact resumable rows with per-row Resume and Start over.

- [x] Add failing source-contract assertions for an eight-entry session array and callbacks receiving a row index.
- [x] Run the dashboard contract and confirm the new assertions fail.
- [x] Render per-session metadata and actions, preserve offline/busy disabled states, and free catalog memory on close/reopen.
- [x] Run the dashboard contract and the UI detector once.

### Task 4: Documentation and regression verification

**Files:**
- Modify: `docs/WPA_PSK_Auditor_Acceptance.md`
- Modify: `docs/Handshake_Crack_Manager_Roadmap.md`
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: completed storage/runtime/UI behavior.
- Produces: manual device acceptance steps for two simultaneous resumable audits.

- [x] Document start A, cancel A, start B, cancel B, resume A, then resume B without lost offsets.
- [x] Run the available WPA Auditor, distributed-resume, worker-stage, stack and crypto contracts. Hardware and native ASan/UBSan acceptance remain pending.
- [x] Inspect scoped `git diff --check` and ensure no unrelated user changes were overwritten.
