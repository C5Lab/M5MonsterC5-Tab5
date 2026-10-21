# WPA PSK Auditor: batch cancellation and CSV closure plan

**Goal:** Make batch interruption semantics explicit and durable, and close the
remaining `crack_state.csv` robustness gap without changing JanOS protocol or
firmware version.

**Workspace constraints:** Work in the existing `development` tree, preserve
unrelated dirty files and generated firmware binaries, do not build firmware,
and leave commits to the repository owner.

## Task 1: Queue-core cancellation semantics

- Add a host-testable queue operation that marks every non-terminal item as
  cancelled, sets the batch to `CANCELLED`, and clears `current_index`.
- Preserve terminal results and every item's checkpoint/session metadata.
- Cover empty, mixed-state, running, paused, and already-terminal queues.
- Run the queue host test and prove the new tests fail before implementation
  and pass afterwards.

## Task 2: Auditor controller and UI

- Keep `Pause batch` as a resumable cooperative stop.
- Rename the old current-item cancellation action to `Skip current`; after the
  current coordinator stops, the next runnable item starts automatically.
- Add confirmed `Cancel batch`; it cooperatively stops the active coordinator,
  cancels all remaining non-terminal items, persists the journal, and never
  starts another item.
- Keep captures, validation sidecars, crack sessions, and worker caches.
- Make action state and wording unambiguous in both landscape and rotated UI.
- Add source-contract tests for the state flags, no-auto-advance guarantee,
  confirmation dialog, labels, and persistence calls.

## Task 3: Quoted `crack_state.csv`

- Extract the aggregate journal merge/write logic into a small host-testable
  module.
- Parse and emit RFC-4180-style quoted fields, including doubled quotes and
  embedded commas/CR/LF.
- Continue accepting legacy unquoted rows.
- Use atomic temporary-file replacement and preserve unspecified columns on
  update.
- Add round-trip, legacy compatibility, merge, and multiline-field tests.

## Task 4: Documentation

- Update the batch design, acceptance checklist, and project roadmap with the
  final `Pause / Skip / Cancel batch` semantics.
- Document data-preservation guarantees and the quoted CSV migration.
- Record which cases are host-tested and which still require one device smoke
  test after the owner builds and flashes firmware.

## Task 5: Verification

- Run all relevant C host tests (through WSL when native GCC is unavailable).
- Run WPA Auditor Python contract tests and stack/CMake contract checks.
- Inspect the final diff for unrelated changes and report exact test results
  plus the small hardware smoke-test matrix.

## Execution result

Completed on 2026-09-23 without compiling firmware or changing JanOS. The
final tree passes nine sanitizer-enabled C host suites, 66 WPA Auditor source
contracts, the crack-stack contract and the CMake registration check. Device
acceptance is limited to the documented Pause/Skip/Cancel smoke tests after the
owner builds and flashes Tab5.
