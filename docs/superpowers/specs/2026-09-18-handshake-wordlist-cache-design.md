# Handshake wordlist cache and resume design

Date: 2026-09-18

## Purpose

The on-device handshake cracker currently enumerates every selected wordlist
twice: once to count valid candidates for the progress bar and once to perform
the cryptographic checks. Large wordlists also restart from byte zero after a
cancel, reboot, or power loss, and a completed wordlist can be repeated for the
same capture because the existing `crack_state.csv` stores only one aggregate
wordlist status.

This change removes the mandatory counting pass, persists completed attempts,
supports safe resume, and optionally keeps a small wordlist in PSRAM while a
batch is active. It does not claim to make PBKDF2 cheaper for a new network.

## Goals

- Start checking candidates immediately instead of displaying a full-file
  `Counting` pass.
- Never repeat a completed, unchanged handshake/wordlist pair unless the user
  explicitly selects `Force re-run`.
- Resume an interrupted attempt from a safe byte offset, losing at most one
  checkpoint interval of work.
- Invalidate cached state whenever the capture, wordlist, or built-in generic
  candidate set changes.
- Keep cache failures non-fatal: cracking remains correct even when cache files
  cannot be read or written.
- Reuse a small wordlist from PSRAM across files in a future batch.
- Preserve the existing `crack_state.csv` as the manager's per-capture summary.

## Non-goals

- Caching derived PMKs across different SSIDs. WPA PBKDF2 uses the SSID as its
  salt, so such a cache would normally be invalid and would consume roughly
  32 bytes per candidate per SSID.
- Loading arbitrarily large wordlists into memory.
- Making `notfound` from one capture apply automatically to another capture
  with a similar SSID.
- Changing the WPA/WPA2 verifier or supporting SAE/WPA3.

## Performance model

The counting pass performs parsing and SD reads but no PBKDF2 work. Removing it
eliminates duplicated I/O and gives immediate feedback. The first full attempt
against a new capture still has to perform the cryptographic check for every
candidate. With the observed multi-second candidate time, this can take hours;
the cache primarily prevents that work from being lost or repeated.

Progress for an uncached wordlist is based on bytes consumed rather than a
pre-counted number of lines:

```text
percent = safe_byte_offset / wordlist_size
```

The UI continues to display the exact number of candidates actually attempted.
ETA is estimated from elapsed time and byte progress. If byte progress is too
small for a stable estimate, ETA remains `calculating`.

## Storage layout

All files live under `/sdcard/lab/handshakes`:

- `crack_state.csv` — existing one-row-per-capture manager summary.
- `crack_attempts.csv` — completed generic and per-wordlist attempts.
- `.crack_resume` — the single currently active resumable attempt.
- temporary files ending in `.tmp` — used for replace-on-success writes.

`crack_attempts.csv` is separate from `crack_state.csv` because one capture can
have many wordlist attempts. The summary remains small and directly usable by
the manager UI.

All CSV files use real quoted CSV encoding. Commas, quotes, spaces, and other
valid password characters are preserved exactly. A parser or writer must not
sanitize a credential by replacing characters.

### On-disk schemas

`crack_attempts.csv` starts with a schema marker and then contains these
columns:

```text
schema,capture_crc32,capture_size,ssid,bssid,source_kind,source_id,
source_size,source_mtime,source_head_crc32,source_tail_crc32,
generic_version,status,tried,password
```

`source_kind` is `generic` or `wordlist`. A generic entry uses `generic` as its
`source_id` and leaves the wordlist fingerprint columns empty. A wordlist entry
uses its normalized path as `source_id`. Hex CRC fields use eight uppercase
digits so comparisons are deterministic.

`.crack_resume` contains one quoted CSV record with:

```text
schema,capture_crc32,capture_size,source_id,source_size,source_mtime,
source_head_crc32,source_tail_crc32,safe_offset,tried,all_mode,
selected_list_index,updated_sequence
```

The first field is a numeric schema version. Readers reject unknown versions
without rewriting the file. Writers emit fields in this fixed order and use a
temporary file followed by replacement only after close succeeds.

## Identity and invalidation

### Capture identity

A local capture is identified by:

- file size;
- full CRC32 of the PCAP;
- extracted SSID and BSSID when available.

PCAP files are small enough that computing their full CRC while preparing the
records is acceptable. A filename is descriptive metadata, not the identity.

### Wordlist identity

A wordlist is identified by:

- normalized path;
- file size;
- modification time when supplied by FATFS;
- CRC32 of up to the first and last 4096 bytes.

This fingerprint avoids a full-file hash before cracking while detecting
normal replacements and edits. A changed fingerprint invalidates completed and
resume state for that list. The user can always override a matching completed
entry with `Force re-run`.

### Generic identity

The firmware defines `HS_CRACK_GENERIC_VERSION`. Changes to SSID-derived rules
or the built-in dictionary increment this constant and invalidate earlier
generic `notfound` results.

CPU mode is not part of attempt identity because it changes throughput, not the
candidate set or expected result.

## State model

Completed attempt statuses are:

- `found` — a password was verified and is stored;
- `notfound` — the selected candidate source reached a complete EOF without a
  match;
- `error` — the attempt could not complete and must not be treated as exhausted.

`cancelled` is not a completed status. An interrupted run is represented only
by `.crack_resume`.

Resume checkpoints apply to file-backed wordlists. The generic candidate set is
small and deterministic, so an interrupted generic phase safely restarts from
its beginning rather than creating a generic checkpoint.

The resume record contains:

- capture fingerprint;
- method and generic version;
- wordlist fingerprint and path;
- last safe byte offset;
- number of safely completed candidates;
- selected-list index when `ALL` is active;
- update sequence and timestamp when available.

If any identity field no longer matches, the resume record is ignored and the
attempt starts from zero. An invalid record is logged and does not block the
cracker.

## Safe checkpoint semantics

Dual-worker execution can have queued candidates that have been read but not
yet verified. Persisting the raw reader position would skip those candidates
after a restart.

Each periodic checkpoint therefore follows this sequence:

1. Stop enqueueing new candidates.
2. Drain all queued and in-flight jobs.
3. Record the file position after the last fully completed input line.
4. Write `.crack_resume.tmp`, close it successfully, then replace
   `.crack_resume`.
5. Resume enqueueing.

A checkpoint is requested after 64 completed candidates or five minutes,
whichever occurs first. This limits SD writes while bounding typical lost work.
Cancel preserves the last safe checkpoint; it never advances to an unsafe
reader offset. A successful `found` or full `notfound` removes the resume file
after the completed attempt has been persisted.

## Candidate source flow

### First use of an uncached wordlist

1. Create the wordlist fingerprint.
2. Check for a matching completed attempt or resume record.
3. If neither exists, start at byte zero immediately; do not pre-count.
4. Parse, validate, and enqueue candidates in one pass.
5. Update progress from the safe byte offset and total file size.
6. Persist periodic safe checkpoints.
7. On EOF, save `notfound`; on match, save `found` and the password.

### Repeated completed attempt

The default action is to skip the cryptographic run and show `Cached: found` or
`Cached: not found`. The UI offers `Force re-run`, which ignores the completed
entry for this invocation without deleting historical data first.

### Interrupted attempt

The UI offers `Resume` and `Start over`. `Resume` seeks to the stored safe byte
offset after verifying both fingerprints. `Start over` removes the matching
resume state and begins at byte zero.

### `ALL`

Each wordlist has independent completed and resume state. `ALL` skips completed
lists, resumes the matching interrupted list, and then proceeds through the
remaining lists. A match stops the sequence. Wordlists are sorted
case-insensitively by display name so execution order is deterministic.

## PSRAM cache

The runtime maintains at most one cached wordlist blob. A file is eligible when:

- its size is at most 8 MiB;
- enough free PSRAM remains after reserving 6 MiB for the UI, records, and other
  application work;
- allocation succeeds without falling back to internal RAM.

The blob contains the original bytes and is parsed with the same validation as
the streamed path. It is keyed by the full wordlist fingerprint. A fingerprint
change, selection of a different eligible file, memory pressure, or batch end
evicts it. Large files remain streamed from SD.

The PSRAM cache avoids repeated SD reads and parsing within a session or batch;
it does not cache PMKs and does not change cryptographic results.

## UI behavior

The old `Counting` stage is removed. A run transitions through:

```text
Preparing -> Checking generic -> Checking WL -> Saving result -> Complete
```

Example status:

```text
WL: polish.txt
13,312 tried | 42% file | 1.8 cand/s | ETA 7h 12m
```

Additional states:

- `Cached: already exhausted` with `Close` and `Force re-run`;
- `Cached password found` with password reveal;
- `Resume available: 42%` with `Resume` and `Start over`;
- `Checkpoint unavailable` as a non-fatal warning while the run continues.

The wordlist scanner ignores dotfiles and the internal cache/temp filenames.
Normal user `.txt`, `.lst`, and `.dic` files remain selectable.

## Integration boundaries

A new `main/hs_crack_cache.c` and `main/hs_crack_cache.h` own:

- quoted CSV parsing and writing for cache records;
- capture and wordlist fingerprints;
- completed-attempt lookup and persistence;
- resume load, validation, save, and removal;
- deterministic wordlist catalog sorting;
- the optional one-entry PSRAM blob cache.

`main/main.c` remains responsible for:

- LVGL objects and user choices;
- record extraction and cryptographic workers;
- translating worker completion into safe checkpoint offsets;
- progress display;
- `cracked.txt`, `save_pass`, and `crack_state.csv` summary updates.

The cache API does not depend on LVGL. This keeps host tests small and prevents
the manager UI from becoming the only way to exercise persistence logic.

## Interaction with the future batch manager

The batch manager snapshots selected captures and wordlists before starting.
For each capture it consults completed state before launching the engine. Small
wordlists remain in the one-entry PSRAM cache as the batch advances. Transport
locking is used only for file transfer and `save_pass`; a separate active-job
guard prevents overlapping crack/copy operations during CPU-only work.

Captures must not share a `notfound` result solely because their SSIDs match.
A later optimization may group multiple records from the same capture or a
proven identical network into one verifier run, but it is outside this change.

## Failure handling

- Missing or malformed cache files are treated as an empty cache and logged.
- Unknown CSV versions are ignored without overwriting them.
- Failed cache writes produce a UI warning but do not change a cryptographic
  result into failure.
- A completed result is persisted before its resume record is removed.
- A cancelled or errored pass never becomes `notfound`.
- Cache lookup never suppresses work unless every identity field matches.
- Passwords and SSIDs are preserved byte-for-byte within their supported text
  representation.

## Migration

Existing `crack_state.csv` rows remain valid as summary information. Because
they do not identify a specific wordlist version, an existing aggregate
`wordlist=notfound` does not automatically create completed attempt entries.
The first run after the upgrade performs the selected list normally and then
creates precise cache state.

The obsolete `crack_state.tsv` is not imported automatically.

## Verification

Host tests cover:

- quoted CSV fields containing commas, quotes, spaces, and empty passwords;
- wordlist fingerprint stability and invalidation;
- capture fingerprint changes;
- completed-attempt lookup and `Force re-run` behavior;
- resume validation and stale-resume rejection;
- safe checkpoint offsets with queued work;
- cancel/error never becoming `notfound`;
- deterministic `ALL` ordering;
- PSRAM-cache eligibility decisions.

Target verification covers:

- ESP-IDF target build with warnings treated as errors;
- first run starts checking without a counting pass;
- Single and Dual produce identical results;
- cancel, reboot, and resume do not skip the first unchecked candidate;
- completed unchanged attempts are skipped;
- modified wordlists are rerun;
- `ALL` skips and resumes lists independently;
- cache-write failure leaves cracking functional;
- memory remains stable with an 8 MiB PSRAM-cached list and with a larger
  streamed list.
