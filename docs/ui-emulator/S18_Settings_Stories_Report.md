# S18 — Scan Setup and Red Team stories

Local implementation, 2026-09-13. Focused native browser acceptance: **PASS**.

The **More guided stories** selector contains four S18 variants:

| Variant | Native evidence required |
| --- | --- |
| Scan Setup save | Open form, change GROVE timing values and vendor switch, Save, reopen with retained values, complete a fresh GROVE Scan |
| Scan Setup validation and cancel | Submit an invalid GROVE range, observe native validation, Cancel, reopen with original timing values, return to Settings |
| Red Team cancel | Begin disabled, open disclaimer, Cancel, inspect Test/Tests menu names, complete fresh Scan with ARP/Nmap and without Deauth/Evil Twin |
| Red Team accept | Begin disabled, open disclaimer, confirm, inspect Attack/Attacks menu names, complete fresh Scan with Deauth/Evil Twin actions |

The scan story alternates its requested timings when 200/500 are already saved, preventing Cancel from being credited as Save. Show Vendors is an immediate native setting: Cancel only discards timing edits. The portable module retains these settings; synthetic scan duration follows the browser simulation speed, not radio dwell timing. The guide does not claim a radio measurement or a change to the synthetic vendor fixture labels.

Steps observe the INTERNAL or GROVE tab appropriate to that step. Hidden documents, stale epochs, wrong modules and old completed scan jobs cannot advance the guide. Restarting with an existing Scan Setup dialog requires closing and reopening it, so an old validation error cannot count as a fresh invalid submission. Completion restores the story menu through **Back to stories**. This implementation did not modify firmware source or deploy the emulator.

Validation:

- `node --test tests/test_emulator_settings_s18.mjs` — **6 tests PASS**, including repeated-story stale Save, existing validation error on restart, wrong module, hidden document, old epoch and restricted action bar regressions.
- `.\tools\ui_emulator\.venv\Scripts\python.exe tests/test_emulator_settings_s18.py -f` — **5 methods PASS in 29.675 seconds**. Four variants run at each of four rotations (16 paths). Additional native cases check interruption/restart, rejection of old validation feedback, and Cancel after editing timings that differ from the saved settings.
- Every native path launches the actual story picker and clicks canvas controls. Completed variants expose **Back to stories**, and all methods check for native unavailable events and browser errors.

The full regression gate is owned by the integration report; this focused report does not claim that gate has passed. Acceptance uses Chromium and the portable emulator, not a physical Tab5.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
