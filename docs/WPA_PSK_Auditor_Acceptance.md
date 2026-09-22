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
  Exact cross-source identity uses `size+CRC32`. Batch synchronization may
  coalesce provisional records only when name, size and format agree; one
  canonical copy is retained while every worker path is indexed as an alias.
  Conflicting known CRC values are never coalesced.
- The INTERNAL tab exposes `WPA PSK Auditor`. Its dashboard reads up to eight
  resumable sessions from independent A/B checkpoint pairs plus the immutable
  history store, reports source availability, and routes each row's Resume or
  confirmed Start over through the existing crack flow.
  Resume restores the saved wordlist and synchronization toggles before Start.
  A missing or changed wordlist is never silently replaced: Start stays locked
  until the operator selects a replacement, which begins a new audit.
  Starting a different audit does not retire older resumable sessions; terminal
  completion, Start over and the confirmed `Delete audit` action tombstone only
  the selected session. Deletion also makes a best-effort cancellation of that
  session's matching live worker jobs, while preserving captures and wordlist
  caches on every Monster.

Merged multi-source discovery, per-source filtering, `Sync to Tab5`, and the
sequential `Sync all to Tab5` batch are wired into the dashboard. Hardware
acceptance must confirm that a duplicate present on multiple workers produces
one local file and does not create a `_1` copy. `Sync all` remains reusable:
each press refreshes all sources before rebuilding the missing-file queue. Its
availability follows connected remote workers rather than the previous catalog
snapshot; selecting `LOCAL` still keeps the global refresh-and-sync action.

Every newly synchronized PCAP/HCCAPX is validated locally. A hidden,
CRC-protected sidecar binds the report to file size, full CRC32, format and
validator version. LOCAL catalog refresh reuses only an exact match and
revalidates stale or corrupt cache entries. Invalid files remain visible as
`Needs investigation`, but cannot be launched for audit.

## Host gate

Run from the Tab5 checkout in WSL:

```sh
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I main -I components/pcap_reader/include \
  tests/hs_capture_analyzer_test.c main/hs_capture_analyzer.c \
  components/pcap_reader/pcap_reader.c -o /tmp/hs_capture_analyzer_test
/tmp/hs_capture_analyzer_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_capture_validation_test.c main/hs_capture_validation.c \
  -o /tmp/hs_capture_validation_test
/tmp/hs_capture_validation_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_crack_session_test.c main/hs_crack_session.c -o /tmp/hs_crack_session_test
/tmp/hs_crack_session_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_session_catalog_test.c main/hs_session_catalog.c \
  main/hs_crack_session.c -o /tmp/hs_session_catalog_test
/tmp/hs_session_catalog_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_audit_history_test.c main/hs_audit_history.c -o /tmp/hs_audit_history_test
/tmp/hs_audit_history_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_artifact_inventory_test.c main/hs_artifact_inventory.c \
  -o /tmp/hs_artifact_inventory_test
/tmp/hs_artifact_inventory_test
gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
  tests/hs_audit_queue_test.c main/hs_audit_queue.c \
  -o /tmp/hs_audit_queue_test
/tmp/hs_audit_queue_test
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

The dashboard source contract is:

```sh
python3 tests/test_wpa_psk_auditor_contract.py
```

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
   flagged `unknown/limit_reached` for inspection. A PCAP may be fingerprinted
   up to 16 MiB.
6. **Old firmware fallback:** connect a worker without `ARTIFACT/1`; Tab5 must
   use `list_dir -s`, mark identity provisional and never label a transport
   timeout as invalid capture.
7. **Power-cut durability:** interrupt alternating session-slot and history
   writes at several offsets. At least the previous valid A/B snapshot must
   load; corrupt history candidates stay untouched and are skipped.
8. **Sync validation:** interrupt Sync to Tab5, resume
   its `.part`, confirm prefix CRC, atomically publish the local file, then run
   the Tab5 analyzer. Invalid material remains in the local catalog as
   `Needs investigation` and its Audit action is disabled. Reopen the catalog
   to confirm an unchanged file uses its sidecar; mutate the file and confirm
   its size/CRC mismatch forces fresh validation.
9. **Local deletion:** choose `Delete from Tab5` for a local capture, cancel
   once, then confirm. The capture, its hidden `.audit` sidecar and sync-index
   aliases must disappear; copies on Grove, USB and M-BUS must remain. After
   the automatic rescan, `Sync all to Tab5` must offer the missing capture.
10. **Repeatable batch sync:** complete `Sync all to Tab5`, add a new remote
   capture without reopening the Auditor and press `Sync all` again. The action
   must remain enabled, refresh every source, copy only the new capture and
   report the already-local items without creating duplicate files. Repeat once
   with the source dropdown on `LOCAL`; the global action must remain available.
11. **Dashboard:** open INTERNAL -> WPA PSK Auditor. With no checkpoint it must
   show an explicit empty state. Cancel a long distributed crack, reopen the
   dashboard and confirm capture, wordlist, safe progress and worker count.
   Resume must open the normal Crack dialog with the saved wordlist and sync
   toggles already selected, then continue the saved suffixes. Rename or modify
   that wordlist and confirm Resume reports the stale input and disables Start;
   choosing a replacement must explicitly switch to a new audit.
   For an `ALL` run, add a new dictionary that sorts before the active one and
   confirm Resume remaps the saved fingerprint instead of discarding progress.
   Start over must first show a destructive confirmation. Back must return to
   the INTERNAL tiles without rebooting.
12. **Multiple resumable audits:** start capture A with wordlist A, wait for
   non-zero safe offsets and Cancel. Start capture B with wordlist B and Cancel
   it as well. The dashboard must show both rows. Resume A and verify its own
   wordlist and saved shard suffixes, Cancel again, then Resume B and verify its
   independent offsets. Starting B, resuming A, or completing either audit must
   not remove the other checkpoint. A legacy `.crack_audit/active.a|active.b`
   checkpoint must appear once; after its next saved checkpoint the per-session
   copy is preferred and the legacy copy must not create a duplicate row.
13. **Batch creation:** select three valid LOCAL captures using the catalog
   checkboxes, choose one candidate source and press `Add to queue`. Confirm the
   collapsed Batch queue reports three items. Expand it and verify all rows use
   the same locked wordlist. Invalid captures must not be selectable.
14. **Remote-only batch:** select a valid capture available only on Grove, USB
   or M-BUS. Adding it must first synchronize only the selected missing capture
   to Tab5, validate the completed local file, refresh the catalog, and then add
   exactly one queue item. A failed sync must not enqueue a nonexistent path.
15. **Sequential execution:** start a three-item batch and confirm only one
   normal Crack coordinator is active. FOUND, NOT FOUND and ERROR each persist
   their item state before the next item starts. The aggregate card must update
   completed count, percent, elapsed time, ETA and active remote worker count.
16. **Pause/reboot/resume:** pause during an active item and wait for its normal
   cancellation/checkpoint boundary. Reboot Tab5. The A/B batch journal must
   restore the interrupted item as Paused, preserve its `session_id`, wordlist
   fingerprint and remaining queue, and `Resume batch` must continue that item
   before later queued captures.
17. **Cancel/remove isolation:** `Cancel current` must mark only the current
   item Cancelled and continue with the next queued capture. Removing a queued,
   paused or terminal row must change only the batch journal; capture files,
   validation sidecars, resumable sessions and Monster caches must remain.

Record serial excerpts for every terminal `ARTIFACT/1` response and retain SD
directory listings from before and after the test as the non-destructive proof.
