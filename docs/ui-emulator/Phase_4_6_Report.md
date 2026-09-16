# Phase 4.6 — Settings, system and module status

Status (2026-09-12): full automated Chromium gate passed on the rebuilt artifact,
including all earlier regression stages: **26 scripts PASS, 0 failures, 126.95 s**.
[Final run summary](test-runs/20260912-003423-29c9ca/summary.json).

## Implemented

- INTERNAL → Module Status: simulator-owned native LVGL cards for Grove/MBus,
  connection state, demo firmware/board label, uptime, reboot count, virtual SD
  use and operation status. Simulate reboot is module-local and preserves SD.
- Monster OTA: native form, keyboard, network picker, channels and monitor with
  cooperative offline jobs. Picker uses the scenario scan; update progress,
  completion, cancellation, failure and disconnect come from the shared model.
  Info/release views are explicitly simulated. No actual firmware is fetched.
- Monster SD Admin: native form, Start/Quick Start/Stop, keyboard and Back dialog.
  A model job reserves the selected module; SD removal, disconnect, reset and
  Stop release it. No AP/HTTP service or functional connection QR is created.
- Dashboard metadata uses virtual module SD use and firmware version, replacing
  hardcoded SD capacity/status. Existing localStorage-backed NVS settings stay
  authoritative; module reboot does not reset browser preferences.
- Seven model tests and five browser cases, plus a gate including full 4.5
  regression. Browser cases cover persistence, isolated reboot, disconnected
  status, SD Admin back/stop/SD failure, OTA success/reopen/failure/cancel.

Credentials stay inside native form memory; jobs, status and logs receive no
passwords. Reset demo still resets the scenario, unlike a module reboot.

## Verification

- 27 model/regression tests passed (device, network tools, attacks and system).
- Three slice tests and one tokenization test passed.
- Generated C: 578 retained functions, 171 explicit boundaries.
- Emscripten syntax check passed. New Python test/gate files compile.
- Full rebuilt-artifact gate passed; five system browser cases passed in 6.99 s.

## Run from the repository root

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_phase4_settings_sys.py -v
tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_phase4_settings_sys.py
```

The final command records `phase4-settings-sys-verification.json` and the shared
section/time summary under `docs/ui-emulator/test-runs/`. The successful run above
is the recorded acceptance evidence; commands are retained for reproduction.

Manual check: open INTERNAL → Module Status, reboot Grove, verify MBus unchanged;
open Settings → Monster OTA, try update and reopen Info; open Monster SD Admin,
Quick Start, Back → Stay here, then Back → Stop and back. Refresh with Ctrl+F5.

## Explicit limits

Ad Hoc Portal & Karma remains a separate unsupported local-server application,
outside this system slice, and must stay visible in the 4.7 coverage audit.
No real device flashing, restart, SD administration or network service occurs.
Physical hardware, other browsers and portrait Nmap accessibility remain open.
