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

## Pixie Dust demo

On GROVE or MBus, open **WiFi Scan & Attack**, wait for the scan to finish,
select exactly one access point, and choose **Pixie Dust** in the action bar.
The native Tab5 page consumes an offline JanOS-shaped command stream. The
NEON-BAZAAR fixture reaches a verified demo result and reveals a one-time
synthetic PIN and network key on that page. Other access points produce a
bounded no-match result. **ESC / Stop and back** waits for the simulated
`wps stop` completion before closing. A missing/disconnected module and a
second concurrent operation are refused by the local model. No WPS packets,
radio commands, credential recovery, or JanOS device communication occur.

After rebuilding, run the focused model and Chromium tests from the repository
root:

```bat
node --test tools\ui_emulator\tests\test_wps_pixie.mjs
tools\ui_emulator\.venv\Scripts\python.exe -u tools\ui_emulator\tests\test_wps_pixie_browser.py -v
```

## WPA PSK Auditor demo

Open **INTERNAL → WPA PSK Auditor** to inspect four fixed, synthetic capture
records. NEON-BAZAAR appears once with Grove and USB sources; GHOST-LINE is
remote only; CHROME-CLINIC is intentionally invalid; SYNAPSE-RELAY starts in
the local catalogue. Source filtering, idempotent **Sync all to Tab5**, two
demo wordlists, individual audits, safe pause/resume, Start over/Delete audit
confirmation, a sequential batch queue, history and recovered demo credentials
are interactive native LVGL controls. Paused checkpoints, history and recovered
demo credentials survive browser reload when browser storage is available.
Synced virtual SD copies return to their seed state on reload. **Reset demo**
clears the saved audit state.

**Load PCAP examples** also installs a synthetic NEON-BAZAAR WPA capture in
GROVE's **Compromised Data → Handshakes** list. Its file actions and **CRACK**
route can be tried there. The capture bytes are an offline fixture marker, not
radio data or a parseable PCAP.

The fixture catalogue contains metadata only. Sync changes synthetic catalogue
state; it does not copy handshake bytes. Candidate counts and outcomes are
scripted, with no packet parsing, password verification, radio use, JanOS
commands or physical storage. **Handshaker → Crack latest** opens a matching
fixture in the auditor. **Compromised Data → Handshakes** retains the volatile
virtual file list; its **Open WPA Auditor** link opens the catalogue. A file's
**CRACK** button recognizes only exact offline fixture content at its expected
path; an imported PCAP with a matching name alone cannot select a scripted
result.

Focused checks after rebuilding:

```bat
node --test tests\test_emulator_wpa_auditor.mjs
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_wpa_auditor.py -v
```

## Global Portal and Observer Probes & Karma

On Grove or MBus, open **Global WiFi Attacks → Portal**. The native popup
accepts a demo SSID and a virtual HTML template. **Start Portal** shows the
same staged synthetic client, visit and submitted demo password used by the
INTERNAL portal. **Background** hides the popup while the local job continues;
opening Portal again shows its current state. **Stop Portal** releases the
module. A virtual SD card is required for the external module's template list.

In **Network Observer**, start the observer, open a network row, scroll the
action strip sideways to **Karma**, select a synthetic probe and a virtual
template, then start or stop the same offline portal story. The action strip
supports horizontal wheel scrolling in portrait mode. These flows create no
AP, DNS or HTTP service, radio traffic, captured form or real credential.

Focused checks after rebuilding:

```bat
node --test tests\test_emulator_portal_flows.mjs
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_portal_flows.py -v
```

## Rogue GITM offline flow

On **WiFi Scan & Attack**, select one named scenario victim and scroll the
action strip sideways to **Rogue GITM**. The setup popup lists only different,
named uplinks on the victim's channel. It accepts a local demo mirror pass of
8–63 characters and an optional demo uplink pass. Passwords are validated and
discarded; they are absent from the job, its result and saved file metadata.

**Start** opens the native Capture Gateway live view with simulated uplink,
mirror preparation and capture stages. **STOP GITM** writes a synthetic
Ethernet/ARP PCAP for local inspection. No deauthentication, mirror AP, real
uplink, credential verification or radio capture occurs. Observer's single
network popup directs this flow to the full WiFi Scan list.

Focused checks after rebuilding:

```bat
node --test tests\test_emulator_rogue_gitm.mjs
tools\ui_emulator\.venv\Scripts\python.exe -u tests\test_emulator_rogue_gitm.py -v
```

## Try Bluetooth discovery and locate (Phase 4.1)

1. On Grove, open **Bluetooth**, then **BT Scan & Locate**.
2. The list contains six synthetic BLE devices. Tap one to see its name, MAC
   address and changing RSSI, then use Back to return to the list.
3. **Rescan** reads the scenario again. Grove and MBus keep separate discovery
   snapshots. A disconnected model module returns no devices and an active
   locator displays **No signal**.

All four screen rotations are covered. AirTag scanning and the nRF24 jammer
also run as deterministic offline demos; this workflow uses no physical
Bluetooth radio.

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
WiGLE upload uses an offline sync adapter; its firmware Wi-Fi scan and credential
prompts are outside the emulator route. See the [WiGLE prompt audit](../../docs/ui-emulator/WiGLE_Upload_Prompt_Audit.md)
for the call paths and verification.
With automatic upload and a saved Home Network enabled, **Home → Yes** stops
the local recording and shows the upload result. A failed result offers
**Retry**, **Resume** and **Close**. A successful result with Power off enabled
shows a demo countdown and **Stay on**; it never powers off the browser or a
device. **Archive** moves completed virtual sessions to `/uploaded/` only after
a successful automatic upload. The five automatic-upload switches restore on
reload. The focused browser check is
`tests/test_emulator_wardrive_autoup.py`.
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
or an explicitly deferred screen. Details: [4.7 report](../../docs/ui-emulator/Phase_4_7_Report.md).

**How to compile and then run it (Windows Command Prompt, from the repo root):**

1. Start Docker Desktop, then build the emulator so the manifest matches your
   current sources (the audit reads `generated/manifest.json`):

   ```bat
   powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
   ```

2. Run the standalone coverage audit. It needs no browser, but source changes
   require refreshed inventory, control contracts, and a rebuilt manifest.
   The audit rejects stale source hashes:

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

### Wi-Fi Analyzer

On GROVE or MBUS, open **WiFi Analyzer** and press **Scan**. The native Tab5
screen parses a complete synthetic WFA/1 transaction generated from the demo
networks. Its channel chart, AP details, display filters, sort controls, zoom,
acquisition settings, and channel proposal use the production LVGL screen and
model. Scan jobs and last complete snapshots are separate for each module.
**Stop** cancels the current job and retains the last complete snapshot.

Under **More guided stories**, choose **Wi-Fi Analyzer** to follow a scan,
inspect NEON-BAZAAR, view the channel proposal, and return to GROVE. The
proposal is a relative heuristic over synthetic observations; it does not
measure the local spectrum or change a router. Run `node --test
tests/test_emulator_analyzer.mjs tests/test_emulator_analyzer_guide.mjs` and
`tools/ui_emulator/.venv/Scripts/python.exe tests/test_emulator_analyzer.py`
after building. These checks use an offline browser model and Chromium, not
a JanOS board or radio.

### Saved Evil Twin passwords and Portal Data

Open **Compromised Data → Evil Twin Passwords** or **Portal Data** on a virtual
GROVE, MBUS, or INTERNAL module. The production pages parse `show_pass evil`
and `show_pass portal` text from that module's volatile SD model. They begin
empty. A submitted offline Evil Twin story writes one demo password entry;
a submitted Ad Hoc Portal story writes one demo portal entry. The native
password row opens its Connect confirmation and routes to the offline ARP
page. Cancel, Back, Clean, and confirmed Delete use the native controls;
deleting the virtual file empties its list on refresh. Reset demo clears the
entries. Run `node --test tests/test_emulator_compromised_model.mjs` and
`tools/ui_emulator/.venv/Scripts/python.exe tests/test_emulator_compromised_passwords.py`
after building. These records are synthetic; no module SD card or Wi-Fi
network is read.

### Transfer Speed and Observer exit

Settings > Transfer Speed saves nine GROVE/M-BUS rates (115200 through 4000000)
and eight CH34x USB rates (115200 through 3000000) independently. Both restore
after reload; demo reset returns them to 460800 and 921600 respectively.
Invalid stored rates recover to those defaults. This is a configuration
demonstration; virtual file operations do not measure UART speed.

Settings > Scan Setup saves **Show Vendors** and **Log unknown vendors** across
reloads. The default virtual SD exposes a synthetic vendor capability; the
browser does not read an `oui_wifi.bin` file.

Back from a running Observer now asks **Keep running** or **Stop and exit**.
After **Stop**, **Export** writes the native Observer CSV columns to the selected
module's volatile SD model at a deterministic path such as
`/sdcard/lab/observer/observer_grove_0001.csv`. Export while running asks you
to stop first; absent virtual SD reports an error without creating a file.
Options and network details close before leaving Observer. Run
`tools/ui_emulator/.venv/Scripts/python.exe tests/test_emulator_observer_export.py`
after building to check the CSV bytes, per-module paths and navigation. The
export contains synthetic scenario observations, not a physical RF capture.
Run `tests/verify_emulator_settings_followup.py` after building; the full Phase 5
settings gate includes these cases. See `docs/ui-emulator/Settings_Followup_Report.md`.

### Wi-Fi STA MAC policy

Open **INTERNAL → Settings → Wi-Fi MAC**. The default demo has virtual Grove and
MBus Monsters, so select the one whose status you want to inspect. To see the
automatic read, choose **Grove only · MBus disconnected** under **Module
condition**; the demo restarts and selects Grove when you open Wi-Fi MAC.
**Refresh** repeats the read. Choose **Factory**, **Random**, or **Fixed** (for example,
`02:11:22:33:44:55`) and save to the selected virtual module. The saved policy
appears separately from the policy and effective MAC for its current boot. Open
**INTERNAL → Module Status** and simulate that module's reboot when **Restart
required** says **Yes**; Tab5 does not reboot it when you save.

The status and MAC addresses are synthetic, isolated per virtual module. This
virtual policy and the current boot MAC survive an ordinary browser reload when
browser storage is available. **Reset demo** clears them; virtual SD files
remain ephemeral. The browser demo does not read a connected physical Monster
or exercise its Wi-Fi radio. See the [Tab5 Wi-Fi MAC instructions](../../docs/Sta_MAC_Setup.md)
for the real JanOS workflow and the distinction between the STA policy and a
temporary WPS session MAC.

### Browser screenshots

Tap the LAB5 title in the device status bar to request a PNG of the virtual
display as a browser download. This does not write to virtual or physical SD.

### Sub-GHz virtual module

Choose **MBus → Sub-GHz**. The retained LVGL screens cover Quick Scan, Hunter,
Listen, SD Signals, Weather, Jammer, Tesla and Settings, including the three
scanner/listener settings pages. Three fixed scenario signals drive scan,
capture and weather displays. Hunter and Listen can save to a per-tab virtual
SD list; rename, delete and replay controls act on that in-memory list. Jammer
and Tesla show simulated status only. Reload or **Reset demo** clears captures
and virtual SD signals. GROVE remains the classic module profile for OTA
Auto-detect; MBus has the virtual Sub-GHz capability.

After building, run `node --test tests/test_emulator_subghz_model.mjs` and
`tools\ui_emulator\.venv\Scripts\python.exe tests\test_emulator_subghz.py`.
These checks exercise the browser model and native Wasm UI, not RF hardware.

### NFC virtual module

Choose **MBus → NFC** to open the retained hub, Read, List, card detail and
Emulate pages. Two scenario cards cycle on successive reads. Save, rename and
delete use a per-tab virtual SD list that clears on reload or **Reset demo**.
Emulation reports full NTAG page support for the NTAG fixture and UID-only
support for the Classic fixture; it sends no NFC signal. Back from Emulate
stops the virtual session.

After building, run `node --test tests/test_emulator_nfc_model.mjs` and
`tools\ui_emulator\.venv\Scripts\python.exe tests\test_emulator_nfc.py`.
