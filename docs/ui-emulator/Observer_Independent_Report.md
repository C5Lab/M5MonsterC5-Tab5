# Independent Network Observer

The native emulator adapter previously copied WiFi Scan's cached networks and
selection. With no scan, it passed an empty target list to the model and instructed
the user to scan first. This was an adapter limitation, not the intended flow.

Start now requests the model's own scenario inventory: 12 networks and their
associated clients. The native rows read Observer state directly. RSSI, packet
counters and client activity continue updating until stopped. No scan results or
selection are populated or changed by starting Observer. Grove and MBus retain
separate Observer state.

Explicit selection and synthetic capture can use Observer-discovered targets
without requiring a WiFi Scan. Stop, restart, exit confirmation, module switching,
popup capture cancellation, file generation and reset remain covered.

Verification: **6 scripts PASS, 0 failures, 15.90 s**, including model tests,
all four rotations, lifecycle tests, activity, Phase 3 capture/analysis acceptance
and the coverage audit. [Run report](test-runs/20260912-204300-62976a/summary.json).

```powershell
.\tools\ui_emulator\.venv\Scripts\python.exe -u tests/verify_emulator_observer.py
```

The native acceptance tests serve the same built Wasm bytes through Playwright
routing. The initial direct-HTTP lifecycle attempt had three startup timeouts
with localhost connection resets; it was not a passing run. HTTP transfer itself
is not validated by the routed tests. Data remains an offline scenario fixture.
