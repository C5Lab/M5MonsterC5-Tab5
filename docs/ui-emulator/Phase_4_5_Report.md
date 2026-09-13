# Phase 4.5 — offline operation screens and Observer activity

Status: automated Chromium acceptance passed on 2026-09-11. The user confirmed the full rebuilt regression run: **24 scripts PASS, 0 failures, 135.10 s**, including Phase 4.4 and 4.5. [Recorded summary](test-runs/20260911-235545-e393be/summary.json). Hardware/cross-browser acceptance and portrait Nmap accessibility remain open.

## Changes

- Original deauth and ARP dialogs/pages with simulated Start/Stop, native host
  selection, progress, cancellation, disconnect and return navigation.
- Original Karma sniffer, probe selection, portal selector and operation popup.
  Probes come from the completed simulated job. Karma state is per module;
  tab changes no longer overwrite it with firmware legacy globals.
- Original Beacon settings, editable SSID list and operation popup. Simulation
  uses the configured SSIDs and rejects an empty list.
- Original Rogue AP, Evil Twin and MITM forms/popups with cooperative simulated
  results. Portal selection includes an explicit Offline demo template. Missing
  SD or disconnected modules show errors instead of throwing into the browser.
- Native MITM Stop writes a genuine target-specific Ethernet/ARP PCAP on virtual
  SD. Cancel, reset and disconnect do not save it; missing/full SD fails atomically.
- Observer preserves scenario network/client identities while changing RSSI,
  simulated packet counts and active/inactive client colors. Updates modify
  existing labels, preserving row identities and scroll position. Stop freezes
  the feed. This passive simulation can coexist with another simulated operation.
- Existing Phase 3 **Simulation: generate PCAP** controls are preserved separately
  from the newly enabled native MITM workflow.
- Scan, Observer and station action routes use simulator adapters. This also
  connects the scan Nmap action that was still an unsupported boundary in 4.4.

These are UI demonstrations. No radio frames are sent, access point started,
remote service called, password acquired or real network attacked. Native UI
code is either retained directly or copied into explicit adapters to remove
transport loops and move legacy global state into per-tab context.

## Verification performed

- 37 Node model/bridge tests pass, including six new attack/Observer cases.
- Three production-slice tests and one tokenization regression pass.
- Generator: 554 retained source functions and 154 explicit boundaries.
- Emscripten syntax-only check passes without warnings for generated browser C.
- Final attack/Observer browser suite: 11/11 passed in 13.35 s; the full 4.5 regression gate passed in 135.10 s.
- Scoped review findings were fixed: station control identities and return routes,
  portal loading exceptions, per-tab Karma state, popup ownership and keyboard closure.

## Local commands

From the repository root with Docker Desktop running:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase4_attacks.py
```

The gate runs 4.4 and all earlier gates before the new checks. Detailed results
are saved in `docs/ui-emulator/phase4-attacks-verification.json`; nested suites
can be quiet for a while. Existing Node, Playwright Chromium and WSL/cc
requirements apply. To run only the new browser suite after rebuilding:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\test_emulator_phase4_attacks.py
```

Start the preview in a separate terminal:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Open http://127.0.0.1:8765 and press Ctrl+F5 after rebuilding.

## Manual walkthrough

1. **WiFi Scan & Attack:** select a scenario network and use Deauth, ARP,
   RogueAP, Evil Twin or MITM. AFTERLIFE-GUEST is an open-network demo target.
   Native action visibility follows Red Team settings (enabled by default here).
2. **Karma:** Start Sniffer, wait, Stop Sniffer, select a returned scenario SSID,
   choose the demo portal and Start Karma. STOP ends the simulation.
3. **Global WiFi Attacks → Beacon Spam:** inspect/edit List SSIDs, Start Spam,
   then Stop. Try deleting the entire list to see validation.
4. **Network Observer:** Start directly; it discovers scenario networks and clients
   independently of WiFi Scan ([follow-up](Observer_Independent_Report.md));
   packet counters, RSSI and client activity change. Stop freezes the cached data.
5. Try Grove/MBus independently and repeat Start/Stop. MITM Stop leaves its file
   path visible; open the file through local PCAP analysis.

Unlisted actions such as SAE, Handshaker, Blackout, Portal and Rogue GITM remain
explicit boundaries. This phase does not claim every inventoried control is
implemented; the final control/navigation audit is 4.7. Prior 4.3 limitations,
physical-device validation and cross-browser checks remain open.
