# Phase 4.6 — Settings, system and module status

Approved scope: extend the existing native Settings screens with offline system
operations. Work in the current tree; no commits, deployment or real device I/O.

1. Add deterministic module status and update/reboot/SD Admin lifecycle to the
   existing device model. Preserve virtual SD on reboot; cancellation, failure,
   disconnect and reset must not commit an update.
2. Retain/transcribe the native OTA and SD Admin forms; replace their blocking
   transport tasks with cooperative model jobs and explicit simulation labels.
3. Add an emulator-owned native Module Status surface under INTERNAL, using
   existing LVGL styling. Show both modules, version, uptime, SD and operations;
   allow simulated module reboot. Keep existing settings persistence intact.
4. Add browser acceptance and regression gate, retaining prior phase checks.
5. Run targeted lightweight tests and Emscripten syntax checking. User performs
   the full build and browser gate; mark acceptance pending until evidence exists.

Ruling: no separate settings store is introduced. Existing NVS/localStorage
adapters remain the persisted-settings source of truth, tested across reload.
Ruling: module reboot preserves files and local browser preferences; Reset demo
continues to reset the scenario. No real firmware is downloaded or installed.
Ruling: Ad Hoc Portal & Karma is an existing separate application boundary; its
local-server behavior is not an OTA/SD/system task and remains explicitly
unsupported in this milestone, to be accounted for in 4.7 coverage closeout.

Status: implementation and automated Chromium acceptance complete. Full gate:
26 scripts PASS, 0 failures, 126.95 s; see Phase_4_6_Report.md.
