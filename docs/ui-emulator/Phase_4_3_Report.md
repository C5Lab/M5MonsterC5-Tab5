# Phase 4.3 — implementation handoff

Status: implemented in the current working tree; full build and browser acceptance pending.
The last verified static preview belongs to Phase 4.2. No 4.3 passing build report is claimed.

## Changes

- Retained original Tab5 PCAP map, devices, remote endpoints, connections, protocols,
  health/investigation, FQDN, packet-detail, tools and extracted-object views.
- Added explicit **Load PCAP examples** preview button: deterministic HTTP/DNS/ICMP
  capture (14 packets, 1225 bytes), DNS/ICMP capture and truncated-file fixture.
  Existing captures are preserved. Fixtures use documentation IP addresses.
- Enabled native PCAP extraction and exports with a portable public-domain SHA-256
  core. Artifact writes and object deletion update virtual-SD accounting; failed
  commits restore file bytes. Original captures are preserved by extraction.
- Retained native file-management screens and confirmations, virtual copy/sync/delete,
  Wardrive cleanup preview/archive/delete. Fix displays an explicit unsupported-format
  message for simulator JSON sessions; native CSV repair is not simulated.
- Generator now follows anonymous-enum constants used by retained functions.

## Checks already run

- Generator: 444 retained production functions, 87 explicit boundaries.
- Eight PCAP-fixture/file-model tests passed, including checksum validation, module
  isolation, storage exhaustion and atomic rollback.
- SHA-256 compatibility: eight hashlib comparison vectors, streaming and padding
  boundaries, ASan/UBSan passed.
- Emscripten syntax-only check passed for generated browser C and native extraction.
  Extraction emits the existing host-shim internal-linkage warning. This is not a link test.
- Browser tests and the full Wasm build have not been run for this handoff.

## Run locally from the repository root

With Docker Desktop running:

```bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase4_pcap_deep.py
```

The gate prints RUN/PASS/FAIL and captures detailed logs in
`docs/ui-emulator/phase4-pcap-deep-verification.json`. It includes the previous
4.2 gate, model checks, SHA-256 and new browser tests. Nested prior gates may be
quiet for a while. Requirements: Node, Playwright Chromium, and WSL with `cc`
on Windows (also used by previous native-reader checks).

Preview in another terminal:

```bat
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
```

Open http://127.0.0.1:8765 and refresh with Ctrl+F5 after rebuilding.
Click **Load PCAP examples**, open `example-http-dns-icmp.pcap`, and explore
Map, Devices, Connections, Protocols, Health, FQDN, packet details and Tools.
Objects should extract the text `Offline synthetic example.` from HTTP.
The truncated example must show an error.

Manual acceptance still required: native PCAP file copy/delete confirmation,
Wardrive file selection and Cleanup preview/archive/delete, plus the visible Fix
format message. These dialogs are not covered by the new deep-view browser tests.
No real radio, remote upload or physical SD is used. Cross-browser/hardware checks
remain later phases. Cache/profile auxiliary-file persistence needs a separate audit;
the atomic model transaction currently wraps artifact workers and object deletion.
