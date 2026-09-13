# Queue 2 attack stories — S09–S12

Local offline emulator only. Native firmware source `main/main.c` is unchanged.

## Coverage

| Story | Launcher variants | Native path and evidence | Cleanup |
|---|---|---|---|
| S09 Karma | `karma-menu` | Module menu → Start Sniffer → increasing synthetic packets → Stop Sniffer → NEON-BAZAAR probe → demo HTML configuration → Start Karma → clients and packets | Native STOP, Back to module menu |
| S10 Scan actions | `deauth`, `sae`, `handshake` and each `-cancel` | Select only NEON-BAZAAR → native action → target-specific fresh operation → native activity/result | Native STOP/Done; separate emulator cancellation and popup close |
| S11 Global actions | `blackout`, `global-handshaker`, `snifferdog` and each `-cancel` | Global WiFi → confirm → No leaves module idle; success variants reopen → Yes → changing packets/handshakes | Native STOP; cancel variants return to module menu |
| S12 Beacon | `beacon`, `beacon-empty` | List SSIDs → Start Spam → packets; separate delete all → empty refusal without an operation | Native Stop/Back |

S10 preserves immediate launch where the firmware has no confirmation dialog. It does not add a new confirmation screen. S09 covers the actual module-menu Karma route. The original backlog action-bar clause was a mapping error: `main/main.c:17777` builds the Scan bar with a null Karma callback, so `main/main.c:7543` selects Radar instead. `main/main.c:18619` gives the Observer bar `observer_karma_btn_cb`, whose implementation at `main/main.c:37573` calls `karma2_fetch_probes`. That distinct unsupported Karma2 route remains S22; no extra Scan control or rerouting was introduced.

## Native endpoints

- Karma: `show_karma_page`, `karma_start_sniffer_cb`, `karma_stop_sniffer_cb`, `karma_probe_click_cb`, `karma_html_select_cb`, `karma_attack_popup_close_cb`, `karma_back_cb`. Offline operations are `attackStart(..., 'karma')`, `attackStop`, and `cancel` on Back.
- Scan actions: `handle_selected_attack` selects `show_scan_deauth_popup`, `app_sae_open`, or `show_handshaker_popup`; native stop callbacks close their owning popup. Offline operation names are `deauth`, `sae_overflow`, and `handshake`.
- Global actions: each retained `show_*_confirm_popup` exposes No/Yes; `app_global_confirm` creates `blackout`, `global_handshaker`, or `snifferdog`; native `show_*_active_popup.stop_btn` ends the operation.
- Beacon: `show_beacon_spam_page`, `show_beacon_ssids_page`, `beacon_ssids_delete_cb`, `beacon_spam_start_cb`, `beacon_spam_active_close_cb`, and native Back. The `beacon` job copies the configured SSID list and an empty list cannot reserve a job.

## Implementation

`web/queue2-attacks.mjs` exports `queue2Attacks` with 15 launcher entries. Predicates use the read-only `emu_queue2_attacks_state` native export, inspected visible controls and immutable offline job snapshots. Fresh job IDs, exact targets, native surface closure, and completed/cancelled state gate progress. Restart records the current job floor; hidden or other-module context does not advance and resets the activity observation baseline.

The Handshaker portion of `runtime/detectors.c` now reserves a synthetic `handshake` operation, snapshots the native selected target, reports that target in its retained native log, and respects busy/disconnected/cancelled state. Its native Done/STOP releases the job. The success log now requires an actual eligible model handshake result; ineligible/no-result selections show a truthful empty result instead. A silent emulator `alert_chime_play` adapter preserves success logging without falsely treating the absent physical speaker as an unsupported operation. Beacon now displays an empty-SSID refusal on its visible main page instead of its hidden list page. `model/attacks.mjs` accepts this strictly offline operation and derives handshake results from the selected fixture networks.

## Verification

- `node --test tests/test_emulator_queue2_attacks.mjs`: **11 PASS** (2026-09-13).
- `node --test tests/test_emulator_attacks.mjs tests/test_emulator_global_attacks.mjs`: **9 PASS** (2026-09-13).
- `.\tools\ui_emulator\.venv\Scripts\python.exe tests/test_emulator_queue2_attacks.py -f`: **7 PASS, 87.831 s** (2026-09-13, local Chromium).

Browser acceptance launches the actual story picker and native canvas controls and checks visible guide titles/progress. Verified matrix: 15 variants × four rotations, 60 rotation/variant paths in total, plus four edge paths covering wrong target, busy refusal, no eligible handshake result, and restart/module/disconnect. It serves the compiled artifact on loopback and intercepts only the local Wasm file for reliable test loading.

No files are uploaded, no radio is driven, and no hardware acceptance is implied.

Verified artifact (SHA-256; the root aggregate gate verifies any subsequent shared rebuild):

- `emulator.wasm`: `0f964f6ecc1602320a0c1fd6ee3a159c12f3d40e83ac77831b3293aa14433787`
- `emulator.js`: `18df0601bb253673906bbb8cf24aa1006be3b14e102e658dbac42a0ba43e6082`

The first full browser attempt overlapped a shared build and failed with an explicit JS/Wasm mismatch. That artifact race was resolved by freezing the build pair; the complete rerun above passed. Native failure cases found during development (hidden Beacon refusal and unconditional Handshake capture log) have regression coverage in the passing suite.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
