# Time settings — 2026-09-12

The native Settings > Time popup now edits a virtual RTC. Set validates calendar
dates and updates the top-bar clock; Close discards unsaved roller edits. The
retained format/visibility controls persist immediately. Manual DST shifts the
virtual clock by one hour exactly once per toggle, including date rollover.

The clock uses civil date/time fields independently of automatic timezone/DST
rules. It advances at wall-clock speed, independently of accelerated radio jobs.
A persisted millisecond offset lets it continue across reloads. Without a saved
offset it uses the computer's local time; reset clears the offset and preferences.
No physical RTC or host system clock is changed. Storage fallback follows the
existing SettingsStore behavior.

`slice-phase5-time.json` retains the native popup and most callbacks;
`runtime/clock.c` replaces the RTC boundary and status clock. Each repeated switch
gets a distinct binding identity. `web/persistence.mjs` validates the clock keys.

## Acceptance

Build succeeded. The focused gate passed seven persistence cases, three Chromium
cases and five coverage audit cases in 10.30 s:
[summary](test-runs/20260912-180256-e0b1b8/summary.json).
Browser tests exercise actual roller changes, Set/Close, reload, four rotations,
clock progression, invalid February dates, leap-day DST rollover, 12-hour format,
visibility and reset. No unavailable events or page errors occurred.

Time tests supply identical built Wasm bytes through Playwright routing because
direct local HTTP transfers intermittently reset. This result validates native
behavior and persistence, not the HTTP startup path.

The audit changes by exactly three control templates: 331 covered, six unsupported,
115 not-wired, 12 open candidates and zero uncategorized controls. A template used
by three switches counts once in this static ledger.

```powershell
./tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_time.py
```

The full Phase 5 settings gate includes this check. The user's earlier full run
passed 36 scripts before this Time change; that result is historical evidence,
not full acceptance of the new build. Transfer Speed and INTERNAL Ad Hoc Portal
& Karma remain separate unsupported flows.

Additional general settings regression: five of six browser cases passed; one
failed before emulator initialization with `Failed to fetch` for Wasm. Evidence:
`tools/ui_emulator/time-settings-regression.log`. No full regression PASS is claimed.
