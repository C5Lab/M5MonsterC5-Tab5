# Tab5 Wi-Fi Analyzer Implementation Plan

**Goal:** Deliver the previously authorized JanOS/Tab5 analyzer across both repositories, preserving the legacy scan screens and Flipper behavior.
**Spec:** `projectZero/ESP32C5/docs/wifi-analyzer-protocol.md` and `wifi-analyzer-tab5-prompt.md` in the sibling JanOS repository, plus the user's channel/RSSI chart reference.
**Architecture:** A strict portable WFA/1 reader commits complete snapshots into separate PSRAM storage. A per-port worker owns transport, capabilities, one-shot/repeated acquisition and cooperative cancellation. An LVGL screen renders frequency/RSSI footprints, channel counts, filters and AP details using the existing Tab5 theme.
**Tech stack:** C, ESP-IDF, FreeRTOS, LVGL 9, PSRAM, Python source/contract tests.

## Constraints and decisions

- Work in the user's existing Tab5 checkout, which was clean at entry. No commits or unrelated refactoring.
- Do not compile C, firmware or emulator, flash, or access serial hardware. Author C tests and run compiler-free Python/source checks. Explicitly report deferred target validation.
- This is implementation of the already approved architectural feature; the user's correction explicitly confirms both repositories are in scope. Do not repeat an approval gate.
- Preserve incumbent theme, touch/navigation conventions, legacy scan buffers and selections. The chart reference supplies composition, not a replacement visual identity.
- Default 64 records, maximum 128. Bulk reader/snapshot buffers use PSRAM; no silent internal-RAM fallback.
- AP counts/RSSI/overlap are observations, not measured airtime or spectrum utilization. Unknown width stays unknown.
- English code comments and documentation.

## Tasks

- [x] Portable reader/model (`main/wifi_analyzer_model.h/.c`): strict bounded stream framing, transactions, control replies, geometry, filters, stable AP identity/colors; portable C harness authored, not executed.
- [x] Session/transport (`main/wifi_analyzer.h/.c`, host hooks in `main/main.c`): shared ownership, per-tab state, deadlines, cancellation, disconnect/reboot handling and repeat scheduling.
- [x] LVGL view (`main/screens/wifi_analyzer_screen.h/.c`): additive home tile, chart and counts, sortable AP table, local filters, scan settings, details and lifecycle/error states.
- [x] Compiler-free verification and documentation: focused Python reference/source contracts, source grammar parsing, independent integration review and English [usage and device acceptance documentation](wifi-analyzer.md). On 2026-09-28, 65 selected checks passed and 33 files/fragments parsed without grammar errors. Device rendering and execution remain pending the user's later build.

## UI direction

Operate mode. Extend the current theme. Use a fixed full-width header with
Back/Scan/Stop and a scrollable full-width chart/list workspace. In both landscape
and portrait, Filters / Setup expands a collapsible area above the chart;
controls wrap to the available width. The AP list has its own keyboard-accessible
scroll area. The chart selector shows one band at a time, defaulting to 2.4 GHz.
Frequency determines horizontal distances; channel numbers label the axis.
Stable BSSID colors connect chart footprints and AP rows. Touch controls remain
usable at all existing rotations. No fabricated historical ranges or own-AP marker.
