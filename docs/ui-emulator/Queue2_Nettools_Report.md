# Queue 2 — ARP, MITM, Nmap and GITM

Scope: local offline emulator only. No radio, network scan, packet interception or external service is used.

## Implemented paths

| Story | Entry and native sequence | Required result |
|---|---|---|
| S13 ARP | GROVE Scan → AFTERLIFE-GUEST → ARP → Connect → List Hosts → 192.0.2.10 → packets → STOP → Back | Two synthetic hosts, correct selected host, increasing packet count, completed job and return to Scan |
| S13 MITM | GROVE Scan → AFTERLIFE-GUEST → MITM → Connect & Start → packets → STOP CAPTURE → Cancel | Current session traffic, finalized virtual SD PCAP and return to Scan |
| S13 Nmap | GROVE Scan → AFTERLIFE-GUEST → Nmap → Connect → List Hosts → quick and medium single-host scans → heavy all-host scan → STOP each → Back | Correct level/target, completed native result with port 80/tcp, return to Scan |
| S14 Scan | GROVE Scan selection → GITM → SCAN → upstream AFTERLIFE-GUEST → CONNECT → AP name → START | Current capture has packets, clients and bytes; Keep capturing preserves session; Stop and exit saves file and returns home |
| S14 Global | GROVE Global WiFi → GITM → independent SCAN/configuration | Same final evidence, with independent Global entry |

Nmap's secured-network connection section now uses a vertical layout, with wrapped password rows and bounded status text so Connect and List Hosts are reachable in portrait. Firmware `main/main.c` is unchanged by this work. GITM clears its transient Scan preselection on exit, so cancelling setup cannot contaminate a later Global entry.

`queue2_nettools_story.c` exports read-only evidence. Retained job IDs distinguish fresh operations from prior results. The guide checks module/tab/visibility, current target, actual completed jobs and the exact finalized job file. MITM requires the selected guest network ID in its capture job; GITM requires both AFTERLIFE-GUEST upstream and the native AP SSID SIMULATED-GATEWAY. GITM emits a fixed 14-frame fixture; its guide checks that fixture's packets, client records and byte count. It does not claim a growing GITM packet counter.

## Verification

- Node: `node --test tests/test_emulator_queue2_nettools.mjs` — **10 PASS**. Tests cover successful paths, old epochs and job IDs, restart, hidden/wrong module, wrong scan target, empty hosts, missing output file and interrupted observation.
- Python acceptance: `tests/test_emulator_queue2_nettools.py` uses the real story menu and native pointer controls. Includes 12 S13 and 8 S14 variant/rotation combinations, four secured-network Nmap layouts, native cancel/disconnect, busy retry, missing SD/retry, COPY into ESPShark and sequential Scan-to-Global entry after cancelled setup (30 concrete cases in seven test methods). **7 PASS**, 176.450 s on 2026-09-13. All 30 cases passed; no browser page errors.
- Existing `test_emulator_nettools_gitm.py`: **3 PASS**, 3.203 s on the focused acceptance artifact; covers finalized file copy into ESPShark, disconnect and reset. Its Wasm loading now uses the same deterministic local fixture route as the new harness.

Focused S13/S14 acceptance passes. The root shared regression gate remains the integration check before marking the queue complete.

Tested Wasm SHA256: `0f964f6ecc1602320a0c1fd6ee3a159c12f3d40e83ac77831b3293aa14433787`. A later shared binding rebuild must be covered by the aggregate gate.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
