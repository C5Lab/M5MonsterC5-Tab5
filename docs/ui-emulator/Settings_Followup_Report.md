# Transfer Speed and Observer exit — 2026-09-12

Settings > Transfer Speed now retains the firmware popup, dropdown callback and
rate mapping. All nine firmware rates are supported, with 460800 as the default.
The settings store persists the value immediately, rejects unsupported rates,
recovers invalid records and clears the choice on reset. Disk denial uses the
existing session fallback. The native selection is restored on reload. No
physical UART baud changes or measured throughput are simulated.

Back from a running Observer now shows the retained confirmation. Keep running
closes the dialog while preserving the session. Stop and exit stops the offline
Observer and returns Home. The lifecycle regression now follows this explicit
confirmation before reopening the Observer.

## Verification

Build succeeded. Focused gate passed **16 cases** (eight persistence, two Transfer
Speed browser, one Observer-exit browser, five audit) in 15.79 s:
[run summary](test-runs/20260912-193522-4e60fb/summary.json).
Native selection/reload/reset and both confirmation choices were checked in all
four rotations. The new browser cases serve identical built Wasm bytes through
Playwright to avoid intermittent local transfer resets.

Separately, all four `test_emulator_lifecycle.py` cases passed on this build in
8.485 s using the regular HTTP harness. Evidence:
`tools/ui_emulator/observer-exit-regression.log`. No full cumulative PASS is claimed.

Exactly four control templates moved to covered. Current audit: 345 covered
(257 retained, 88 adapters), six unsupported, 101 not-wired out of 452; eight open
candidates, 93 deferred, zero uncategorized controls or uncovered back routes.

```powershell
./tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_settings_followup.py
```

The full Phase 5 settings gate includes this follow-up. Remaining dialog work:
Wardrive Home, no-board, SD-warning, version mismatch and OTA slot controls.
