# Browser Emulator Phase 2 Implementation Plan

> **For agentic workers:** Execute inline with executing-plans; independent build infrastructure may be delegated. Work in the current working tree as requested.

**Goal:** Begin Phase 2 with a reproducible browser build of the production Home, Settings and Scan interface, then close the complete phase only with browser acceptance evidence.

**Architecture:** Extract original C functions with source provenance and an explicit slice policy. Compile repository LVGL/fonts to WebAssembly. A Canvas shell supplies input and elapsed time; bounded device jobs feed production parsing and row construction.

**Tech Stack:** C, repository LVGL, Emscripten, Python/tree-sitter, JavaScript and browser automation.

**Spec:** `docs/Live_Emulator_TODO.md`, `docs/ui-emulator/Control_Adapter_Contracts.md`, `docs/ui-emulator/Accepted_Decisions.md`.

## Constraints and rulings

- Preserve firmware source and all pre-existing working-tree changes.
- No physical device, RF operation, firmware flashing, publication or external upload.
- SubGHz is deferred. Documentation is English.
- No implicit successful stubs. Unimplemented destinations must be explicit in the prototype and coverage report.
- Production scanning has no cancel button or cancellation flag. Prototype cancellation is an explicit simulator control and adapter operation; it must not be described as an original firmware callback.
- Demo job time and LVGL animation time are separate; timing changes only while idle.

## Task 1: Build and portable browser runtime

Files: `tools/ui_emulator/CMakeLists.txt`, `Dockerfile`, `build.ps1`, `runtime/runtime.{c,h}`, `web/`, `tests/test_emulator_input.mjs`.

- [x] Test scaled logical coordinates, pointer boundaries and cancelled pointer state with independent literal expectations.
- [x] Implement RGB565 Canvas presentation, pointer/key events and an elapsed-time LVGL loop.
- [x] Pin Emscripten and build repository LVGL/fonts without pthreads.
- [x] Compile and load the output through a local static HTTP server.

Interface: `emu_init(rotation)`, `emu_tick(elapsed_ms)`, `emu_pointer(x,y,pressed)`, `emu_key(key)`, `emu_width()`, `emu_height()`, `emu_framebuffer()`; C application hooks `app_init()` and `app_tick(elapsed_ms)`.

## Task 2: Production interface slice

Files: `tools/ui_emulator/prepare_browser.py`, `slice-policy.json`, `runtime/application.c`, generated source/manifest (ignored).

- [x] Extract retained functions and required declarations with original source locations.
- [x] Emit explicit unsupported boundaries; fail if a required decision is missing.
- [x] Initialize connected Grove/MBus/Internal contexts and production Home/Settings/Scan navigation.
- [x] Execute timed scan responses through production parsing and row construction.
- [x] Verify completion, cancellation, restart and context isolation in browser tests.

## Task 3: Phase acceptance and evidence

Files: browser tests, `docs/ui-emulator/Phase_2_Report.md`, `docs/Live_Emulator_TODO.md`.

- [x] Exercise navigation and scan in all rotations, scrolling and text input at different CSS sizes.
- [x] Verify stable concrete bindings, original Settings controls and asynchronous continuations against the frozen contracts.
- [x] Record startup/download/frame/memory measurements and desktop/mobile budgets with device limitations.
- [x] Update tracker precisely; leave unchecked any item without evidence. Do not close Phase 2 on a rendered screenshot alone.

## Execution notes

The existing renderer accepts unstarted tasks and removes callbacks. Its host adapters are not reused. The original architecture is already accepted; the user's request authorizes implementation. Build tooling is independent of application extraction and can proceed concurrently.

## Resume checkpoint ? 2026-09-09

Build and navigation/scan subset verified in place. Eight Chromium cases pass
(four rotations, two CSS viewports), alongside 13 input/slice/contract checks.
Scan restart is covered; full application restart remains unverified. Key/text
bridges exist but do not yet have end-to-end acceptance evidence. Task 3 and
the full Phase 2 gate remain open. See `docs/ui-emulator/Phase_2_Report.md`.

The user subsequently confirmed that the manually launched local preview works
and looks good. Preview startup instructions are recorded in the Phase 2 report.
Next implementation priority is remaining Settings controls and restart behavior,
then touch/drag, text input and navigation during scans. This checkpoint does
not close Phase 2 or begin Phase 3.

## Completion checkpoint - 2026-09-10

All tasks passed `tests/verify_emulator_phase2.py` on a single static build.
The gate and matching functional/performance reports are under
`docs/ui-emulator/`. Physical mobile and other browsers remain Phase 6 work.
The user authorized proceeding to Phase 3 after this gate passes.
