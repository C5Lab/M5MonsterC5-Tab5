# JanOS Dummy Data Research

For the running emulator, see [manual startup and walkthrough](../../tools/ui_emulator/README.md)
and [current progress](../Live_Emulator_TODO.md). The producer research below is
the initial protocol checkpoint, not the current browser implementation status.

Reviewed: 2026-09-09. Source checkout: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5`. Repository HEAD: `36911a31dea327800d82ed71d9b24a2e80ff9d20`. The seed records the inspected `main/main.c` SHA-256 because a commit alone does not identify uncommitted source changes.

## Deliverables and status

- [Cyberpunk seed](neon-district.seed.json): twelve Wi-Fi networks, three associated clients, six Bluetooth devices, probe references and two portal filenames. The initial six-network fixture was expanded for browser scrolling coverage.
- [Response examples](neon-district.responses.json): five synthetic response bodies based on inspected JanOS output statements.

The scenario now drives the browser's scan, Observer, clients and synthetic PCAP workflow. The response examples remain test fixtures rather than a complete protocol recording. Five response families are exercised through actual Tab5 parsing code; see the [Phase 1 report](Phase_1_Report.md). Full hardware Wi-Fi/BLE receive-task execution is outside the emulator.

Phase 1 is complete for the accepted scope without SubGHz. [Control and Adapter Contracts](Control_Adapter_Contracts.md) define the behavior/ownership decisions and stable identities; [phase1-verification.json](phase1-verification.json) records the passing specification gate. Live browser execution begins in Phase 2.

Cyberpunk naming is the user-selected convention. All addresses and names are synthetic. Locally administered Wi-Fi MAC addresses deliberately have empty vendor fields; fictional device labels do not imply real vendor lookup results. Client labels are scenario metadata because the inspected `list_hosts` response does not carry hostnames. Documentation IP addresses identify the virtual LAN.

## Confirmed producer contracts

Source line references below are in the inspected JanOS `main/main.c` unless stated otherwise.

| Output | Confirmed contract | Evidence |
| --- | --- | --- |
| Logging | `MY_LOG_INFO` emits the formatted message and a newline without an ESP log prefix | Line 208 |
| Wi-Fi results | Eight quoted CSV fields: index, SSID, vendor, BSSID, channel, security, RSSI, band | `print_network_csv`, line 5072 |
| Wi-Fi completion | `Scan results printed.` follows all rows; indexes are one-based | `print_scan_results`, line 5094 |
| Security labels | Exact strings include `Open`, `WPA2`, `WPA3`, `WPA2/WPA3 Mixed` | `authmode_to_string`, line 4315 |
| LAN hosts | Header, ARP/ICMP rows, separator, then counts; header is not completion | Lines 15175-15191 |
| Probe list | Unique SSIDs, one-based `index SSID` rows; no explicit final marker | `cmd_list_probes`, line 17331 |
| HTML files | Header followed by `index filename`; separate empty-list message | Lines 18906-18916 |
| Bluetooth list | Start message, results header, device count, indexed MAC/RSSI rows with optional name, summary | `bt_scan_task`, lines 23796-23872 |
| Bluetooth locate | Approximately ten-second scan cycles; MAC plus RSSI and optional name, or `not found` | `bt_tracking_task`, line 23951 |
| Tracker counts | AirTag/SmartTag counts emitted as `count,count` during continuous scanning | Lines 23926-23927 |
| Network inspection guards | Requires completed scan and valid index; conflicting operations can require `stop` | `cmd_inspect_network`, line 5336 |

The seed's two first networks share channel 1 to support workflows that require a same-channel choice. Both bands, mixed security, an open network, an unnamed BLE device and varied signal strengths provide useful UI states. Probe rows reference existing network names, and all current clients belong to NEON-BAZAAR.

The response file contains result bodies for `show_scan_results`, `scan_bt`, `list_hosts`, `list_probes`, and `list_sd`. It does not model command echoes, startup logs, transport chunk boundaries, mode transitions, timing, or all error responses. Its `scan_bt` example includes the scan announcement but is stored as one string; a future scheduler must delay results appropriately.

## Important simulator implications

1. Keep byte transport separate from lines and parsed state. Replies may arrive in partial chunks; preserve line buffering and command-specific completion rules.
2. Do not add invented completion markers to commands that currently finish through timeout/idle detection. Reproduce those semantics and test the receiving code.
3. Preserve producer field order and exact security/status strings. Cyberpunk names belong in data fields, not protocol tokens.
4. BLE and Wi-Fi mode transitions need explicit modeling. `cmd_scan_bt` calls `ensure_ble_mode`; its implementation and the Tab5 reconnect handling still need mapping.
5. Timing should use a deterministic clock. Bluetooth tracking code implements approximately ten-second cycles even though one nearby usage comment says two seconds; executable behavior is the stronger reference.
6. Binary file transfer cannot be represented as arbitrary text progress. JanOS `docs/command-manual.md`, Console UART and binary file transfer, specifies `[FT]` metadata, `FTB\x01` frames, indexes, payload length, CRC32, ACK/NAK/CAN, retries, cancellation and final CRC. Preserve metadata order and resume-prefix validation when this adapter is implemented.
7. JanOS also documents `[GPSCFG]` and `[ZIG]` machine responses. Determine which commands the current Tab5 actually consumes before treating these as simulator UI scope.

## Remaining research

- Extend passing line-parser/body tests to full receive-task behavior and additional output families.
- Implement the completed control/adapter contracts, preserving independent Grove/MBus context and transport semantics.
- Map Observer/client detail, connection state, operation progress/results, captured-data formats and upload outcomes.
- Map Wardrive/GPS, capture gateway, file listing/download and virtual file contents.
- SubGHz producer research is deferred by user decision and is outside the current milestone.
- Expand to large lists, hidden/escaped SSIDs, unnamed devices, disconnects and timeouts after parser compatibility is established.
- Add schema validation and repeatable protocol fixture generation as implementation work; do not maintain unrelated hand-edited copies of the same model.

The inspected JanOS checkout was read only. No hardware operations were run.
