# Phase 3 foundation

Reviewed: 2026-09-11. **Phase 3 workflow is verified end to end.** The model
drives scanning, the Network Observer and the synthetic capture from real Canvas
input; the production local-analysis page lists, opens and now renders the saved
capture's summary; and the lifecycle suite verifies module independence, repeated
capture and reset. Verified by [the Phase 3 gate](phase3-verification.json), which
runs the whole Phase 2 regression first. The firmware `%f` defect that previously
blocked the summary page is fixed; see [Resolved blocker](#resolved-blocker).

## Implemented

`tools/ui_emulator/model/device.mjs` loads the versioned synthetic seed and owns
independent Grove/MBus state, discovery, selection, client relationships,
cancellable jobs and virtual SD files. Reset invalidates previous job IDs.
Disconnected modules, missing clients and absent/full storage fail explicitly.
Cancelled operations cannot subsequently create files. Scheduling and retained
job history are bounded; snapshots and file reads return defensive copies.

`model/pcap.mjs` generates deterministic PCAP v2.4 Ethernet/ARP records using the
selected network and its clients. These are synthetic analysis fixtures, not
captured radio traffic or authentication handshakes. Packet timestamps carry
correctly into seconds. Each client contributes one 58-byte record after a
24-byte file header, and the file is committed only after the simulated
operation completes.

`model/bridge.mjs` is the synchronous boundary the retained C interface calls on
the browser thread. `runtime/application.c` drives scan jobs, rows and selection
through it; `runtime/observer.c` holds the Observer boundaries.

## Evidence

```bat
node --test tests/test_emulator_device.mjs
node --test tests/test_emulator_bridge.mjs
tools\ui_emulator\.venv\Scripts\python.exe tests\run_emulator_capture_tests.py
```

Six device tests and the bridge test pass. The capture command exports a model-generated file and
uses WSL C compilation with AddressSanitizer and UndefinedBehaviorSanitizer to
run the actual `components/pcap_reader/pcap_reader.c`. It verifies the selected
AP, client addresses, packet counts and lengths. The
[capture report](phase3-capture-report.json) records reader and fixture hashes.
The seed gives `net-1` three clients, so the fixture is three ARP frames in 126
captured bytes.

## Integration status

Wired and verified in this session:

- `prepare_browser.py` merges `slice-*-*.json` overlays over the base slice
  policy and now reads every `runtime/*.c` boundary file, so
  `slice-phase3-observer.json` widens the slice to the Observer page, its table,
  the network popup and the attack bar.
- `runtime/observer.c` is included from `application.c` after the binding table,
  and `app_observer_tick()` runs from `app_tick()`. It rewrites the capture
  status only when the model state changes, rather than on every frame.
- `app_bind_adapter()` registers simulator-owned controls under an explicit
  `emu.` prefix plus the module name, so they are never mistaken for a frozen
  firmware template and two modules cannot collide on one ID.
- `app_bind()` learned Observer identities: network rows key on the cached
  BSSID, client rows on the client MAC, and the popup attack bar on its action
  label. Without this, one template repeated per row would be rejected as a
  duplicate binding and the row callbacks would never register.
- `xTaskCreate()` accepts `popup_focus_task` and runs it inline. It previously
  refused, which would have left the popup showing "Failed to start target task".
- Virtual `xTimerCreate`/`xTimerStop`/`vTimerSetTimerID` adapters hold the
  retained popup timer without ever firing it; the poll it used to drive is an
  explicit boundary in this build.
- The shell now surfaces `emulator-model-error`. A refused model operation was
  previously dispatched by the C boundary and silently dropped.

`tests/test_emulator_phase3.py` covers the intended workflow end to end from
Canvas input: scan, select `NEON-BAZAAR`, open the Observer, Start, check that
the rows and client MACs match the model, open the production network popup,
generate a synthetic capture through the labelled affordance, and verify the
file's network, client IDs, packet count and PCAP magic. It also checks that
closing leaves no active operation or extra file, and that MBus stays empty
until that module scans. All five checks passed; see
[the browser report](phase3-browser-report.json).

### Local PCAP analysis, wired 2026-09-11

- `slice-phase3-pcap.json` widens the slice to the ESPShark TAB5 SD page, its
  file list, the loader and the capture/packet pages: 210 retained production
  functions and 66 explicit boundaries. Deep sub-views (map, tools, extraction,
  devices, connections, protocols, health, packet detail) and the Monster-side
  captures page stay explicit unsupported boundaries; retaining them dragged in
  JanOS transfer, USB CDC and board detection, which this slice has no business
  running.
- The build compiles the real `components/pcap_*` sources rather than a
  re-implementation. `pcap_extract` is left out: its entry points are unsupported
  here and it needs mbedtls, which this build does not have.
- Emscripten MEMFS replaces `-sFILESYSTEM=0`, and `app_model_sync_files()`
  mirrors the module's virtual SD into it when a capture completes. The
  production page then finds the file by itself through `opendir`/`stat`.
- The model writes to `/sdcard/lab/pcaps/`, matching the firmware's
  `PCAP_VIEWER_ROOT`, instead of a path only the model knew about.
- `xTaskCreate()` defers `pcap_viewer_load_task` to the next tick instead of
  running it inline. Inline, the retained loader replaced the page that owned
  the click target still being dispatched, and LVGL wedged.
- The whole build, LVGL included, is now `-Os`. Adding five analysis components
  pushed the Wasm module past the 1 MB largest-file budget; optimizing for size
  brought it back to 944,827 bytes without moving the budget.
- `artifact_fingerprint()` walks subdirectories. It previously measured only
  top-level files and silently ignored the served `model/*.mjs`.

Verified from the Canvas: the production page reports
`Found 1 local capture(s) under /sdcard/lab/pcaps`, lists
`synthetic-1.pcap  |  198 B`, and the production `pcap_reader` opens it.

## Resolved blocker

`pcap_viewer_render_capture_page()` used to trap on its first label because
`main/main.c` passed `%f` to LVGL's built-in printf (compiled without float
support), so the conversion was not consumed and every following argument was
read shifted. All seven call sites are fixed (format with `snprintf`, pass `%s`);
full analysis is in [Firmware findings](Firmware_Findings.md). The frozen slice
inputs and control contracts were regenerated against the fixed source and the
emulator rebuilt.

### Emulator-tooling off-by-one exposed by the fix

Adding the `snprintf` lines shifted source line numbers, which surfaced a
pre-existing enrollment bug that had never been exercised because the summary
page always crashed first. The interposing macro in `runtime/host.h` passes
`__LINE__` to `app_bind()`; Clang resolves `__LINE__` to the **closing** line of
a multi-line `lv_obj_add_event_cb(...)` call, but the generator enrolled the
**opening** line. Single-line calls matched; every reachable multi-line
registration on the now-rendering summary page (buttons, packet rows, filter
chips) reported "Unenrolled production event registration". Fixed by recording
`source_end_line` (the call's closing line) in `inventory.py` and
`control_contracts.py` and keying the `app_templates` table on it in
`prepare_browser.py`; single-line calls keep `source_end_line == source_line`, so
identities and template IDs are unchanged. A floor/nearest-line match was rejected
because it would silently capture intentionally-unenrolled boundary
registrations.

The lifecycle suite also required the manual `advance()` window in
`tests/test_emulator_lifecycle.py` to exceed the 4500 ms realistic scan duration
(raised from 4000 ms to 5000 ms); at 4000 ms a realistic-speed scan only reached
`floor(4000/4500 × 12) = 10` of 12 networks.

## Gate

`tests/verify_emulator_phase3.py` records `phase3-verification.json`. It runs the
whole Phase 2 gate first, because the Observer slice widens the same build and
the earlier Phase 2 result does not carry over to this artifact, then the model,
bridge, production-reader and browser-workflow suites. It rejects failed or stale
reports and a build that changes mid-run.

~~~bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase3.py
~~~

## Remaining

- [x] Render and verify the capture summary against the model: packet count,
  link type, endpoints and file size agree. `tests/test_emulator_phase3.py` now
  asserts `3 packets`, `Ethernet`, `198 B` and `0.002 s` on the rendered page.
- [x] Verify repeated start/stop, reset and module switching:
  `tests/test_emulator_lifecycle.py` (4 cases) passes in the gate.
- Late-reply/stale-event coverage beyond the current cancellation checks stays
  Phase 4/6 work.
- The gate now covers scan, Observer, clients, capture, the virtual SD listing,
  the production reader opening the file **and the rendered analysis summary**.

See the [implementation plan](../superpowers/plans/2026-09-10-emulator-phase3-implementation.md).
