# Phase 4.2 - Wardrive / GPS

Status: complete within the simulation scope below. The native-GUI build and full acceptance passed on 2026-09-11. The saved gate report has three successful suites and matches the current static build hashes.

The Wardrive interface now uses the original LVGL render functions from Tab5:
main page, Setup, GPS Debug, Home Networks, MAC Blacklist, Upload menu and
WiGLE/WDGWars provider dialogs with file cards and pagination. The small custom
GPS/session panels and their simulation buttons have been removed. Screen layout,
fonts, sizes, switches and native control labels come from `main/main.c`.

Simulator boundaries supply data and cooperative task behavior. GPS Debug uses
synthetic NMEA; Home Networks and blacklist keep local module-specific entries;
blacklisted Bluetooth identities are filtered by the model. Selected, Sync pending
and Force all uploads maintain independent WiGLE/WDGWars states. No real radio,
credential storage or service requests occur. Advanced radio effects of setup
options are not reproduced; Cleanup/Fix remain explicit Phase 4.3 boundaries.

Screenshots: [Setup](wardrive-native-setup.png), [GPS Debug](wardrive-native-gps.png),
[Upload](wardrive-native-upload.png). These document the browser rendering, not a
physical-device screenshot comparison. Fidelity follows retention of the original
render code, not a claim of pixel comparison against hardware.

The shared model owns acquisition/loss of GPS fix, a deterministic route, distance,
WiFi/BLE identities and counters, separate module sessions, SD capacity, cancellation
and reset. GPS loss creates a gap rather than fabricated fixes. Stop saves once;
Cancel/disconnect discard the running session. Sessions are in memory until reset.

Native UI validation: generator succeeds (309 retained functions, 90 explicit
boundaries). Node model regression: 16/16 passed. Browser acceptance covers native
panels, both upload providers, Home/Blacklist scenario pickers, module lifecycle
and Setup in all four rotations. The final full gate is recorded in the
[verification report](phase4-wardrive-verification.json).

From Windows CMD, repository root, with Docker Desktop running:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
```

After a successful build, quick Wardrive browser check:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\test_emulator_phase4_wardrive.py
```

Full acceptance (includes 4.1 and phases 2/3):

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase4_wardrive.py
```

The gate writes `docs/ui-emulator/phase4-wardrive-verification.json`, including
command results and hashes of the static build. The recorded passing result closes 4.2.
If it fails, keep the output; do not tick the gate checkbox.

Manual preview:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Open http://127.0.0.1:8765/ and refresh after building. Open Wardrive, Start, watch
counts, then Stop to save. Setup opens the real Tab5 settings dialog; Debug opens
GPS Debug. Home networks and MAC Blacklist open their native editors. Upload opens
the native file overview; choose WiGLE or WDGWars, select files or Sync pending.
Upload results use synthetic outcomes; test overrides live outside the Tab5 UI.
Use Full screen in the browser shell to see a larger device preview.
