# Mesh Recon continuous monitoring

Focused verification: **22 cases passed, 3 scripts, 10.63 seconds** on the rebuilt
artifact ([report](test-runs/20260912-203804-0fc3eb/summary.json)). Run again with
`.\tools\ui_emulator\.venv\Scripts\python.exe tests/verify_emulator_mesh.py`.
The broader network-tools run passed 8 cases but its 4 wpa-sec cases timed out
during browser startup with localhost connection resets; that run is not a pass.

The emulator previously completed its IoT job after 2,000 simulated milliseconds,
published one fixed PAN and disabled Stop. Firmware `iot_recon_monitor_task`
instead polls while monitoring is enabled.

The offline job now remains running until Stop, Clear, Back, cancellation,
disconnect or reset. At the default 10x timing, one synthetic Zigbee PAN appears
after approximately 2 seconds and a second after 5 seconds. Each has a coordinator,
an end device and a link. Packet counters increase as monitoring continues.
Realistic 1x timing uses 20 and 50 seconds respectively.

Native PAN cards and node details use the existing parser. Expanded PAN state
survives refreshes. Stop preserves results; Start resets discovery and counters;
Clear stops and removes results. Back stops monitoring and leaves a stopped
status when reopening. The active job reserves only its own module.

Validation commands:

```powershell
node --test tests/test_emulator_device.mjs tests/test_emulator_nettools.mjs
.\tools\ui_emulator\.venv\Scripts\python.exe tests/test_emulator_mesh_monitor.py
.\tools\ui_emulator\.venv\Scripts\python.exe tests/test_emulator_phase4_audit.py
```

The Mesh browser tests cover all four rotations, normal wall-clock discovery,
expanded results across updates, Stop/frozen results, restart, Clear, Back/reopen,
disconnect and shell cancellation. They route the built Wasm bytes through
Playwright to isolate native acceptance from intermittent localhost HTTP resets.
The tests are included in `test_emulator_phase4_nettools.py` and the full rollup.
