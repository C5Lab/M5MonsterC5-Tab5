# WPA PSK Auditor — foundation acceptance

Date: 2026-09-21. Firmware compilation is intentionally left to the device
owner. This gate covers host-verifiable capture qualification, durable data
formats, audit history and the cross-repository read-only inventory protocol.

## Implemented foundation

- Tab5 validates complete classic PCAP/radiotap captures before any worker
  preparation and returns structured evidence/reason codes.
- Invalid or unsupported material remains untouched and visible; transport
  errors never classify a capture as invalid.
- A versioned CRC-protected A/B session codec persists bounded, field-wise
  snapshots without raw C padding or pointers.
- Immutable history records contain timing, source, selected lists, worker
  count, throughput, ETA, outcome and resume count, but never a recovered
  password.
- The current JanOS `1.7.5` development tree adds read-only `ARTIFACT/1` while retaining exact
  `CRACK/1 protocol=4` behavior.
- Tab5 has a strict fragmented-stream client and fixed-capacity merged catalog.
  Exact cross-source deduplication requires full `size+CRC32`; provisional
  records are not joined by filename.

Runtime UI/discovery wiring and distributed execution restore are tracked as
separate follow-up work. The current firmware must not present those as
completed features merely because their durable formats are available.

## Host gate

Run from the Tab5 checkout in WSL:

```sh
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I main -I components/pcap_reader/include \
  tests/hs_capture_analyzer_test.c main/hs_capture_analyzer.c \
  components/pcap_reader/pcap_reader.c -o /tmp/hs_capture_analyzer_test
/tmp/hs_capture_analyzer_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_crack_session_test.c main/hs_crack_session.c -o /tmp/hs_crack_session_test
/tmp/hs_crack_session_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_audit_history_test.c main/hs_audit_history.c -o /tmp/hs_audit_history_test
/tmp/hs_audit_history_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_artifact_inventory_test.c main/hs_artifact_inventory.c \
  -o /tmp/hs_artifact_inventory_test
/tmp/hs_artifact_inventory_test
```

Run in `projectZero/ESP32C5`:

```sh
python3 tests/test_artifact_inventory_contract.py
python3 tests/test_crack_worker_diagnostics.py
python3 tests/test_crack_worker_job_replay.py
cc -std=c11 -Wall -Wextra -Werror -I main \
  main/crack_worker_core.c main/crack_worker_transfer.c \
  tests/crack_worker_core_test.c -o /tmp/janos_crack_worker_core_test
/tmp/janos_crack_worker_core_test
```

Also run the existing Tab5 worker recovery, reassignment, stage retry, USB
ACK32/baud/probe/start/cancel and handshake verifier contracts. Finish with
`cmake -P tests/check_hs_crack_cmake.cmake` and `git diff --check` in both repos.

## Hardware acceptance after owner builds/flashes

1. **Version/protocol:** all Monsters report JanOS `1.7.5`; `crack_worker
   capabilities` still reports `protocol=4`; `artifact_inventory capabilities`
   reports `artifact_inventory=1` and one terminal `END`.
2. **Read-only listing:** list both scopes on Grove, USB and M-BUS. Confirm
   pagination beyond 32 entries, raw filename hex decoding and no files created,
   removed or renamed on worker SD cards.
3. **Inspection:** exercise valid HCCAPX, invalid HCCAPX, raw PCAP and a file
   changed during inspection. Expect respectively `valid`, `invalid`,
   `unknown/unsupported_validator` and `unknown/changed`.
4. **Cancel/busy:** cancel a large inspection during USB and hardware-UART
   operation. The accepted request must emit exactly one cancelled terminal;
   another request during it must receive `snapshot=0 reason=busy`.
5. **Limits:** a 16-record HCCAPX may validate; a 17-record HCCAPX must be
   flagged `invalid/limit_reached`. A PCAP may be fingerprinted up to 16 MiB.
6. **Old firmware fallback:** connect a worker without `ARTIFACT/1`; Tab5 must
   use `list_dir -s`, mark identity provisional and never label a transport
   timeout as invalid capture.
7. **Power-cut durability:** interrupt alternating session-slot and history
   writes at several offsets. At least the previous valid A/B snapshot must
   load; corrupt history candidates stay untouched and are skipped.
8. **Sync validation (after runtime wiring):** interrupt Sync to Tab5, resume
   its `.part`, confirm prefix CRC, atomically publish the local file, then run
   the Tab5 analyzer. Invalid material remains in the local catalog.

Record serial excerpts for every terminal `ARTIFACT/1` response and retain SD
directory listings from before and after the test as the non-destructive proof.
