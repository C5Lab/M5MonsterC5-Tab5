# Native dialog completion — 2026-09-12

Implemented the eight remaining open coverage controls without changing production
firmware. The browser still renders the native LVGL dialogs and retained callbacks;
only hardware work is adapted to the offline model.

- Wardrive Home confirmation appears when a configured Home SSID/BSSID occurs in
  observed WIFI rows, auto-upload is enabled, and a demo provider is armed. No
  dismisses once per saved Home entry per recording. Yes stops the recording,
  saves its virtual session, and simulates uploads for the configured providers.
  Starting again resets prompt eligibility. Offline provider credentials are
  implicit fixtures; no real API keys or requests are required.
- Missing-board and missing-SD conditions set actual model module flags. Missing
  board opens the native confirmation at initialization and Continue Anyway
  dismisses it. Missing SD enters the retained warning through the Wardrive tile;
  Cancel leaves home, Continue opens Wardrive. The missing-board adapter states
  accurately that selecting Normal in the shell reconnects the offline demo.
- Version mismatch shows the actual demo firmware version versus the native
  required version. OK dismisses; Monster OTA changes tabs and opens native OTA.
- OTA Info contains both retained partition cards. Activation starts a simulated
  reboot, disables activation while pending, and commits the slot/version only
  when the model reboot completes. Cancel, disconnect, or closing the monitor
  retains the prior image. Info can reopen and switch back; updates refresh the
  currently running slot image. The two repeated card bindings use slot identity.

Files: runtime/application.c, runtime/dialogs.c, runtime/wardrive.c,
runtime/wardrive_lists.c, runtime/settings_ota.c, model/device.mjs,
slice-phase5-dialogs.json under tools/ui_emulator; tests/test_emulator_dialogs.py
and tests/test_emulator_dialogs.mjs. Generated browser manifest/artifact refreshed
by the standard build. Root owns the shell condition selector and final ledger.

Verification actually run:

- Before implementation: native condition suite had 3 expected assertion failures
  against the prior Wasm (missing board, SD warning, version mismatch absent).
- Model tests first failed twice (systemActivateSlot and slots absent), then passed.
- `node --test tests/test_emulator_dialogs.mjs tests/test_emulator_settings_sys.mjs
  tests/test_emulator_wardrive.mjs`: 17 passed, zero failures.
- Final `powershell -NoProfile -File tools/ui_emulator/build.ps1`: success;
  653 retained functions, 191 explicit boundaries. Three pre-existing observer
  string/preprocessor warnings remain.
- Final native dialog suite: 5 tests passed in 19.274 seconds, each exercising all
  four rotations. Includes Home No/Yes/restart, disconnected flags, SD Cancel and
  Continue, both mismatch actions, slot success/cancel/reopen/switchback.
  Tests assert no page errors or unsupported-boundary events.
- Screenshots: docs/ui-emulator/dialogs-evidence contains five dialog views for
  each rotation. Viewed missing-board portrait plus SD, version, Home, OTA landscape.
  Native Home dialog retains its original elevated placement (top outline slightly
  clipped by parent in landscape; title, question, and buttons remain visible).
  Retained SD warning text still mentions HTML portal files on Wardrive, matching
  the production shared dialog. These production layout/copy details were preserved.

Failures found and fixed during verification: missing repeated OTA binding entity,
Info cards covering its summary/log text (fixed flex column ordering), initial
no-board text advertising a removed hardware retry loop. Test navigation corrected
Settings to INTERNAL > Settings; cancellation uses native Close because shell
Cancel follows the current INTERNAL module rather than OTA's Grove target.

Audit comparison (root verified the exact delta and updated the baseline): 452 controls unchanged;
covered 345 -> 353, retained 257 -> 263, adapter 88 -> 90, not-wired 101 -> 93,
open 8 -> 0. Unsupported 6 and deferred 93 unchanged. Exactly the eight requested
IDs disappear from open_items; no new IDs. No commits, publishing, physical
hardware changes, or durable virtual SD/story progress were introduced.

Final integrated focused acceptance: 18 cases / 4 scripts PASS in 39.13 s
([report](test-runs/20260912-211801-f03ebf/summary.json)). The cumulative run
stopped on an unrelated localhost Wasm startup timeout after 22 passing scripts
([record](test-runs/20260912-211423-66ac5d/summary.json)).
