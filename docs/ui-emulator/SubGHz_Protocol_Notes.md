# SubGHz protocol intake and emulator implications

Received: 2026-09-10.
Source: user-supplied **JanOS SubGHz UART Command Reference** in the project
conversation. This document is a working summary, not a verbatim copy or a
firmware-verified contract. The supplied reference did not identify a commit.

Status: reference received; implementation remains deferred under
[Accepted Decisions](Accepted_Decisions.md). Receiving protocol information does
not enable SubGHz in the current seed or close its deferred control contracts.
The next active Phase 2 work remains Settings and restart.

## Transport and parsing

- Commands are text terminated by CRLF. Signal indices are 1-based.
- Tagged events can be embedded in other firmware logs; locate the tag before
  parsing. Lowercase `[subghz]` lines and legacy save messages are diagnostic.
- Do not split all fields on spaces: values such as `type=Nice FloR-S` and
  `mf=Nice One` contain spaces. Parse by known field boundaries, tolerate
  optional fields, and validate required fields per event variant.
- Distinguish receive, duplicate, raw, drop, listing, detail and mutation events.
  Do not append a second signal because a diagnostic event describes the same
  capture. A duplicate references an existing memory capture index.
- Terminate list/info/save operations on their explicit END markers, including
  empty and failed responses. Error tags must not be treated as successful data.
- Listing rows and successful detail rows do not always carry a source. Keep
  the outstanding request's source context; serialize or otherwise disambiguate
  requests because the reference does not specify request IDs.

## Storage model to validate against the producer

The explicit storage section describes two independent stores:

| Store | Identity and lifetime | Mutations |
| --- | --- | --- |
| `mem` | PSRAM cache; `idx` is a monotonic capture sequence, not a row offset; cleared on reboot or clear | RX and analyzer captures; clear |
| `sd` | Persistent `.sub` files; `idx` is positional in the current sorted enumeration | Explicit save from memory, rename and delete |

Use `(source, index)` in request context; do not treat an SD positional index as
a permanent entity identifier. Re-list SD after save, rename or delete and
invalidate selections based on the old enumeration. Names are display labels
and mutable filenames, not immutable identities.

Save copies a memory entry without removing it; collisions do not overwrite the
existing SD file. Clear affects memory only; delete and rename affect SD only.
Rename accepts 1–63 characters from `[A-Za-z0-9_.-]` and updates both filename
and the file's `Name:` field. Model failed mutations without committing changes.
The reference additionally describes a TX synchronization side effect for some
rolling-code memory entries with an existing SD copy; this requires producer
verification before simulating persistent state changes.

## Command families and future simulation coverage

| Family | Commands | Required UI/model behavior |
| --- | --- | --- |
| Status/tuning | `subghz_status`, `subghz_freq`, `subghz_set_freq_correction`, `subghz_get_freq_correction` | Mode/frequency/RSSI; persisted signed correction; validation failures |
| Receive | `subghz_rx` | Timed RSSI, decoded/raw captures, duplicates and drop events |
| Analyzer | `subghz_freq_analyzer` | Start, scan-only versus hunt/raw, quiet/verbose streams, capture lifecycle |
| Traffic | `subghz_scanner` | Start, clustered hits and pass totals; totals differ from deduplicated emitted rows |
| Weather | `subghz_weather` | Start and sensor updates keyed by protocol/ID/channel; missing values and dedup |
| Library | `subghz_list`, `subghz_info`, `subghz_save`, `subghz_rename`, `subghz_delete`, `subghz_clear` | Separate stores, explicit completion/error handling and refreshed SD indices |
| Operation result | `subghz_tx`, `subghz_jam` | Future synthetic state/result flows; not implemented by this intake |
| Diagnostics | `subghz_protocols`, `subghz_keys`, `subghz_selftest`, `subghz_debug` | Protocol/key terminators, opaque logs and debug toggle confirmation |
| Lifecycle/metadata | `subghz_stop`, `board_name` | Stop active job and enter idle; optional provisioning label |

Future emulator fixtures must be synthetic. This intake does not execute UART,
radio operations, hardware provisioning or transmissions.

## Event and scenario details worth retaining

- Frequency correction is specified as signed centi-MHz, range −5.00 to +5.00,
  persisted under NVS `subghz/freq_corr_cc`. Reject NaN/Inf/out-of-range without
  altering saved state. Reported requested frequency and corrected hardware
  tuning must not be conflated.
- RX defaults to an RSSI gate of −80 dBm; the documented gate range is −120 to
  −40. Decode mode and explicit raw mode need separate fixtures.
- Analyzer defaults to −85 dBm and a 2000 ms capture timeout; the documented
  capture range is 100–60000 ms. Start events expose selected options.
- Quiet hunt omits hit/capture/timeout/silent lines. Absence of those lines does
  not mean failure or stopped operation. A UI needing their waterfall/spinner
  transitions must explicitly use the verbose contract or a different state
  source. Do not require verbose events for quiet-mode completion.
- Scanner reports clustered, deduplicated hits. Pass totals count clusters seen
  during that pass, not necessarily the number of emitted HIT lines. Do not
  hardcode the illustrative 62-frequency list length.
- Weather uses optional `-` values for temperature, humidity and channel.
  Negative temperatures are valid. The reference specifies roughly 30-second
  dedup for unchanged `(protocol, id, channel)` readings.
- Stop precedes a new continuous operation. It emits a human-readable stop
  message rather than a dedicated tagged END event. Future adapter jobs need
  cancellation generations so late events cannot revive a stopped screen.
- Boot `[BOARD]` and explicit `board_name=` can populate metadata; other boot
  diagnostics must not imply capture, completion or radio mode transitions.

## Conflicts in the supplied reference — unresolved

1. **Capture persistence:** the storage model says RX/hunt writes only to PSRAM
   memory, while the RX description says auto-save to NVS and an analyzer error
   description mentions NVS save failure. Working interpretation: follow the
   explicit memory/SD model; confirm against the matching firmware revision.
2. **Unknown decode bursts:** detailed RX/drop rules say decode mode drops
   unknown bursts and RAW is explicit-only. The introductory RX description,
   quick matrix and annotated decode-hunt example still describe RAW fallback.
   Working interpretation: explicit RAW only; treat the fallback example as
   potentially obsolete until verified.
3. **Rename target:** the dedicated rename command changes SD only, but another
   paragraph says a captured memory name is mutable via that command. Do not
   implement memory rename from this conflicting statement.
4. **Optional fields:** some examples omit `name=`, `debug=` or other fields
   described as always emitted elsewhere. Establish the minimum supported
   producer version and test both documented variants before freezing parsers.

## Before enabling SubGHz in the milestone

- Compare this reference with the corresponding producer and existing Tab5
  handlers; resolve the conflicts above with versioned evidence.
- Revisit the scope decision explicitly, then refresh the deferred controls,
  adapter decisions and scenario schema together. Do not silently change the
  already accepted Phase 1 gate.
- Start with synthetic status, memory/SD lists, details and lifecycle fixtures;
  cover empty, timeout, malformed, missing-SD and mutation-failure paths.
- Add parser tests for chunking, interleaved diagnostic lines, multiword fields,
  optional values, source context, duplicates and changing SD enumeration.
