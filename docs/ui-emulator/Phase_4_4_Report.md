# Phase 4.4 — network tools handoff

Status: automated Chromium acceptance passed on 2026-09-11. The user confirmed the full rebuilt regression run: **24 scripts PASS, 0 failures, 135.10 s**, including Phase 4.4 and 4.5. [Recorded summary](test-runs/20260911-235545-e393be/summary.json). Hardware/cross-browser acceptance and portrait Nmap accessibility remain open.

## Implemented

- **Nmap:** original connect/password, discovered-host, scan-level and results
  screens. Cooperative simulation supplies two documentation-range hosts and
  HTTP port results, with native Quick/Medium/Heavy settings.
- **Mesh Recon (IoT):** original PAN cards, expanded topology, node tracking and
  Start/Stop/Clear controls, fed by synthetic `[ZIG]` parser input.
  [Continuous monitoring follow-up](Mesh_Monitor_Report.md) replaces the initial
  one-shot result with gradual discovery, live counters and working Stop.
- **GITM:** original upstream scan/pick/connect, AP setup, live counters,
  stop/exit controls and copy-to-viewer flow. Stop stores a genuine 14-packet
  synthetic PCAP on virtual SD; cancellation/disconnect does not save a file.
- **wpa-sec:** original network picker, manual/hidden SSID, password/keyboard
  and upload dialog. The result describes two offline demo files, not uploads
  of actual user files. Success/failure/partial outcomes are simulated.
- Shared job lifecycle keeps Grove and MBus isolated, rejects busy/disconnected
  modules, and invalidates cancelled/reset jobs. GITM storage checks are atomic.
  Passwords are excluded from model jobs and reports.
- Native Global WiFi and Handshakes navigation reaches these tools. Other Global
  WiFi actions remain explicit Phase 4.5 boundaries.

The original nmap UI/parser code is transcribed into an emulator adapter to replace
its global state with per-tab context. IoT and most GITM/wpa-sec render functions
are retained directly; transport/task portions are adapters. No real network,
access point, radio operation or remote service is invoked.

## Checks run

- 31 Node model/bridge checks passed, including six new network-tool cases.
- Three production-slice tests and one new literal-tokenization regression passed.
- Generator: 536 retained functions and 110 explicit boundaries.
- Emscripten syntax-only check passes for generated browser C.
- Final Network Tools browser suite: 10/10 passed in 10.07 s; full 4.4 gate passed in 121.53 s including prior phases.
- Review fixes cover cross-tab binding ownership, native nmap scan counts,
  GITM filename validation and repeated menu/control identities.

## Run locally

From the repository root, with Docker Desktop running:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase4_nettools.py
```

The gate includes 4.3 and prior phases and writes
`docs/ui-emulator/phase4-nettools-verification.json`. Nested suites may be quiet
for a while. Existing requirements apply: Node, Playwright Chromium, WSL with `cc`.
For only the new browser suite while troubleshooting:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\test_emulator_phase4_nettools.py
```

Preview:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Open http://127.0.0.1:8765 and refresh with Ctrl+F5 after building.

1. **WiFi Scan & Attack → select AFTERLIFE-GUEST → Nmap:** connect, list hosts,
   pick a host and run Quick. Nmap/GITM require the native Red Team visibility
   setting (enabled by default in the emulator).
2. **Mesh Recon:** Start, inspect the PAN/node, then Clear or Stop.
3. **Global WiFi Attacks → GITM:** scan, choose AFTERLIFE-GUEST, connect, enter
   an AP SSID, Start, Stop, then Copy to open the capture in the PCAP viewer.
4. **Compromised Data → Handshakes → Send to wpa-sec:** select a scenario
   network and Connect to see the offline result. Close cancels pending work.

The Phase 4.3 manual file-dialog checks and Wardrive JSON Fix limitation remain.
This handoff does not establish browser-layout, cross-browser or hardware acceptance.
