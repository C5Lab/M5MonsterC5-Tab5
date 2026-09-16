# Phase 1 - Inventory, Data Contracts and Parser Verification

Date: 2026-09-09.

Subsequent [accepted decisions](Accepted_Decisions.md) defer SubGHz and settle default timing/module preferences. Phase 1 is now complete for that scope; [Control and Adapter Contracts](Control_Adapter_Contracts.md) and [fresh verification](phase1-verification.json) record the closing work.

## Outcome and completion boundary

The source inventory, scenario schema, story mapping, source-preserving control contracts, frozen identifiers, explicit adapter decisions and five-family parser verification are complete. This is a verified specification baseline for implementation, not evidence that the live UI works.

The closing decision is to retain original C control behavior as the contract: event filters, constructor conditions, handler guards, branch order, assignments, calls and user-data interpretation. Device operations receive explicit adapter contracts. This avoids inventing a separate navigation/state implementation from names alone. All registration sites have contract entries and lifecycle/binding requirements; actual browser outcome tests remain in later phases. The Home/Settings/Scan prototype is not implemented.

## Evidence produced

| Artifact | Result |
| --- | --- |
| [Source inventory](../../tools/ui_emulator/coverage.json) | 19 C files, 1,593 function definitions, 452 event registration sites |
| Callback tracing | All 452 sites have one or more source-resolved candidate callbacks, including helper parameters, conditional callbacks and local tables |
| Boundary candidates | 1,189 device calls, 494 scheduler calls and 62 filesystem calls; categories are proposed from API names |
| [Dependency registry](../../tools/ui_emulator/dependency-registry.json) | 531 explicit symbol decisions; no unresolved policy categories or default no-op substitutions |
| Translation-unit declarations | 990 declarations recorded, including prototypes; this is not a count of 990 mutable globals |
| Flow references | 110 current route entries; earlier documentation reported 108, so use the current generated inventory |
| Manual stories | All 44 numbered steps mapped to current functions with branch notes; middle-man narrative mapped separately |
| [Scenario schema](../../tools/ui_emulator/scenario.schema.json) | Versioned networks, LAN, clients, BLE, modules, settings, GPS, signals, files, jobs and scenario variants |
| [Parser report](parser-test-report.json) | Actual production parsing code compiled with AddressSanitizer and UndefinedBehaviorSanitizer |

Source hashes tie the inventory to the inspected files. Function IDs include the source path. Inventory ordinals remain evidence locators; the separate frozen identity manifest supplies permanent template IDs without source line/order dependence. Runtime instances add logical screen, module, entity and semantic slot.

The 452 registration sites include the global input-device activity callback as well as object callbacks. Indirect calls, platform helpers and the separate HTTP/UART transfer boundaries have explicit decisions. No stub code is generated. There are 406 current-scope contracts and 46 deferred SubGHz contracts.

## Parser verification

The test runner extracts production definitions using the C syntax tree. It compiles `trim_ascii_whitespace`, `parse_csv_mixed_fields`, `parse_network_line`, `parse_bt_device_line`, `parse_probes_from_buffer`, and `fetch_html_files_from_sd`. It also extracts the exact ARP host parsing block using unique source anchors.

Coverage:

- Wi-Fi and Bluetooth populated response bodies at chunk sizes 1, 2, 7, 31, 512 and 4096 bytes, checking decoded fixture fields.
- HTML listing through the complete production fetch function with substituted UART and clock, including the same six chunk sizes.
- Probe deduplication across repeated Grove/MBus responses.
- LAN host parsing, own-IP extraction and exclusion of ICMP-only entries without a MAC.
- Quoted CSV names containing a comma and escaped quotes, malformed/zero-index rows, status lines and empty results.
- HTML empty/error responses and no-response timeout; empty probe results and BLE error/summary lines.

Limits: Wi-Fi/BLE chunk reassembly uses a test adapter around actual line parsers. It does not execute their full receive tasks. Hosts/probes are checked as accumulated response buffers. Binary transfer, UI updates, actual cancellation races and hardware behavior are not covered by this suite.

The fixture initially lacked `Our IP`, which the Tab5 uses in its LAN status. JanOS emits this in `discover_lan_hosts` around line 15026. The response now includes a synthetic own-IP/netmask line and the test asserts its decoded value.

## Data ownership and lifecycle contract

| State | Owner | Lifetime and reset rule |
| --- | --- | --- |
| Network/client snapshots and selections | Device context keyed by transport/module | Replace on the corresponding scan; preserve IDs while rows refer to the same entity; never write another tab's context |
| Observer activity | Module operation plus its snapshot | Stop/cancel explicitly; navigation alone follows the production background-operation policy |
| BLE discovery and tracking | BLE-capable module | Mode transition invalidates incompatible radio jobs; the current firmware's shared BLE globals require an explicit adapter policy |
| Portal credentials and result history | Simulated device/result store | Synthetic only; persist only as a virtual artifact when the scenario outcome requires it |
| File contents and metadata | Virtual filesystem | Listing size, transfer bytes and analysis must derive from the same contents; planned files are not downloadable |
| LVGL objects, event user data and display buffers | Runtime UI instance | Never serialize pointers; rebuild after reboot/reset; detach callbacks before destroying their owner |
| Timers, task handles and cancellation flags | Runtime scheduler/job registry | Never serialize native handles; each event carries a job ID and generation |
| Rotation, theme and selected preferences | Settings store | Simulated reboot preserves accepted preferences; full demo reset restores the selected seed |
| Clock/GPS position | Scenario clock and GPS model | Elapsed monotonic time is independent of API call count; no-fix is an explicit state |
| Transport buffers | Per-module adapter | Preserve partial lines/frames; reset only at an explicit flush, mode transition or cancellation boundary |

These simulator policies are specified for implementation. Differences from production background behavior must be explicitly reviewed; they cannot be introduced as silent callback substitutions.

## Required scenario invariants

1. Every referenced network/module/file ID exists. Entity IDs and file paths are unique within their collection.
2. SSIDs fit the production 32-byte limit; name length checks use UTF-8 bytes, not only character count. Security labels and band/channel relationships follow the producer contract.
3. Network names, BSSIDs, client associations and result files agree across all screens. A rescan must reconcile selections by entity identity rather than assuming a stable row index.
4. A module cannot run incompatible radio jobs simultaneously. Grove and MBus use independent operation state; internal portal state is not automatically JanOS state.
5. Job states are `queued -> running -> completed | failed | cancelled`. A terminal job cannot resume or publish another result without a new ID/generation.
6. Cancellation invalidates pending events before freeing UI state. Late replies cannot complete a new operation with the same visible screen.
7. A completed file has actual contents and a matching size/checksum. A cancelled/failed transfer does not appear as a valid completed capture.
8. Simulated reboot rebuilds transient state and preserves explicitly persisted settings/files. Full reset discards jobs and restores the seed; neither operation serializes pointers or handles.
9. Populated, empty, disconnected, timeout and failure requirements are defined for Wi-Fi, Observer, Bluetooth, files, operations, Wardrive and SubGHz. Current variant entries specify expected behavior; executable overrides are future implementation.
10. Unknown metadata remains unknown. Locally administered synthetic addresses must not acquire fabricated real-vendor identities through lookup.

## Story review findings

The mappings and notes live in [story_contracts.py](../../tools/ui_emulator/story_contracts.py) and are included in the generated inventory.

- Bluetooth stories describe discovery, tracker detection and locating; these are related branches, not one mandatory sequence.
- Global and scan operation galleries show alternative operations. Running them all simultaneously would misrepresent radio ownership and preconditions.
- The Observer/Karma story crosses external observation and the internal portal. Its historical C6 wording needs a current-product wording review.
- The WPA-SEC key file is an external prerequisite; checking the remote website is not a Tab5 screen. The simulator must represent a local synthetic outcome rather than claiming a real upload.
- Middle-man has explanatory sections rather than the image-array format. ARP MITM, GITM and Rogue GITM map to their respective current screen constructors.

## Resolution of the closing items

| ID | Remaining work | Why it matters |
| --- | --- | --- |
| P1-SEMANTICS | Closed by source-preserving handler contracts with explicit preconditions, effects, owners and lifecycle rules | Future browser tests must verify that the implementation follows these contracts |
| P1-BOUNDARIES | Closed by frozen decisions for all 531 external symbols | Adapter implementation and portability checks belong to the browser/runtime phases |
| P1-RUNTIME-IDS | Closed by permanent template IDs plus a required semantic instance-key format | Browser constructors must supply concrete binding values in Phase 2 |
| P1-SUBGHZ | Deferred by user decision; removed from current gates and seed | No SubGHz producer repository is required for this milestone |
| P1-EXPANSION | Schema and behavior requirements complete; populated adapters/files remain later-phase work | Exact additional output bodies must be verified when their adapters are implemented |

No firmware source or JanOS source was edited. No USB device, radio operation, flash or deployment was used.

## Reproduction

Use an isolated environment. Windows commands from the repository root:

```powershell
py -3.13 -m venv tools/ui_emulator/.venv
tools/ui_emulator/.venv/Scripts/python.exe -m pip install -r tools/ui_emulator/requirements.txt
tools/ui_emulator/.venv/Scripts/python.exe tools/ui_emulator/inventory.py
tools/ui_emulator/.venv/Scripts/python.exe tools/ui_emulator/dependency_registry.py
tools/ui_emulator/.venv/Scripts/python.exe tools/ui_emulator/scenario_schema.py
tools/ui_emulator/.venv/Scripts/python.exe tests/validate_emulator_inventory.py
tools/ui_emulator/.venv/Scripts/python.exe tests/run_emulator_parser_tests.py
tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_phase1.py
```

Parser compilation requires WSL with `cc` on Windows, or native `cc` on Linux. The simulator itself has not been built. The Python dependencies are isolated to avoid replacing dependencies used by unrelated local projects.
