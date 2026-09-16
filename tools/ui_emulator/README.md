# Tab5 browser emulator

The emulator runs a retained slice of the real C/LVGL interface in WebAssembly,
with synthetic networks, clients and an in-memory SD card. No device is required.
See [progress and scope](../../docs/Live_Emulator_TODO.md).

## Windows: build and run manually

Run commands in Windows Command Prompt from the repository root:

```bat
cd /d C:\Users\mati\Documents\GitHub\M5MonsterC5-Tab5
```

Start Docker Desktop and wait for its engine to be ready. Build once, and again
after changing sources:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
```

The build uses the pinned Docker compiler image and requires the project's
managed LVGL component in `managed_components/lvgl__lvgl`. It writes static assets
to `tools/ui_emulator/dist`. Docker is only needed to build, not to serve them.

Start the preview:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Leave that terminal open, then visit **http://127.0.0.1:8765/**. Refresh the page
after rebuilding. Ctrl+C stops the server. If port 8765 is occupied, use 8766
in both the command and URL. A refused connection means the server is not
listening there; keep the foreground server running.

If the virtual environment is absent, an installed Python can serve the build:

```bat
py -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Do not run `py tools\ui_emulator\.venv\Scripts\python.exe`: that asks Python to
interpret an executable as source and produces the Non-UTF-8 syntax error.
The Docker image's pip is independent of Windows Python; its Dockerfile must not
use the unsupported `--break-system-packages` option.

## Try the synthetic capture workflow

1. On Grove, open **Network Observer** directly; a WiFi Scan is not required.
2. Choose **Start** to discover the scenario networks and their clients.
3. Open the NEON-BAZAAR row. The popup shows its three scenario clients.
4. Choose **Simulation: generate PCAP**, then **Generate synthetic PCAP**.
5. After completion, choose **Open local file analysis**, then **OPEN** beside
   `synthetic-1.pcap`. The summary and packet table come from the production reader.

This fixture contains three Ethernet/ARP packets (198 B), not radio traffic or
authentication data. Grove and MBus files use separate directories. **Cancel /
Close** during generation discards the pending capture. **Reset demo** reloads
the scenario and clears virtual files and saved settings. Reload also discards
virtual files; virtual SD has no persistent storage.

## Try Bluetooth discovery and locate (Phase 4.1)

1. On Grove, open **Bluetooth**, then **BT Scan & Locate**.
2. The list contains six synthetic BLE devices. Tap one to see its name, MAC
   address and changing RSSI, then use Back to return to the list.
3. **Rescan** reads the scenario again. Grove and MBus keep separate discovery
   snapshots. A disconnected model module returns no devices and an active
   locator displays **No signal**.

All four screen rotations are covered. AirTag scanning and the nRF24 jammer
remain visibly unsupported; this workflow uses no physical Bluetooth radio.

## Guided Bluetooth story and condition dialogs

Choose **Start Bluetooth story** beside the device. Follow discovery on Grove,
select NEURODECK-07, watch three distinct signal readings and return to the list.
The guide advances from native device events; there is no Next button.
**Restart story** clears guide progress only. **Free exploration** hides the guide
without changing the device. Reload/reset clears story progress.

**Demo module conditions** lets you restart with no external boards, missing SD
or a JanOS version mismatch. These expose the native warnings and their actions.
Return the selector to **Connected · SD available** for the usual demo; Reset
also restores that condition. Wardrive's home confirmation appears when a saved
home network is detected with auto-upload enabled. Monster OTA **Info** exposes
the simulated boot-slot cards.

Focused acceptance:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_guided_dialogs.py
```

## Wardrive / GPS (Phase 4.2, verified)

The verified slice adds Wardrive recording, the original Tab5 **Setup**, **GPS Debug**, **Home Networks**, **MAC Blacklist**
and **Upload** dialogs. Data and upload results remain simulated.
Rebuild after changing sources to update the preview. Build, acceptance
commands and the manual walkthrough are in the [Phase 4.2 handoff](../../docs/ui-emulator/Phase_4_2_Report.md).

## Test setup and verification

Phases 4.3–4.5 add deep PCAP/file views, network tools, native simulated operation
screens and Observer activity. Automated Chromium gates through 4.5 passed on
2026-09-11: 24 scripts PASS, 0 failures, 135.10 s. See the
[4.5 report](../../docs/ui-emulator/Phase_4_5_Report.md). Hardware/cross-browser
acceptance and portrait Nmap accessibility remain open; next implementation is 4.6.
After rebuilding, use **Load PCAP examples** or the native tool menus.

For a fresh test environment with Python installed:

```bat
py -m venv tools\ui_emulator\.venv
tools\ui_emulator\.venv\Scripts\python.exe -m pip install -r tools\ui_emulator\requirements.txt
tools\ui_emulator\.venv\Scripts\python.exe -m playwright install chromium
```

After building:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase4_attacks.py
```

This runs the previous Phase 4.4 gate (including all earlier stages),
plus offline operation/Observer model checks and native browser acceptance.
Node.js is required for
model tests; native reader/format regressions require WSL with `cc` on Windows.
Reports are saved in `docs/ui-emulator/` with hashes of the tested static assets.

Each `verify_emulator_*.py` run now ends with one plain-text `TEST SUMMARY`,
grouped by phase, with script PASS/FAIL/ERROR status and elapsed seconds.
Wrapper times include nested checks; the total is wall-clock time, not their sum.
The counters describe scripts, not individual unittest/Node test cases.
Existing fail-fast behavior remains: after a failure, later checks may not run
and must not be treated as passing. Normal Ctrl+C records an interrupted check;
forcibly killing the process cannot produce a final summary.
Each run writes `summary.txt`, `summary.json`, and per-command logs into a unique
directory under `docs/ui-emulator/test-runs/`. Output disables child-process colors
and strips ANSI color/title sequences so tests do not change the console style.
No emulator rebuild is required for this reporting change.


## Phase 4.6 ? system handoff

Module Status, simulated OTA/reboot and native SD Admin passed the full automated
Chromium gate on 2026-09-12: 26 scripts PASS, 0 failures, 126.95 s. See [4.6 report](../../docs/ui-emulator/Phase_4_6_Report.md).

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_phase4_settings_sys.py -v
tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_phase4_settings_sys.py
```

## Phase 4.7 ? coverage audit and Phase 4 roll-up gate

4.7 is the Phase 4 closeout. It reconciles the coverage ledger against the built
emulator: every visible control is `retained`, an `adapter`, an inert
`unsupported` boundary, or a catalogued open item; every back route is covered
or an explicitly deferred (SubGHz) screen. Details: [4.7 report](../../docs/ui-emulator/Phase_4_7_Report.md).

**How to compile and then run it (Windows Command Prompt, from the repo root):**

1. Start Docker Desktop, then build the emulator so the manifest matches your
   current sources (the audit reads `generated/manifest.json`):

   ```bat
   powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
   ```

2. Run the standalone coverage audit. This one needs **no browser and no
   rebuild** ? it is pure Python over the ledger and manifest, so it is the
   quickest way to check coverage after any slice change:

   ```bat
   tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_phase4_audit.py -v
   ```

   Or just print the summary without the test harness:

   ```bat
   tools\ui_emulator\.venv\Scripts\python.exe tools\ui_emulator\audit_coverage.py
   ```

3. Run the full Phase 4 roll-up gate. This chains every earlier gate
   (4.6 -> 4.5 -> 4.4 -> 4.3 -> 4.2 -> 4.1 -> Phase 3 -> Phase 2) and then the
   audit, on one fingerprinted static build ? so **build first** (step 1) and
   leave no preview server needed; it starts its own:

   ```bat
   tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_phase4.py
   ```

   The report is written to `docs/ui-emulator/phase4-verification.json`, and the
   per-control audit to `docs/ui-emulator/phase4-coverage-audit.json`.

If a deliberate slice change wires or unwires a control, the audit fails against
the committed baseline until you regenerate it:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tools\ui_emulator\audit_coverage.py --write-baseline
```

## Detector/attack features wired in 4.7

Three previously-inert features now work as offline simulations (no real radio):

- **Deauth Detector** tile (main WiFi menu): Start begins a simulated deauth
  flood from the scenario networks (newest row first); Stop freezes, Back returns.
- **Anti-Surv** tile (main WiFi menu): Start flags "followers" from the scenario;
  Stop shows the devices/followers summary; Back returns.
- **Handshaker**: open **WiFi Scan & Attack**, select a network, tap **Handshake**;
  the popup progresses to a captured handshake, then Done closes it.

Build, then run its gate (chains every earlier stage, the model test and the
browser test):

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_phase4_detectors.py -v
tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_phase4_detectors.py
```

The committed coverage baseline already reflects these three as covered. If you
change the slice again, regenerate it before the roll-up gate:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tools\ui_emulator\audit_coverage.py --write-baseline
tools\ui_emulator\.venv\Scripts\python.exe -u tests\verify_emulator_phase4.py
```

SAE overflow is now wired and verified by `tests/verify_emulator_sae.py`.
Global WiFi Blackout, global Handshaker and SnifferDog are wired to offline jobs,
native Yes/No confirmation and STOP. Handshaker shows synthetic scenario results;
these operations produce no radio effects or handshake files.

Run the focused gate after building:

```bat
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_global_attacks.py
```

The full `tests/verify_emulator_phase4.py` gate includes this follow-up.
See `docs/ui-emulator/Global_Attacks_Report.md` for acceptance evidence.

Time settings are wired to a persistent virtual wall clock, including date validation,
12/24-hour format, visibility and manual DST. Run `tests/verify_emulator_time.py`
after building; `tests/verify_emulator_phase5_settings.py` includes this gate.
The Time browser tests serve unchanged local Wasm bytes through Playwright to
isolate native behavior from intermittent local HTTP transfer resets.

### Offline client and password demonstration

Start Evil Twin or Rogue AP after selecting a scenario network. At the default
10x speed, the activity area shows a client connecting after about 2 seconds,
opening the portal at 5 seconds and submitting `DEMO-only-2026!` at 8 seconds.
The password is a fixed synthetic fixture; no form receives real credentials.
With realistic timing these stages occur at 20/50/80 seconds.

For the INTERNAL flow, open **Ad Hoc Portal & Karma > Show Probes**, select a
scenario network, leave the offline template selected and press **Start**.
STOP, Cancel and reset end the demonstration. The INTERNAL job is independent
of Grove/MBus. Networks without a matching scenario client receive a story-only synthetic actor
so every selected network completes the demonstration. Portal templates are labels for this offline story; no HTTP server runs.

Run `tests/verify_emulator_portal_demo.py` after building. The full Phase 5 settings
gate includes it. See `docs/ui-emulator/Portal_Demo_Report.md` for evidence.

### AP Radar

Open **WiFi Scan & Attack**, select exactly one network and tap **Radar**.
The native radar displays that SSID/BSSID/channel, a rotating sweep and changing
synthetic RSSI marked `(SIM)`. The marker radius represents signal strength, not
measured distance or bearing. STOP and Back release the module and return to Scan.
Grove and MBus own independent radar jobs. Disconnect/Cancel stops readings.

After building, run `tests/verify_emulator_radar.py`; the full Phase 5 settings gate
includes it. Acceptance evidence: `docs/ui-emulator/Radar_Report.md`.

### Transfer Speed and Observer exit

Settings > Transfer Speed now saves any of the nine firmware baud choices
(115200 through 4000000), restores the selection after reload and returns to
460800 after demo reset. Invalid stored rates recover to the default. This is a
configuration demonstration; virtual file operations do not measure UART speed.

Back from a running Observer now asks **Keep running** or **Stop and exit**.
Run `tests/verify_emulator_settings_followup.py` after building; the full Phase 5
settings gate includes these cases. See `docs/ui-emulator/Settings_Followup_Report.md`.
