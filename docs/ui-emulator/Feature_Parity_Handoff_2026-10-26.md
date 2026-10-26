# Tab5 browser emulator feature parity handoff (2026-10-26)

## Reference and delivery

The Tab5 firmware UI at `034b12ad` is the route and control reference. This
work was prepared on `codex/emulator-parity` and transferred to `development`.
Production LVGL screens and handlers run in Wasm where practical. Device I/O is
replaced by deterministic local models. The user confirmed that full Sub-GHz
belongs in this scope.

## Added workflows

| Area | Browser behavior |
| --- | --- |
| Wi-Fi Analyzer | Native scan, chart, AP details, channel proposal, filters, sort, zoom, acquisition settings, cancellation and guided journey, using synthetic WFA/1 responses. |
| WPA PSK Auditor | Native catalogue, local sync, per-file CRACK routing, scripted wordlists and batch audits, pause/resume/checkpoints, history and demo results. Fixture content, source and path must all match. |
| Sub-GHz | Native Quick Scan, Hunter, Listen, SD Signals, Weather, Jammer, Tesla and settings screens; synthetic signals and volatile per-module captures. |
| NFC | Native read/list/detail/emulate journeys with synthetic NTAG and Classic cards and volatile files. |
| Other Tab5 routes | Bluetooth AirTag and nRF24, Settings Tool Order and Screen Lock, Compromised Data passwords, Portal/Karma, Rogue GITM, Observer exit/CSV export, OTA variant choice, screenshot download, and Wardrive automatic upload result, archive and preference controls. |

The virtual module and SD availability states are tested separately from the
default connected demo. Radio, capture, cracking, credential and upload results
are local fixtures; no actual RF operation, password verification, service
upload or physical file write occurs. Session-generated virtual SD data clears
on reload/reset; fixed seed fixtures are reloaded when the page starts. WPA
history and paused checkpoints can persist independently of those files.
The Show Vendors switch uses a synthetic vendor-capability flag on a present
virtual SD card; it does not read a real `oui_wifi.bin` file.

## Coverage and explicit boundaries

The [control audit](phase4-coverage-audit.json) checks every firmware control
registration against a frozen source inventory and checks back routes. Its
baseline and source hashes fail on unreviewed changes. Of 518 registrations,
371 retain native behavior, 98 use adapters, 33 are documented workflow
replacements, and 16 are deliberately not wired; none are uncategorized or
classified as unsupported. All 10 recorded Back routes are covered. The 16
non-simulated registrations comprise four physical boot/display sleep callbacks,
two ESP Modem callbacks in `Internal C6 Test` without a firmware menu tile, and ten controls
in the WiGLE Wi-Fi credential picker. The retained WiGLE Send/Sync/Force All controls
use the [offline upload model](WiGLE_Upload_Prompt_Audit.md); the emulator never
asks for a real Wi-Fi password to simulate that upload.

The 33 replacement entries are reviewed routing decisions, not 33 independent
behavioral tests. The browser suites exercise their representative journeys;
the audit itself verifies classifications, links to runtime/tests, source
freshness and Back-route coverage.

## Verification

Build with the pinned Docker/Emscripten image, then run
`tests/verify_emulator_feature_parity.py`. The script runs the new model and
Chromium suites plus the cumulative Phase 5 gate against one unchanged static
bundle, and writes a local `feature-parity-verification.json` report and
timestamped `test-runs/` logs. These generated files are ignored by Git.
The final acceptance run passed 18 parity checks and 89 cumulative scripts
with no failures; the local report recorded the verified bundle hashes.
The [performance report](phase2-performance-report.json) counts all published
assets, including nested model modules, against the download, desktop and
throttled-mobile budgets. See the [preview instructions](../../tools/ui_emulator/README.md)
for a local browser session.

This is host and Chromium evidence. It does not establish ESP32 firmware,
physical radio/SD, a real phone, Firefox/WebKit, or published Pages behavior.
