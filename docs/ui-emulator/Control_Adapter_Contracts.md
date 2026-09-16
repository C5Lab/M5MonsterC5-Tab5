# Control and Adapter Contracts

## Phase 1 decision

The production C handlers are the behavioral specification for the emulator. Retain their event filters, guards, branch order, user-data interpretation, state writes and navigation. Provide simulated results at explicit device boundaries. Do not create an independent JavaScript navigation model or substitute successful no-op callbacks.

This resolves the control-contract decision without inventing a second description of every branch. Each entry in [control-contracts.json](../../tools/ui_emulator/control-contracts.json) includes the actual callback guards, assignments with values, calls and reachable-function evidence. These are implementation contracts, not claims that the browser has executed those controls.

The snapshot contains 452 event registration sites: 406 in the current scope and 46 deferred SubGHz sites. Shared dashboard helpers remain available, but their SubGHz actions must be filtered out when constructing the current profile. The presence of deferred code in the inventory does not enable it in the simulator.

## Concrete control behavior

| Control family | Preconditions and ownership | Required outcome |
| --- | --- | --- |
| Dashboard and transport navigation | Live UI session; destination module available in the selected profile | Run the original tile/tab callback with the concrete action and context; apply existing page hide/show behavior |
| Wi-Fi network checkbox | Current context exists; row index resolves to the same network represented by the row | Preserve deduplication and selection limits; update that context's selected indices; do not invent an immediate device command |
| Scan start/rescan | Existing transport selection and scan/task guards | Run a scheduled scan with the originating context; pass real-format responses to retained parsing and row updates |
| Bluetooth rescan | Active BLE page and valid module profile | Run the original `show_bt_scan_page` path, including its existing-page behavior; do not replace it with a guessed list refresh |
| Bluetooth device selection | `0 <= device_idx < bt_device_count` | Open the locator for that device; bind its stable device ID so row reordering cannot silently change the target |
| Scan-time save | Connected module and its input widgets; each module's minimum must be lower than maximum | Retain validation/error feedback and the original per-module command/settings behavior |
| Red Team switch | Current setting and switch state | Retain disclaimer/confirmation and dependent-page invalidation; default-enabled fixtures do not remove these controls |
| Rotation dropdown | Valid original enum selection | Save the selected setting through virtual NVS; preserve the separate restart action and rebuild the simulated display on restart |
| Password/SSID confirmation | Nonempty validated input and captured continuation | Copy text/callback before closing the popup; invoke the captured callback once; never serialize the function pointer |
| Continue without SD | Pending continuation may be null | Capture continuation before popup cleanup; acknowledge for the action; invoke only if non-null; reset acknowledgement afterward |
| File copy/download | Source and destination context, cancellation token, valid virtual path | Execute an asynchronous transfer contract; progress, bytes, checksum and final file must agree |
| Delete/draw/focus/scroll events | Matching original event filter and live object ownership | Retain their original purposes. These are not all clickable buttons; deletion callbacks must clean up their resources |

The exact handler records are authoritative where this table summarizes multiple variants. Nmap and ARP have repeated registration expressions in different open-network, saved-password and manual-entry branches. Constructor branch paths distinguish those templates.

## Stable identifiers

[control-identities.json](../../tools/ui_emulator/control-identities.json) freezes one permanent template ID per registration identity. Identity includes the source function, object expression, callback expression, event filter, user-data expression and constructor branch path. It does not include line numbers or registration order.

Actual runtime binding uses:

```text
template_id / screen_id / module_id / entity_id / slot
```

- `screen_id`: the logical screen instance within the UI model, not its memory address.
- `module_id`: Grove, MBus, Internal or another explicitly selected supported profile.
- `entity_id`: a canonical network/client/file/device ID; use `static` for a fixed control.
- `slot`: the semantic role/action supplied by the constructor, such as `select`, `back`, `connect` or `delete`.

Helpers and loop-created rows must receive these binding values from their caller. A current array index resolves to the entity at construction time; the index itself is not a persistent identifier. Reject duplicate active instance IDs rather than appending allocation-order counters.

Reordering or moving existing registrations preserves IDs. Adding/changing a binding or constructor condition requires explicit enrollment/migration review. Source changes also invalidate the recorded contract snapshot. The initial enrollment is checked in as data; ordinary verification never silently enrolls new controls.

## State and lifetime rules

- Preserve the original distinction between per-tab contexts and shared application globals. Shared BLE or popup variables belong to the UI session unless an explicitly reviewed later change separates them.
- An event resolves its module and user data as the original handler requires. A scheduled operation retains the context that started it; switching the visible tab must not redirect its results.
- UI bindings, timers and operation callbacks carry lifecycle generations. Deleted bindings cannot receive queued updates. Rebuilt screens get new lifecycle generations while retaining their logical semantic IDs.
- Keep constructor guards and handler guards distinct. Creating a control conditionally is different from creating it and refusing an action later.
- LVGL timers, animations and event callbacks retain their semantics. Hardware/task scheduling can suspend a blocking operation and resume its continuation; it cannot return fabricated success early or reorder later state writes.
- UI animation time remains independent of the accelerated device-job clock. Device replies and consumer deadlines use the same clock. The initial implementation changes timing mode while idle.

## Adapter decisions

[adapter-decisions.json](../../tools/ui_emulator/adapter-decisions.json) freezes 531 symbol decisions and supporting source hashes. [dependency-registry.json](../../tools/ui_emulator/dependency-registry.json) adds source call sites. There are no unresolved default categories and no permitted default no-op implementation.

| Boundary | Decision |
| --- | --- |
| LVGL and pure UI logic | Retain the repository implementation |
| C library and pure PCAP helpers | Retain compatible behavior; PCAP file operations use the virtual filesystem |
| ESP CRC32 | Preserve the exact seeded CRC algorithm; never return a constant |
| UART/USB/device I/O | Per-module queues, command responses, buffering, availability and errors |
| Wi-Fi, networking and HTTP server actions | Local scenario state and events; no real radio or external upload |
| NVS | Versioned simulated settings and explicit persistence failures |
| Clock, random values and restart | Deterministic scenario clock/seed and simulated lifecycle |
| Task/timer/synchronization APIs | Bounded scheduled jobs, cancellation and cleanup; no accepted-but-unstarted tasks |
| Hardware allocation/telemetry | Host allocations and simulated device telemetry with allocation/error semantics |
| ESP-only TLS deletion branch | Exclude the platform-specific branch in the browser target; still perform scheduler cleanup once |
| Indirect callbacks | Retain the original bound continuation, arguments, lifetime and null checks |

These are architecture decisions for implementation. They do not mean the adapters already exist. Any newly encountered symbol fails the decision check until reviewed.

### Two file-transfer paths

The UART path uses `[FT]` metadata, binary blocks, CRC, ACK/NAK/CAN and its own timeout/resume rules. Preserve it as a protocol adapter.

The separate `janos_file_transfer_download` component is an HTTP transfer interface. Its simulated job must honor the existing config/result/progress structs: query size, download, verify, commit and finish/cancel/error. Keep the `.part` file until successful atomic commit; report bytes, CRC, HTTP status and error consistently. `janos_file_transfer_state_name` is a pure function and is retained.

Do not turn both paths into one arbitrary progress counter. They can share virtual file contents while preserving their distinct protocol/state contracts.

## Verification and handoff

Run from the repository root with the isolated Python environment:

```powershell
tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_phase1.py
```

The check rebuilds the inventory, verifies frozen identities/policies, runs identity stability and negative tests, validates the scenario and runs native production-parser tests with ASan/UBSan. [phase1-verification.json](phase1-verification.json) records the result and artifact/source hashes.

Phase 2 implements browser bindings and the Home/Settings/Scan runtime slice using these contracts. Actual callback execution, input mapping, asynchronous continuation behavior and performance are acceptance tests for that implementation, not completed Phase 1 claims.
