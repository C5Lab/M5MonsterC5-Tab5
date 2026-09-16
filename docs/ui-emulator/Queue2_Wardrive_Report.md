# Queue 2 — Wardrive and Files stories

Recorded 2026-09-13. Local Chromium acceptance; no deployment or physical device access.

## Scope and evidence

The Queue 2 selector exposes 18 S16/S17 variants. Every variant uses the visible guide launcher and native Canvas controls. Read-only native exports report page/dialog visibility, actual Wardrive job IDs, successful Apply/GPS sample counters, exact pending-delete paths, and successful copy/delete counters. The reader always captures the GROVE baseline, including when a guide is launched or restarted on INTERNAL or MBUS; the guide separately checks the actual visible tab.

| Family | Variants | Required observed result |
|---|---|---|
| S17 recording | `s17-record`, `s17-no-gps`, `s17-no-sd` | New native Start/job, actual GPS/network observations, correct saved session; no invented rows during missing GPS; missing SD fails to save and is restored before leaving |
| S17 configuration | `s17-settings`, `s17-gps-debug` | Changed configuration plus a new Apply; fresh NMEA samples, explicit Stop and close |
| S17 lists | `s17-home`, `s17-blacklist` | NEON-BAZAAR Home entry; NEURODECK-07 blacklist entry plus other Bluetooth rows and exclusion of the selected MAC |
| S17 providers | `s17-wigle`, `s17-wdgwars` | The new recording has the selected provider's successful simulated status |
| S17 Home decisions | `s17-home-keep`, `s17-home-upload` | Armed Home detection for the fresh run; No keeps it running; Yes stops, saves and uploads that run |
| S16 Handshakes | `s16-handshakes`, `s16-handshakes-delete`, `s16-handshakes-empty` | Exact HTTP/DNS fixture copied by a new native transfer; delete No then Yes for the same target while peer files remain; actual empty list and Back |
| S16 upload | `s16-handshakes-upload` | Realistic timing, fresh WPA-SEC job, Close cancellation, new retry and the two expected successful demo-file results |
| S16 Wardrive Files | `s16-wardrive`, `s16-wardrive-delete`, `s16-wardrive-empty` | Fresh recorded file, selection of that exact path and WiGLE result; delete No/Yes for the same path; empty list and Back |

An existing HTTP/DNS fixture can be reused by successive Handshakes stories. Completion requires a fresh copy/delete action, not the mere presence of an old file. Empty-list variants explicitly start from a reset demo. Recording stories reject older job IDs, cancellation and disconnected sessions. Home/blacklist stories require adding the intended entry after starting the guide; remove an existing target entry before restarting that variant.

## Native correction found during acceptance

Wardrive Files' Uploaded, All and None buttons shared one generated registration template and therefore collided in the native binding registry. Two controls were rejected on each render, producing four duplicate-binding events during initial load. The integration change gives those three callbacks distinct slots. The regression creates one session uploaded to both providers and a second pending session, then checks Uploaded selects one, All selects two and None selects zero. Neither module's files are mutated by selection.

The native asynchronous file renderer resets its `compromised_files_loaded` flag after rendering. Evidence therefore also recognizes actual rendered file cards or the native empty-list label, together with matching model files, rather than depending only on that transient flag.

## Validation

- Node guide tests: 10 PASS. Cases include stale run/epoch rejection, hidden/wrong-tab context, launch from INTERNAL while GROVE is already recording, correct Home target/provider, exact copy/delete target, peer-file preservation and WPA-SEC cancel/retry results.
- Final consolidated browser run: **7 tests PASS in 209.305 seconds**. This includes 72 variant/rotation combinations (18 variants × 4 rotations), the INTERNAL-launch/restart/wrong-module/cancel/disconnect regression, and the native Uploaded/All/None selection regression.
- Every completed browser variant asserts that no native unavailable/duplicate-binding event occurred. All seven browser methods check page errors.
- The first consolidated browser run correctly failed the 12 Wardrive Files combinations on duplicate-binding events. The native distinct-slot correction resolved them; the final run above includes the empty-list, delete/cancel, provider and bulk-selection paths with strict unavailable-event checks enabled.
- The aggregate emulator gate is recorded separately by the root integration workflow; this report certifies the focused S16/S17 gate.

Browser tests load the same built Wasm bytes through a Playwright route because the local HTTP server intermittently resets large transfers. Every native action is still performed through the Canvas or visible browser controls; no guide is instantiated privately by the browser tests.

Frozen acceptance artifacts (SHA-256):

- `emulator.wasm`: `0e43c83995d8c647433732a35ef79f93b45efca2d816f68590d2be54fca6518f`
- `queue2-wardrive.mjs` (source and served copy): `6825eaf49ccc2fcf92fde534798207e0ab03d2f360b91306fe99818ecdcc1412`

Reproduce with `node --test tests/test_emulator_queue2_wardrive.mjs` and `tools/ui_emulator/.venv/Scripts/python.exe -m unittest discover -s tests -p test_emulator_queue2_wardrive.py -v` after the shared emulator build.

## Boundaries

All data and provider outcomes are synthetic and local. No radio, GPS hardware, WiGLE, WDGWars or WPA-SEC connection occurs. Handshakes uses the retained capture-file listing with the named HTTP/DNS fixture; it does not claim that this Ethernet fixture contains a Wi-Fi handshake. WPA-SEC intentionally reports `offline-demo-1.pcap` and `offline-demo-2.pcap`, not an upload of a user's selected capture. Wardrive files contain JSON track/row fixtures; the pre-existing native CSV Fix operation reports its format limitation and does not repair these JSON files. Preferences persist separately; virtual files and story progress do not.

## Pause checkpoint: aggregate selection readiness regression

The prior focused results above remain historical evidence. The later aggregate run failed in `test_native_uploaded_all_none_selection_is_distinct` before selection: the WDGWars Sync binding was absent after clicking its provider button immediately after successful WiGLE Close. Native `wardrive_wigle_close_cb` invalidates the menu cache after success, then an asynchronous refresh reconstructs the menu; provider controls are disabled while loading. The test now waits for the actual WDGWars binding with `LV_STATE_DISABLED` cleared and then for provider 2 plus the native Sync binding. No web, C, or dist artifacts changed.

Post-change verification is **pending**. The first attempted targeted run exposed use of raw inspection objects without binding fields; corrected to the native binding getter. The three-repeat command stopped after its first run timed out (34.936 seconds) because the new JavaScript predicate had a newline after `return` (automatic semicolon insertion). That predicate was corrected, but no further tests started because the user requested saving and stopping for the night. Resume by running the selection method three times, then the parent aggregate gate; do not claim a final aggregate PASS yet.

Checkpoint verification update: parent requested one final targeted run after the ASI correction. `test_native_uploaded_all_none_selection_is_distinct` **PASS, 1 test in 5.726 seconds**. This verifies both provider readiness and native Uploaded/All/None counts on the frozen artifact. Additional repeated runs and the final aggregate gate remain pending; no additional tests started.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
