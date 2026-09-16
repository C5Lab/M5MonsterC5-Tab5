# Phase 4.7 - Dialog and back-route audit (closeout)

Static closeout for Phase 4. It reconciles the coverage ledger against the
built emulator so that every ledger item has behaviour evidence or is a clearly
identified open item, and it rolls the per-domain sub-gates into one gate.

## Wired in 4.7: Deauth Detector, Anti-surveillance, Handshaker

Three previously-inert features were made live, all as offline simulations that
render byte-exact firmware pages, intercept their infinite UART monitor task in
`xTaskCreate()`, and drive the UI from the frame loop:

- **Deauth Detector** (`show_deauth_detector_page`): `app_deauth_tick()` feeds
  synthesized `[DEAUTH] ...` lines through `parse_deauth_line()` into the table.
- **Anti-surveillance** (`show_antisurv_page`): `app_antisurv_tick()` flags
  followers from the scenario BT devices and appends the red rows; the retained
  Stop callback drains a synthesized "Devices seen: D, followers flagged: F"
  summary through the `transport_read_bytes_tab` adapter.
- **Handshaker** (`show_handshaker_popup`): reachable via the "Handshaker" attack
  action added to `attacks_routes.c`; `app_handshaker_tick()` drives the popup
  through `append_handshaker_log()` to a captured handshake and
  `handshaker_set_done_state()`. UART/observer preparation is stubbed by adapters
  (`observer_handshaker_prepare_selection`, `wait_for_observer_uart_idle`,
  `usb_flush_input`).

- Slice: `tools/ui_emulator/slice-phase4-detectors.json`.
- Model: `SimulatedDevice.deauthEvent/antisurvFollower/handshakerTarget` +
  `BrowserDevice.deauthDetectorLine/antisurvFollowerLine/handshakerTarget`.
- Runtime: `tools/ui_emulator/runtime/detectors.c`, wired into `application.c`
  (forward decls, three task interceptions, ticks, command recognition).
- Tests: `tests/test_emulator_detectors.mjs` (model, passing) and
  `tests/test_emulator_phase4_detectors.py` (3 browser classes); gate
  `tests/verify_emulator_phase4_detectors.py`.

This moved 8 controls from not-wired to covered (310 -> 318 covered; 136 -> 128
not-wired). Follow-up: [SAE Overflow is now wired](SAE_Report.md), adding one retained control
(328 covered, 118 not-wired after the Global WiFi follow-up). Other deferred scope remains open.

## What landed

- `tools/ui_emulator/audit_coverage.py` - the audit engine. For every control
  in `coverage.json` it resolves the firmware handler through
  `control-contracts.json` and classifies it against `generated/manifest.json`:
  `retained`, `adapter`, `unsupported` (inert boundary) or `not-wired` (sliced
  out). Every `not-wired` control and every `not-wired` back route is matched to
  a documented category; anything unmatched is reported `UNCATEGORIZED` and
  fails the gate, forcing a triage decision instead of shipping a dead control.
- `docs/ui-emulator/phase4-coverage-baseline.json` - the committed baseline
  snapshot. Regenerate deliberately with
  `python tools/ui_emulator/audit_coverage.py --write-baseline` after a slice
  change that wires or unwires a control.
- `tests/test_emulator_phase4_audit.py` - five static assertions (no browser):
  no uncategorized control, all back routes covered or deferred, every open item
  carries a reason, live disposition matches the baseline (regression guard both
  ways), and the control total is conserved. Writes
  `docs/ui-emulator/phase4-coverage-audit.json`.
- `tests/verify_emulator_phase4.py` - the Phase 4 roll-up gate. Runs the
  cumulative 4.6 gate (which chains 4.5 -> 4.4 -> 4.3 -> 4.2 -> 4.1 -> Phase 3 ->
  Phase 2) on one static build, then the coverage audit. Writes
  `docs/ui-emulator/phase4-verification.json`.

## Coverage ledger result (from the current build artifacts)

| Disposition | Controls |
| --- | --- |
| Covered (retained 257 + adapter 88) | 345 |
| Unsupported boundary (inert by design) | 6 |
| Not-wired, catalogued | 101 |
| **Uncategorized (must be zero)** | **0** |
| **Total** | **452** |

Back routes: 10 total; 3 covered (`pcap_viewer_back_cb` x2 retained,
`beacon_spam_back_btn_event_cb` adapter); 7 deferred (all SubGHz). None
uncovered.

### Not-wired controls by category

Open candidates - reachable dialogs inside otherwise-covered flows, the next
controls to wire:

| Category | Controls | Note |
| --- | --- | --- |
| `dialog-open-candidate` | 2 | Wardrive-home confirmation (Observer exit now wired) |
| `settings-open-candidate` | 3 | Version/OTA-slot dialogs (Time now wired) |
| `utility-dialog-open-candidate` | 3 | No-board / SD-warning popups (Transfer Speed now wired) |

Deferred / out of current scope (identified, not implemented):

| Category | Controls | Reason |
| --- | --- | --- |
| `subghz-deferred` | 46 | SubGHz deferred by user decision |
| `adhoc-karma-portal` | 11 | Remaining Karma2/phishing screens; INTERNAL demo now wired |
| `wardrive-wigle-wifi` | 11 | Real Wi-Fi join / credential entry out of scope |
| `device-chrome` | 10 | Splash / sleep / screen-lock hardware behaviour |
| `rogue-gitm-deferred` | 6 | Deferred with the GITM rework |
| `bluetooth-detector-unsupported` | 4 | AirTag scan + nRF24 jammer (Phase 4.1) |
| `evil-twin-extra` | 3 | Connect popup + password list beyond the lifecycle |
| `esp-modem-not-simulated` | 2 | ESP modem serial tool drives real hardware |

## Gate

Every ledger item has behaviour evidence (covered), is an explicit inert
boundary, or is a catalogued open item with a reason. Any unsupported item
remains open and is named in `phase4-coverage-audit.json` for the demo.

**Independent verification (2026-09-12):** All five audit assertions and five
detector model cases passed. The direct detector Chromium suite passed four
cases; Deauth start/stop and Handshaker timed out before emulator initialization
(six cases, 68.51 s). The cumulative run also failed during startup in the earlier
lifecycle suite; later domain gates did not run. A network probe reproduced local
Wasm `ERR_CONNECTION_RESET`. Full acceptance remains open. See the
[latest cumulative run](test-runs/20260912-125312-25a13f/summary.json) and
[Phase 5.1 report](Phase_5_1_Report.md).

## Not verified here

- Firmware build and a complete independent passing Chromium run.
- Behaviour of covered controls is proven by the per-domain browser gates, not
  by this static audit.
- Physical hardware, cross-browser and Nmap portrait accessibility remain open.
