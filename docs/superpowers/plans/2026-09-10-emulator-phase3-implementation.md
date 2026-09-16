# Stateful Emulator Phase 3 Implementation Plan

> Execute inline with `executing-plans`, in the existing working tree.

**Goal:** Link the accepted scenario to scanning, selected-network clients,
synthetic captures, virtual files and analysis without contradicting identities.

**Architecture:** A deterministic device model owns scenario data, jobs and
virtual SD entries. The C/LVGL interface keeps its production handlers and
parsers. Explicit browser/device adapters deliver model results and never open
hardware. Native synthetic files are generated from the same network/client
records used by the interface.

**Spec:** `docs/Live_Emulator_TODO.md` (Phase 3), the accepted live-emulator design,
`docs/ui-emulator/Accepted_Decisions.md` and the frozen contracts.

The user authorized moving on after Phase 2; its recorded gate passed on
2026-09-10. This is execution of the accepted architecture.

## Constraints

- No physical radio, UART, flash, network capture, external upload or publication.
- Preserving the firmware source is a constraint on convenience, not a reason to
  hide a defect: record what the emulator finds and put the fix to the user.
- SubGHz stays deferred. Use synthetic identities from the versioned seed.
- Preserve the production firmware source and existing working-tree changes.
- Cancellation/reset invalidate outstanding operation IDs; do not fabricate
  successful completion or allow late replies to create files after cancellation.
- UI animation time remains separate from the model clock.

## Task 1 - Shared model and virtual storage

- [x] Validate and load the versioned seed, with independent module state.
- [x] Test scan/selection/client identity links and deterministic reset.
- [x] Implement bounded event scheduling, radio ownership and cancellation.
- [x] Generate a valid synthetic PCAP from the selected network and its clients;
  store it on the matching module's virtual SD only after completion.
- [x] Test empty clients, absent/full SD, cancellation and cross-module isolation.

Evidence: model tests in `tests/test_emulator_device.mjs` and the production-reader
check in `tests/run_emulator_capture_tests.py` pass. See
`docs/ui-emulator/Phase_3_Report.md`. Browser integration is verified below.

## Task 2 - Browser and production interface integration

- [x] Connect the shared model through explicit retained-code boundaries.
  Scan/rows/selection run through `model/bridge.mjs`; Observer boundaries live in
  `runtime/observer.c`, included from `application.c`.
- [x] Retain and validate the selected-network/client/capture UI callbacks.
  The Observer page, table, network popup and attack bar are retained through
  `slice-phase3-observer.json`; `app_bind()` now gives each row its own identity.
  Evidence: `docs/ui-emulator/phase3-verification.json`.
- [x] Connect virtual storage to file listing and the existing PCAP analysis code.
  The virtual SD is mirrored into MEMFS and the production ESPShark TAB5 SD page
  lists and opens the file with the real `pcap_reader`. The rendered summary
  passes after the `%f` fix recorded in `docs/ui-emulator/Firmware_Findings.md`.
- [x] Exercise the whole workflow from real Canvas input, including back routes.
  Scan, Observer, clients, capture and the file listing pass in
  `tests/test_emulator_phase3.py`, including displayed packet counts, duration,
  file size and client endpoints.

## Task 3 - Acceptance

- [x] Verify selected network, clients, capture bytes and displayed analysis agree.
  Network, clients and capture bytes agree, and the production reader opens the
  file and renders the matching summary and packet table.
- [x] Test repeated start/stop, late replies, reset and module switching.
  Four browser lifecycle cases pass; advancing the clock after cancellation does
  not create a file. Both modules retain independent capture paths.
- [x] Re-run the applicable Phase 2 regressions after integration.
  `verify_emulator_phase3.py` runs the whole Phase 2 gate first, on one build.
- [x] Record the exact build and mark Phase 3 complete only after the browser
  workflow gate passes. A standalone model test is not completion of Phase 3.

Completion: `tests/verify_emulator_phase3.py` passed all six suites on 2026-09-11,
including the full Phase 2 gate. The report records matching static build hashes.
The separate LVGL decimal-format regression and refreshed Phase 1 gate also pass.
