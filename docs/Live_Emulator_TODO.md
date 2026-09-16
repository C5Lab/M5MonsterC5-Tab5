# Live Emulator TODO and Progress

Last reviewed: 2026-09-13.

**Current checkpoint (2026-09-13):** Queue 1 and Queue 2 are complete. S18-S20 add 15 settings variants; all 47 Queue 2 variants passed the integrated gate. S08 and S15 now run in the main gate too. **72 scripts PASS, 0 failures, 1155.20 s** ([summary](ui-emulator/test-runs/20260913-201748-ac7773/summary.json), [integration report](ui-emulator/S18_S20_Integration_Report.md)). Next: **S22 global Portal and additional Karma2**. Local development/testing only; no deployment.

**Previous checkpoint (before S01/S27):** First guided Bluetooth story and all eight open dialog controls are implemented. The focused gate passed 18 cases across four scripts, including all four orientations ([run](ui-emulator/test-runs/20260912-211801-f03ebf/summary.json)). The audit covers 353/452 controls, with 0 open candidates and 93 explicitly deferred controls. The previous full user run passed 49 scripts; the new full run stopped at an unrelated browser startup timeout ([run](ui-emulator/test-runs/20260912-211423-66ac5d/summary.json)); no new full PASS is claimed. See [Bluetooth guide](ui-emulator/Guided_Bluetooth_Report.md) and [native dialogs](ui-emulator/native-dialogs-20260912-report.md).

**Resume audit (2026-09-11):** The `%f` fix was applied to `main/main.c`, the frozen slice inputs and control contracts were regenerated against the fixed source, and the emulator was rebuilt. Fixing `%f` shifted line numbers and exposed an emulator-tooling off-by-one in multi-line event-callback enrollment (now fixed via `source_end_line`), plus an under-provisioned `advance()` window in the lifecycle test. `tests/verify_emulator_phase3.py` now reports `passed` (all six suites) on the rebuilt static build.

**Design reference:** [Live Web Emulator Plan](superpowers/plans/2026-09-09-live-web-emulator.md).

**User checkpoint (2026-09-09):** The user successfully opened the local emulator and confirmed that the current prototype works and looks good. This confirms local preview usability; it does not close the untested interactions or the full Phase 2 gate.

## Working now

| Capability | Phase | Evidence |
| --- | --- | --- |
| Docker/Emscripten build and local browser preview | 2 | Build passed; user confirmed the local preview works |
| Home, Scan and Internal Settings navigation | 2 | Eight Chromium cases across four rotations and two viewport sizes |
| Simulated Wi-Fi scan, cancellation, rescan and network selection | 2 | Browser checks, including suppression of late cancelled responses |
| Scan results, Observer rows and clients from one shared scenario | 3 | Phase 3 browser report: identities agree between model and retained UI |
| Network Observer page, network popup and synthetic PCAP capture | 3 | Phase 3 browser report: file network, client IDs, packet count and PCAP magic |
| Virtual SD listed and opened by the production local-analysis page | 3 | ESPShark TAB5 SD lists `synthetic-1.pcap` (198 B) and the real `pcap_reader` opens it |
| Separate Grove and MBus scan results | 2 | Browser context-isolation checks |
| Canvas clicks in four orientations and landscape list scrolling | 2 | Browser tests at 1440×1000 and 390×844 CSS pixels |
| Scan Setup: change minimum channel time, save and reopen | 2 | Browser check for 100 → 150 ms |

**Earlier checkpoint (2026-09-12):** Initial detector and settings runs had intermittent localhost startup failures. Those historical attempts were superseded by the user-confirmed 49-script cumulative pass recorded below. Routed native tests still do not constitute validation of every HTTP transfer path.

**Accepted scope:** [User decisions](ui-emulator/Accepted_Decisions.md): SubGHz deferred; shortened demo timing with a realistic option; required virtual modules available by default. No further user preference is needed for Phase 1.

## Status rules

- `[x]` means completed with evidence; `[ ]` means remaining work.
- A rendered screen is not evidence that its controls or asynchronous behavior work.
- Close each phase only after its acceptance gate passes. Record the test command/report or artifact next to completed implementation items.
- Update this tracker after each completed work session. Record blockers explicitly; do not use estimated percentages as proof of progress.

## Phase overview

| Phase | Status | Deliverable |
| --- | --- | --- |
| 0. Initial analysis and references | Complete | Architecture, screenshots, flow references, manual stories review |
| 1. Detailed interaction and dummy-data inventory | Complete | Verified control contracts, identities, adapter decisions and scenario schema |
| 2. Browser feasibility prototype | Complete | Local Home, Settings and Scan build; eight Chromium cases pass |
| 3. Stateful simulated device | Complete | Consistent scan-to-file-to-analysis workflow, verified through the rendered summary and lifecycle suite |
| 4. Full application coverage | Implemented scope accepted | Full regression passed 36 scripts; catalogued missing flows remain open |
| 5. Guided stories and persistence | In progress | Guides S01-S21 and S27 verified; deferred Queue 3 remains; virtual SD stays volatile by user decision |
| 6. Verification and compatibility | Not started | Browser, visual, lifecycle and firmware regression evidence |
| 7. GitHub Pages delivery | Not started | Combined site artifact containing emulator and existing tools |

These tracking phases split the design plan's first phase into inventory and prototype, and explicitly include the later manual-stories addition.

## Phase 0 - Initial analysis and references

- [x] Export actual LVGL render references: 98 named cases in four rotations. Evidence: [render gallery](ui-render/README.md).
- [x] Produce source-anchored navigation references: 108 routes. Evidence: [flow documentation](ui-render/flow/README.md).
- [x] Inspect existing renderer fixtures and identify their limits. Evidence: `tools/ui_render/entry.c`, `host.h`, and `prepare.py`.
- [x] Identify disabled callbacks/tasks and the one-frame render lifecycle as gaps for live use.
- [x] Review `tab.stories`: seven image/caption tutorials with 44 entries, plus a differently structured middle-man page.
- [x] Propose shared C/LVGL -> WebAssembly, browser input, simulated device adapters and deterministic scenarios.
- [x] Inspect both Pages publishers and account for existing firmware assets and site content.
- [x] Save an English architecture and delivery plan, including guided stories.

**Historical Phase 0 scope:** Existing fixtures and reference workflows were examined. The interaction inventory was subsequently completed in Phase 1, and a working WebAssembly slice was verified in Phase 2.

### Existing dummy-data baseline

| Area | Confirmed current render fixture | Missing for live simulation |
| --- | --- | --- |
| Modules and storage | Connected flags for Grove/MBus, SD presence, SubGHz availability | Lifecycle, disconnects, real virtual storage contents |
| Wi-Fi | Three LAB networks with BSSID, channel, RSSI and security; one selected | Timed discovery, rescans, filtering/selection effects, errors |
| Observer | One network linked to one client | Client details, changing activity and consistent event history |
| Bluetooth | One named LAB device and RSSI | Populated discovery, locate behavior, changing signals |
| Portal file selection | Two HTML filenames | File contents and workflow state |
| Settings/status | Defaults and special render setup | Persistence, restart, coherent clock/battery/module state |
| PCAP, Wardrive, SubGHz and remaining workflows | Constructor rendering is available for named cases | Full fixture sets, operation results, valid files and cross-screen consistency |

## Phase 1 - Detailed interaction and dummy-data inventory

- [x] Inspect initial JanOS producer contracts for scan results, hosts, probes, HTML files and BLE; review binary-transfer documentation. Evidence: [JanOS dummy data research](ui-emulator/README.md).
- [x] Create a synthetic cyberpunk seed and five example response bodies. Evidence: [seed](ui-emulator/neon-district.seed.json) and [responses](ui-emulator/neon-district.responses.json); parser verification is recorded below.
- [x] Run five initial response families through production parsing code; cover chunking, empty/error inputs and HTML timeout. Evidence: [parser report](ui-emulator/parser-test-report.json). Full Wi-Fi/BLE receive tasks are not executed.
- [x] Create `tools/ui_emulator/coverage.json`: 452 registration sites with resolved callback candidates, source hashes and 110 current route references.
- [x] Assign permanent control-template IDs and specify runtime screen/module/entity/slot IDs. Line/order stability, branch separation and invalid-binding rejection are tested; actual browser bindings are Phase 2 work.
- [x] Inventory callbacks, task/timer call sites, parsers, storage calls and translation-unit declarations; document proposed ownership/lifetime rules. Evidence: [Phase 1 report](ui-emulator/Phase_1_Report.md).
- [x] Specify all 531 external-symbol decisions, including HTTP transfer, CRC, indirect callbacks and ESP-only cleanup. Freeze decisions/source hashes; no default no-op or unresolved category is permitted.
- [x] Complete control contracts through explicit retention of the original handlers, guards, branch order, state writes and user-data semantics. All 452 source sites have contracts; 406 are current and 46 are deferred with SubGHz. Evidence: [Control and Adapter Contracts](ui-emulator/Control_Adapter_Contracts.md). Browser behavior is not yet validated.
- [x] Map all 44 manual story steps to current functions and document branches/external steps; map the middle-man narrative separately.
- [x] Define and validate the versioned scenario schema for modules, networks, clients, BLE, signals, GPS, files, settings and jobs.
- [x] Define populated, empty, disconnected, timeout and failure requirements across seven applicable domains; executable variants remain implementation work.
- [x] Define invariants for identities, file/result consistency, radio ownership, cancellation and reset. Schema/reference checks pass; lifecycle behavior awaits the runtime.

**Gate:** Every inventoried control has preconditions, an action, expected state changes and a data owner. Unknowns are explicitly listed, not silently stubbed.

**Gate result:** Passed for Phase 1 specifications and the initial native-parser suite. Run `tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_phase1.py` to reproduce. This is not an all-screen interactive test.

## Phase 2 - Browser feasibility prototype

- [x] Add a reproducible Emscripten build using the repository LVGL version and production fonts. Evidence: `tools/ui_emulator/build.ps1` passed with pinned Emscripten 4.0.14.
- [x] Restore actual Home, Settings and Scan navigation callbacks through explicit boundaries. Evidence: [eight Chromium cases](ui-emulator/phase2-browser-report.json); unsupported destinations remain explicit.
- [x] Implement a cooperative main loop with elapsed time and bounded simulated jobs. Evidence: scan completion, cancellation, clock advancement and rescan checks in the browser report.
- [x] Connect Canvas display and mouse clicks; verify wheel scrolling on overflowing landscape lists. Evidence: eight Chromium cases.
- [x] Verify touch/drag, pointer cancellation and text input end to end. Implemented in `tests/test_emulator_phase2.py`; passed in the [Phase 2 gate](ui-emulator/phase2-verification.json).
- [x] Verify mouse-click mapping in all four rotations at two CSS viewport sizes. Evidence: eight Chromium cases.
- [x] Extend rotation/input acceptance to touch, drag and text entry. Implemented as an eight-case touch/text loop in `tests/test_emulator_phase2.py`; passed in the [Phase 2 gate](ui-emulator/phase2-verification.json).
- [x] Demonstrate scan start, progress, completion and cancellation. Evidence: the browser report checks active/completed/cancelled states, populated results and late-event suppression.
- [x] Record local startup, static download size, render-callback duration and Wasm heap size. Evidence: browser report; these are desktop localhost measurements.
- [x] Exercise the remaining retained Settings controls, rotation-triggered restart and the modal scan overlay that blocks navigation during an active scan. Implemented in `tests/test_emulator_phase2.py`; passed in the [Phase 2 gate](ui-emulator/phase2-verification.json).
- [x] Verify runtime control identities and popup registration lifetimes against the frozen contracts. Implemented as the binding-conformance test; passed in the [Phase 2 gate](ui-emulator/phase2-verification.json).
- [x] Establish and validate desktop/mobile performance budgets on representative conditions. Budgets defined in `tools/ui_emulator/performance-budgets.json` and evaluated by `tests/test_emulator_performance.py`; passed in the [Phase 2 gate](ui-emulator/phase2-verification.json).
- [x] Document manual local preview startup and confirm it with the user. Evidence: [Windows preview instructions](ui-emulator/Phase_2_Report.md#manual-local-preview) and user confirmation on 2026-09-09.

**Gate:** A locally served static build supports real navigation and cancellable scanning in every rotation without freezing. Record the preview artifact and test results.

**Gate result (2026-09-10):** Passed: seven suites, eight mouse cases, eight touch/text rotation-and-scale cases, Settings/restart/binding checks and both performance profiles. Reports contain matching static artifact hashes. Physical mobile devices and Firefox/WebKit remain Phase 6 work.

## Phase 3 - Stateful simulated device

- [x] Implement the shared seeded scenario model and versioned fixtures. Evidence: six model tests in `tests/test_emulator_device.mjs`.
- [x] Implement simulated command/reply adapters, reusing production parsers where practical. The bridge emits scan rows that the retained `parse_network_line()` consumes. Evidence: `tests/test_emulator_bridge.mjs`.
- [x] Add scheduled events, operation IDs and protection against late/cancelled responses. Evidence: model tests; browser-side cancellation is covered by the Phase 2 gate.
- [x] Add virtual SD contents and valid synthetic PCAP files. Evidence: [capture report](ui-emulator/phase3-capture-report.json), checked with the production `pcap_reader.c` under ASan/UBSan.
- [x] Complete Scan -> selected network -> clients -> simulated capture -> saved file -> analysis. Browser automation now drives the whole path and asserts the rendered capture summary (`3 packets`, `Ethernet`, `198 B`, `0.002 s`) against the model. Evidence: [Phase 3 gate](ui-emulator/phase3-verification.json) and [report](ui-emulator/Phase_3_Report.md).
- [x] Verify repeated start/stop, navigation during activity and deterministic reset. Evidence: `tests/test_emulator_lifecycle.py` (4 cases) in the gate.

**Gate:** The entire workflow runs interactively; selected network, clients, saved data and analysis agree.

**Gate result (2026-09-11):** Passed. Six suites green on the rebuilt static build: Phase 2 regression, device and bridge model tests, the production PCAP reader under ASan/UBSan, the Phase 3 Canvas workflow through the rendered summary, and the lifecycle suite. Late-reply/stale-event coverage beyond cancellation stays Phase 4/6 work.

## Phase 4 - Full application coverage

Phase 4 is split into independent per-domain slices. Each slice follows the same
five-step template and lands and gates on its own, mirroring Phase 3. Control
counts are approximate, from the coverage ledger (`coverage.json`).

**Per-slice template (repeat for every sub-section below):**

1. `tools/ui_emulator/slice-phase4-<domain>.json` - explicit `retain`/`adapter`/`unsupported` decisions; no defaults.
2. Extend the shared model (`model/device.mjs`, plus `model/<domain>.mjs` when the domain owns its own data/fixtures).
3. Wire the slice into `runtime/*.c` (boundary functions and any per-tick activity) and register simulator-owned controls under the `emu.` prefix.
4. `tests/test_emulator_phase4_<domain>.py` - drive the domain from the Canvas and assert UI against the model.
5. `tests/verify_emulator_phase4_<domain>.py` mini-gate (re-runs the Phase 3 gate first, then the domain suite); record the report and tick the boxes here.

**Recommended order:** 4.1 -> 4.2 -> 4.3 -> 4.4 -> 4.5 -> 4.6 -> 4.7.

**Shared model work (built incrementally, not a separate slice):** changing
client/network activity and signal drift over time, consumed by Observer live
activity, Bluetooth and Wardrive. Land the minimum each slice needs; do not build
it speculatively.

**SubGHz** module, signal and operation simulation is deferred by user decision and is not a gate for this milestone.

- [x] Record the user-supplied SubGHz UART reference (2026-09-10), storage/lifecycle implications and unresolved contradictions. Evidence: [SubGHz protocol notes](ui-emulator/SubGHz_Protocol_Notes.md). Protocol intake only; no SubGHz runtime implementation or producer verification is claimed.

### 4.1 - Bluetooth (~6 registration controls: menu, scan, locator)

Discovery list, device detail/locate, applicable detector states. The real BT
control surface is small; the earlier ~42 estimate over-counted `bt` substrings.

- [x] Slice `slice-phase4-bluetooth.json` (menu/scan/locator page renders retained; `parse_bt_device_line` retained; `transport_read_bytes`, `bt_scan_rescan_cb`, `bt_scan_device_click_cb` and `bt_locator_tracking_back_btn_event_cb` adapters; locator task, AirTag scan and nRF24 jammer explicit unsupported boundaries).
- [x] Model: shared BLE device set (`device.bluetooth`) surfaced per connected module, plus drifting locator RSSI (`device.bluetoothRssi`); bridge `bluetoothScanText`/`bluetoothLocatorRssi`.
- [x] Runtime wiring `runtime/bluetooth.c`: `scan_bt` stages the device text drained by the retained synchronous scan through `transport_read_bytes`, and each list is snapshotted per tab (`app_bt_modules[4]`) so the shared firmware parser globals cannot be clobbered before a cached row is clicked. Rescan defers a rebuild to the next tick (out of the click handler); the infinite locator task is intercepted in `xTaskCreate` and its RSSI label driven from `app_tick`, showing "No signal" when the module disconnects. `app_bind` differentiates BT menu tiles and per-MAC scan rows.
- [x] Browser test `test_emulator_phase4_bluetooth.py` (5 checks; 6 seeded devices).
- [x] Lifecycle test `test_emulator_bluetooth_lifecycle.py` (4 cases): rescan across disconnect/reconnect, exact model-matched RSSI tracking + "No signal", per-tab cached list survives an empty MBus scan, and discovery+locator in all four rotations.
- [x] Mini-gate `tests/verify_emulator_phase4_bluetooth.py` + report; passed 2026-09-11.

**Gate:** Discovery and locator identity/RSSI agree with the model; disconnect/reconnect, rescan and module isolation pass. Discovery and locate work in every rotation. AirTag detection remains outside this completed slice.

**Gate result (2026-09-11):** Passed. The Phase 4.1 gate runs the whole Phase 3 gate first, then the Bluetooth workflow and lifecycle suites, on one static build (all three exit 0; matching artifact hash). Live tracking is driven from `app_tick`; AirTag scan and the nRF24 jammer stay explicit unsupported boundaries. Note: the download performance budget was raised (raw bytes; transfer is gzipped ~40%) to scale with full-application coverage — the live mobile timing budgets are unchanged and remain the UX guardrail.

### 4.2 - Wardrive / GPS (~33 controls)

Wardrive run, GPS state, saved sessions, simulated upload outcomes.

- [x] Slice `slice-phase4-wardrive.json`: production page, CSV parser and table retained; native Setup/GPS/Home/Blacklist/Upload overlays with cooperative hardware adapters.
- [x] Model: GPS acquisition/loss/track, WiFi+BT counts, per-module saved sessions, SD errors, cancellation and upload success/failure/partial. Native provider selection/pending/all and effective blacklist are covered; 16/16 Node tests including existing model regression pass.
- [x] Runtime uses original Tab5 render code for Wardrive, Setup, GPS Debug, Home Networks, Blacklist and Upload; the former mini-panels are removed. Radio effects remain simulated; Cleanup/Fix belong to 4.3.
- [x] Browser test `test_emulator_phase4_wardrive.py` written: recording, native Setup/GPS/Home/Blacklist, both upload providers, disconnect/SD, module isolation/cancel and four rotations. **Passed 2026-09-11.**
- [x] Mini-gate + report: all three suites passed; report hashes match the current static build (2026-09-11).

**Gate:** Counts, GPS state, saved sessions and upload outcomes agree with the model. **Passed:** rebuilt after the native-GUI update and reran `tests/verify_emulator_phase4_wardrive.py`; all suites pass and report hashes match the current build. See [4.2 handoff](ui-emulator/Phase_4_2_Report.md).

### 4.3 - File management and deep PCAP views (~72 controls)

Extends the Phase 3 `slice-phase3-pcap.json`: map, tools, extraction, devices,
connections, protocols, health, packet detail; plus invalid and full-storage outcomes.

- [x] Slice `slice-phase4-pcap-deep.json`, plus file-management and extraction overlays.
- [x] Model: richer synthetic captures, invalid file and full-storage fixtures; atomic virtual-SD writes.
- [x] Runtime wiring for native deep views, extraction and file-management screens.
- [x] Browser test `test_emulator_phase4_pcap_deep.py` and mini-gate passed on 2026-09-11.
- [x] Full build and automated Chromium mini-gate passed on 2026-09-11, including five PCAP browser cases (6.19 s in the final run).

Implementation handoff: [Phase 4.3 report](ui-emulator/Phase_4_3_Report.md).
Lightweight model checks and C syntax checks passed. This does **not** mark 4.3 verified.
Wardrive Cleanup supports virtual sessions; Fix explicitly rejects their JSON format.

**Gate:** Every deep sub-view renders and agrees with the capture; invalid/full-storage fail visibly.

### 4.4 - Network tools (~42 controls)

nmap, IoT, GITM, wpa-sec operation screens and result views.

- [x] Explicit nettools slice overlays, preserving native screen layout.
- [x] Model: simulated nmap/IoT results, GITM capture and wpa-sec outcomes; shared cancellation/busy/module guards. Six new model tests pass.
- [x] Runtime wiring for four tool flows, native pickers/dialogs and result views; Emscripten syntax-only check passes.
- [x] Browser suite `test_emulator_phase4_nettools.py` and gate `verify_emulator_phase4_nettools.py` passed on 2026-09-11.
- [x] Full build and automated Chromium gate passed: 10 Network Tools browser cases (10.07 s); full 4.4 gate 121.53 s including prior stages. See [4.4 report](ui-emulator/Phase_4_4_Report.md).

**Gate:** Each tool runs, completes and shows a result consistent with the model.

### 4.5 - Attacks and Observer live activity (~78 controls)

deauth, karma, rogue, ARP, evil twin, MITM, beacon; plus Observer changing
client/network activity. Simulated results only; no real radio behavior.

- [x] Explicit `slice-phase4-attacks.json` and three tool-group overlays.
- [x] Model: seven offline lifecycles, stop/cancel/reset/disconnect, target-specific MITM PCAP and Observer activity. Six new model tests pass.
- [x] Native screen adapters and cooperative ticks, preserving legacy Phase 3 capture; Emscripten syntax-only check passes.
- [x] Browser suite `test_emulator_phase4_attacks.py` and full regression gate passed on 2026-09-11.
- [x] Full build and automated Chromium gate passed: 11 attack/Observer browser cases (13.35 s); full regression 24 scripts PASS, 0 failures, 135.10 s. See [4.5 report](ui-emulator/Phase_4_5_Report.md).

**Gate:** Attack lifecycle and Observer activity agree with the model; no operation runs real hardware.

### 4.6 - Settings, system and module status (~59 controls)

Extends the Phase 2 settings: internal tools, module status, applicable OTA/update/reboot simulations.

- [x] Slice `slice-phase4-settings-sys.json` plus explicit OTA/SD overlays (2026-09-12).
- [x] Model: module status, update/reboot/SD Admin lifecycle; existing NVS/localStorage settings preserved. Seven new model cases pass.
- [x] Runtime: Module Status, native OTA form/monitor/picker and native SD Admin form/Back confirmation; no real services.
- [x] Browser test `test_emulator_phase4_settings_sys.py` passed (five cases, 6.99 s) in the final 4.6 run.
- [x] Gate `verify_emulator_phase4_settings_sys.py` and [report](ui-emulator/Phase_4_6_Report.md) added.
- [x] Full 4.6 Chromium gate passed on the rebuilt artifact: 26 scripts PASS, 0 failures, 126.95 s. [Evidence](ui-emulator/test-runs/20260912-003423-29c9ca/summary.json).

**Gate:** Module status, tools and update/reboot simulations behave consistently.

### 4.7 - Dialog and back-route audit (closeout)

- [x] Cover dynamic dialogs and branches absent from the 98 render cases: the coverage audit classifies all 452 ledger controls and reports the dynamic dialogs/popups by disposition (310 covered, 6 inert boundaries, 136 not-wired, all catalogued; 15 open candidates identified as next-to-wire). Evidence: [4.7 report](ui-emulator/Phase_4_7_Report.md), [coverage baseline](ui-emulator/phase4-coverage-baseline.json).
- [x] Verify all back routes and every visible control against the coverage ledger: `tools/ui_emulator/audit_coverage.py` + `tests/test_emulator_phase4_audit.py` (five static assertions) reconcile every control and all 10 back routes against `generated/manifest.json`; 0 uncategorized, 0 uncovered back routes. Any drift from the committed baseline fails the gate.
- [x] Roll all Phase 4 sub-gates into one `verify_emulator_phase4.py`: runs the cumulative 4.6 gate (chains 4.5 -> 4.4 -> 4.3 -> 4.2 -> 4.1 -> Phase 3 -> Phase 2) then the audit on one static build; writes [phase4 verification](ui-emulator/phase4-verification.json).

**Gate:** Every ledger item has behavior evidence. Any unsupported item remains open and is clearly identified in the demo. **Static audit passes** on the current build artifacts (0 uncategorized, 0 uncovered back routes); run `tests/verify_emulator_phase4.py` after a rebuild for the full Chromium + audit acceptance.

**Features surfaced by the 4.7 audit (were never in any Phase 4 sub-scope):**

- [x] **Deauth detector** (`show_deauth_detector_page`) - **wired**: native render + intercepted UART task + `app_deauth_tick()` feeding `parse_deauth_line()`.
- [x] **Anti-surveillance** (`show_antisurv_page`) - **wired**: native page + intercepted monitor task + `app_antisurv_tick()` flags followers; the retained stop reader drains a synthesized "Devices seen: …" summary through the `transport_read_bytes_tab` adapter.
- [x] **Handshaker** (`show_handshaker_popup`) - **wired**: reachable via the "Handshaker" attack action (`attacks_routes.c`); intercepted monitor task + `app_handshaker_tick()` drives the popup through `append_handshaker_log()` to a captured handshake + `handshaker_set_done_state()`.
- [x] **SAE overflow popup** (`show_sae_popup`) - native popup wired to an offline single-target operation, STOP, disconnect/cancel/reset and independent Grove/MBus lifecycles. Four rotations verified. See [SAE follow-up](ui-emulator/SAE_Report.md).
- [x] **Global handshaker** confirm/active popups - offline job with bounded synthetic handshake counts and last SSID.
- [x] **Blackout** confirm/active popups - offline job, progress and native STOP.
- [x] **SnifferDog** confirm/active popups - offline job, progress and native STOP.

**Global WiFi follow-up (2026-09-12):** The previously unassigned planning gap is
implemented. All three actions route through native confirmations and active
screens. Busy/disconnected refusal, cancellation, reset and four rotations pass
browser acceptance; global Handshaker also verifies Grove/MBus isolation. The
focused gate passed 11 Node and seven browser cases (including SAE regression).
Nine controls moved to covered: 328 covered, 118 not-wired. These are offline
simulations, with no radio effects or handshake files. See the
[Global Attacks report](ui-emulator/Global_Attacks_Report.md).

All three wired features share `slice-phase4-detectors.json`, `runtime/detectors.c`, model methods in `device.mjs`/`bridge.mjs`, tests `test_emulator_detectors.mjs` (passing) + `test_emulator_phase4_detectors.py` (3 classes), gate `verify_emulator_phase4_detectors.py`. Independent browser rerun (2026-09-12): 4 of 6 cases passed; Deauth start/stop and Handshaker cases timed out before emulator initialization (68.51 s total). No hardware acceptance claimed. Full catalogue: [coverage baseline](ui-emulator/phase4-coverage-baseline.json).

## Phase 5 - Guided stories and persistence

- [x] **AP Radar follow-up:** Scan > select exactly one network > Radar now shows native radar geometry, animated sweep and synthetic RSSI. STOP/Back, busy rejection, cancellation, disconnect and independent Grove/MBus lifecycles pass browser acceptance in four rotations. [Radar report](ui-emulator/Radar_Report.md).


- [x] **Time settings follow-up:** native date/time picker, calendar validation, running virtual clock, 12/24-hour format, visibility, manual DST shift, reload persistence and reset. See [Time report](ui-emulator/Time_Report.md).
- [x] **Transfer Speed:** native baud selector, all nine firmware rates, persistence, invalid-value recovery and reset. Four orientations and denied-disk fallback verified. This stores the preference; physical UART throughput is not simulated.
- [x] **Observer exit confirmation:** native Keep running / Stop and exit dialog now guards Back from an active session. Four rotations and Observer lifecycle regression pass. See [settings follow-up](ui-emulator/Settings_Followup_Report.md).
- [x] **INTERNAL Ad Hoc Portal & Karma demo:** native network/template selection and active page show a synthetic client connecting, opening the portal and submitting a fixed demo password. Evil Twin and Rogue AP show the same sequence. Seven control templates are now wired; the remaining 11 Karma2/phishing controls remain deferred. See [Portal demo report](ui-emulator/Portal_Demo_Report.md).


- [x] Implement versioned native-settings persistence, legacy migration and invalid/unknown-schema recovery. Seven Node cases and six Chromium lifecycle/loading cases pass; see [5.1 report](ui-emulator/Phase_5_1_Report.md).
- [x] Implement complete demo reset and sessionStorage fallback; preserve settings through the native rotation/restart flow. If both storage tiers fail, edits last until reload and reset intent remains in the URL to suppress stale settings.
- [x] **Scope decision (2026-09-12):** Keep virtual SD and generated demo files volatile, in memory for the current session only. Reload/reset discards them. Persistent virtual SD is excluded by user decision; no IndexedDB or other durable file storage is planned.
- [ ] Add browser controls for scenario, rotation, fit/full screen and demo reset.
- [x] Keep the visible Demo indicator, expose storage fallback/recovery status, and retry interrupted Wasm downloads at most three times before a recoverable loading error. Two browser cases inject download failures.
- [x] Define the first Bluetooth story using instructions, native events, ordered completion conditions and flow references in `web/stories.mjs`.
- [x] Deliver one complete guided Bluetooth story: discover Grove devices, locate NEURODECK-07, observe RSSI and stop. [Report](ui-emulator/Guided_Bluetooth_Report.md).
- [ ] Extend guided mode to the remaining reviewed manual-story topics.
- [x] Support guide-only restart and switching to free exploration; progress stays volatile and fullscreen retains the active guide.
- [ ] Generate flow views and remaining story screenshots from scenario definitions. Bluetooth screenshots are already generated by native acceptance tests.

**Gate:** Stories advance from actual application state; reset/reboot behave consistently, and the guide remains usable in all orientations.

## Phase 6 - Verification and compatibility

- [ ] Run deterministic scenario and parser/storage checks.
- [ ] Automate navigation, dialogs and full workflows with Playwright.
- [ ] Verify pointer mapping, scrolling and typing in every rotation and representative viewport size.
- [ ] Compare matching seeded states against existing LVGL image references.
- [ ] Test stale events, repeated navigation, cancellation, reboot and scenario switching for crashes/leaks.
- [ ] Test Chromium, Firefox, WebKit and real mobile touch on a supported device.
- [ ] Verify measured performance against Phase 2 budgets.
- [ ] Build firmware and run relevant regressions after shared production-code changes.

**Gate:** Record passing results and explained visual differences. Browser tests do not close pending physical Tab5 display/touch tests.

## Phase 7 - GitHub Pages delivery

- [ ] Generate static HTML, JavaScript, WebAssembly and scenario assets.
- [ ] Share site assembly between both existing Pages publishing workflows.
- [ ] Preserve the flasher, firmware assets/manifest and `/tab.stories/` in the combined artifact.
- [ ] Update build triggers and coordinate publishers to prevent incomplete/stale overwrites.
- [ ] Test `/emulator/` and assets under the repository URL prefix.
- [ ] Add links between manual stories and matching emulator stories.
- [ ] Verify an assembled preview, then publish when implementation is authorized for delivery.
- [ ] Smoke-test the deployed emulator and existing tools.

**Gate:** Published emulator and existing site features work from the same complete artifact.

## Next action and blockers

**SAE follow-up (2026-09-12):** SAE is now wired; its focused gate passed eight Node and three browser cases in 11.87 s ([summary](ui-emulator/test-runs/20260912-145145-fe6dcd/summary.json)). The full Phase 4 gate includes `verify_emulator_sae.py`. The audit changed by exactly one native control (318 to 319 covered). A separate earlier attack-browser regression still has four startup timeouts; global Handshaker was subsequently wired in the Global WiFi follow-up.

**Previous full regression accepted (2026-09-12):** The user ran the rebuilt cumulative
`verify_emulator_phase5_settings.py` gate after the Mesh Recon and independent
Network Observer fixes: **49 scripts PASS, 0 failures, 257.54 s**.
The local [summary](ui-emulator/test-runs/20260912-204801-792f6f/summary.json)
was checked and reports PASS. This supersedes the earlier failed cumulative
startup attempts; some focused native tests route the built Wasm through
Playwright, so this is not a claim that every HTTP transfer path is fixed.

**Next action:** Follow [Stories TODO](ui-emulator/Stories_TODO.md), starting with S22
(global Portal and additional Karma2). S01-S21 and S27 are complete. Nmap portrait accessibility is verified.
Continue Phase 6 browser/mobile acceptance. All eight former open dialog controls are
now wired and verified. Virtual SD remains session-only by user decision.

**Known limitations:** Intermittent localhost HTTP resets have occurred in
unrouted browser tests. They did not block the accepted full run. SubGHz stays
deferred by user decision. Physical-device and cross-browser validation remain
Phase 6 work.

**Historical task boundary (before S01-S03/S27, 2026-09-12):** First guided Bluetooth story and the
eight dialog controls have focused acceptance (18 cases, 4 scripts, 39.13 s).
The cumulative attempt passed 22 scripts before `test_emulator_browser.py` timed
out during Wasm startup with localhost connection resets; subsequent suites did
not run. [Failure record](ui-emulator/test-runs/20260912-211423-66ac5d/summary.json).
Other stories remain open; persistent virtual SD is explicitly out of scope.
Changes remain in the working tree; no commit, push or deployment.
