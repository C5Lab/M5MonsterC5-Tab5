# Global WiFi follow-up — 2026-09-12

Blackout, global Handshaker and SnifferDog now open their retained native
confirmation and active screens from Global WiFi Attacks. Yes starts an offline
model job; No closes confirmation; native STOP completes the job and returns Home.
Busy or disconnected modules reject the start without disturbing an existing job.
Cancellation, disconnect and reset stop simulated activity. Global Handshaker
shows bounded synthetic counts and the last scenario SSID. No radio effects or
handshake files are produced.

Implementation: `slice-phase4-global-attacks.json`, `runtime/global_attacks.c`,
the Global WiFi action adapter, shared application lifecycle and attack model.
Owner deletion cancels the associated job and clears native popup pointers.

## Verification

The Docker/Emscripten build completed successfully. The focused gate passed
11 Node cases and seven Chromium cases in 23.77 seconds:
[run summary](test-runs/20260912-173702-55d17d/summary.json).
The browser cases cover all three operations through Yes/No/STOP in four
rotations, busy and disconnected refusal, shell cancellation, reset/reopen,
and independent Grove/MBus global Handshaker results and STOP. Three SAE
browser cases also pass as regression checks. Model tests verify module isolation
for every new operation and deterministic, bounded handshake results.

All five coverage audit tests pass. Exactly nine controls changed from not-wired
to covered: 328 covered (246 retained, 82 adapters), six unsupported and 118
not-wired out of 452. No controls or back routes are uncategorized/uncovered.
The obsolete global-radio-popup deferral rule was removed.

```powershell
./tools/ui_emulator/build.ps1
./tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_global_attacks.py
```

The Phase 4 roll-up includes this gate. This focused result does not re-accept the
entire cumulative suite: earlier cumulative runs encountered intermittent local
Wasm transfer resets during browser startup. Hardware and cross-browser acceptance
remain separate. Owner-deletion cleanup was reviewed in code; no dedicated browser
deletion case was added.
