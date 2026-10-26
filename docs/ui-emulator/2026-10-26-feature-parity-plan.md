# Emulator feature parity plan (2026-10-26)

The firmware source at `034b12ad` is the reference for UI routes and actions. The browser emulator keeps production LVGL screens where practical and replaces device, radio, UART, storage and timed work with deterministic local models. Demo files remain volatile across reload and reset. Full Sub-GHz is in scope, as confirmed by the user.

## Delivery sequence

1. Refresh the inventory against current firmware and emulator source. Record each visible route and callback as retained, simulated, or still blocked; update coverage and communication catalogs. Treat generated files as build output.
2. Wire Wi-Fi Analyzer, WPA PSK Auditor and all Sub-GHz pages in parallel. Give each a realistic offline state model, navigation, cancellation and error states, and focused browser/model tests.
3. Close Settings gaps (Tool Order, Screen Lock), NFC, Bluetooth AirTag/nRF24, remaining Compromised Data, Portal/Karma, Rogue GITM, Observer and OTA controls. Integrate each slice only when its tests and Docker build pass.
4. Run focused tests after each slice, then the full host/browser gate and a manual navigation sweep in portrait and landscape. Document exactly what browser tests prove and list any remaining hardware-only behavior.

## Acceptance

- Every visible firmware tile in the emulator opens a usable screen. Buttons perform an observable local action or explicitly identify a hardware-only boundary.
- Synthetic operations never send real attack, scan, capture, or radio commands. Their state is deterministic and survives tab changes as the firmware flow requires.
- Virtual SD files exist only for the browser session. Settings that firmware persists use emulator settings storage and Reset clears them.
- Browser console has no uncaught error or unbound callback in the covered journeys. Docker build and focused Playwright/model tests pass; the broad gate passes after integration.
- Coverage metadata, stories, accepted decisions and handoff report match the implemented build.

## Work allocation

- Wi-Fi Analyzer: isolated `codex/emulator-analyzer` branch.
- WPA PSK Auditor: isolated `codex/emulator-wpa` branch.
- Full Sub-GHz: isolated `codex/emulator-radio` branch.
- Integration, Settings, NFC and remaining routes: `codex/emulator-parity` branch.

The original `development` checkout is not modified by this work.
