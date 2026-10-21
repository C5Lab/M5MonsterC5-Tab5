# Handshake Wordlist Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the double-pass wordlist flow with immediate one-pass cracking, persistent completed-attempt caching, safe resume, and an optional one-entry PSRAM wordlist cache.

**Architecture:** A new host-testable `hs_crack_cache` module owns CSV persistence, fingerprints, attempt lookup, resume records, catalog ordering, and PSRAM-cache policy. `main.c` keeps LVGL and worker orchestration, but consumes the module through explicit structs and functions. The current single-file flow is migrated first; the future batch manager will reuse the same interfaces.

**Tech Stack:** C17, ESP-IDF 5.4.1, FreeRTOS, LVGL, FATFS/VFS, PSRAM heap capabilities, host GCC tests.

**Spec:** `docs/superpowers/specs/2026-09-18-handshake-wordlist-cache-design.md`

## Global Constraints

- Do not commit unless the user explicitly asks.
- Do not flash hardware; the user performs device flashing.
- The first wordlist pass must perform cryptographic checks immediately; no preliminary full-file count is allowed.
- A cache mismatch must cause work to run again, never cause candidates to be skipped.
- Cancelled or errored attempts must never be persisted as `notfound`.
- CSV encoding must preserve commas, quotes, spaces, and empty fields exactly.
- Internal RAM must not hold complete wordlists; blobs are PSRAM-only and limited to 8 MiB with a 6 MiB reserve.
- Target compilation uses warnings as errors.

## File structure

- Create `main/hs_crack_cache.h` — public data types and cache API, with no LVGL dependency.
- Create `main/hs_crack_cache.c` — CSV codec, fingerprints, completed attempts, resume state, catalog sorting, and PSRAM blob policy.
- Create `tests/hs_crack_cache_test.c` — host tests over real files and the real cache implementation.
- Modify `main/main.c` — one-pass enumerator, progress, workers, UI states, and module integration.
- Modify `main/CMakeLists.txt` — register `hs_crack_cache.c`.
- Modify `tests/README.md` — document the new host test command.
- Modify `tests/test_handshake_cracker.py` — stop extracting removed legacy wordlist code and point verifier tests at current helpers.

---

### Task 1: Cache module, quoted CSV, and deterministic identities

**Files:**
- Create: `main/hs_crack_cache.h`
- Create: `main/hs_crack_cache.c`
- Create: `tests/hs_crack_cache_test.c`
- Modify: `main/CMakeLists.txt`
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: standard C `FILE`, `stat`, and byte buffers; ESP-only allocation is guarded by `#ifdef ESP_PLATFORM`.
- Produces:

```c
#define HS_CRACK_CACHE_PATH_MAX 160
#define HS_CRACK_CACHE_PASSWORD_MAX 64
#define HS_CRACK_GENERIC_VERSION 1U

typedef enum {
    HS_CRACK_CACHE_OK = 0,
    HS_CRACK_CACHE_NOT_FOUND,
    HS_CRACK_CACHE_STALE,
    HS_CRACK_CACHE_INVALID,
    HS_CRACK_CACHE_IO_ERROR,
} hs_crack_cache_result_t;

typedef enum {
    HS_CRACK_ATTEMPT_FOUND = 0,
    HS_CRACK_ATTEMPT_NOTFOUND,
    HS_CRACK_ATTEMPT_ERROR,
} hs_crack_attempt_status_t;

typedef struct {
    uint64_t size;
    uint32_t crc32;
    char ssid[33];
    char bssid[18];
} hs_crack_capture_id_t;

typedef struct {
    char path[HS_CRACK_CACHE_PATH_MAX];
    uint64_t size;
    int64_t mtime;
    uint32_t head_crc32;
    uint32_t tail_crc32;
} hs_crack_wordlist_id_t;

typedef struct {
    hs_crack_capture_id_t capture;
    bool generic;
    uint32_t generic_version;
    hs_crack_wordlist_id_t wordlist;
    hs_crack_attempt_status_t status;
    uint64_t tried;
    char password[HS_CRACK_CACHE_PASSWORD_MAX];
} hs_crack_attempt_t;

typedef struct {
    hs_crack_capture_id_t capture;
    hs_crack_wordlist_id_t wordlist;
    uint64_t safe_offset;
    uint64_t tried;
    bool all_mode;
    uint16_t selected_list_index;
    uint32_t updated_sequence;
} hs_crack_resume_t;

uint32_t hs_crack_cache_crc32_update(uint32_t crc, const void *data, size_t size);
hs_crack_cache_result_t hs_crack_cache_capture_id(
    const char *pcap_path, const char *ssid, const char *bssid,
    hs_crack_capture_id_t *out);
hs_crack_cache_result_t hs_crack_cache_wordlist_id(
    const char *path, hs_crack_wordlist_id_t *out);
bool hs_crack_cache_capture_equal(const hs_crack_capture_id_t *a,
                                  const hs_crack_capture_id_t *b);
bool hs_crack_cache_wordlist_equal(const hs_crack_wordlist_id_t *a,
                                   const hs_crack_wordlist_id_t *b);
hs_crack_cache_result_t hs_crack_cache_find_attempt(
    const char *csv_path, const hs_crack_capture_id_t *capture,
    bool generic, uint32_t generic_version,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_attempt_t *out);
hs_crack_cache_result_t hs_crack_cache_upsert_attempt(
    const char *csv_path, const hs_crack_attempt_t *attempt);
hs_crack_cache_result_t hs_crack_cache_load_resume(
    const char *path, const hs_crack_capture_id_t *capture,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_resume_t *out);
hs_crack_cache_result_t hs_crack_cache_save_resume(
    const char *path, const hs_crack_resume_t *resume);
hs_crack_cache_result_t hs_crack_cache_remove_resume(const char *path);
```

`wordlist` is `NULL` only when `generic` is true; implementations reject every
other null identity as `HS_CRACK_CACHE_INVALID`.

- [ ] **Step 1: Write failing tests for quoted CSV and identity stability**

Create fixtures containing `Cafe,Guest`, `say"hello`, an empty password, and a
63-character password. Assert round-trip equality through
`hs_crack_cache_upsert_attempt()` and `hs_crack_cache_find_attempt()`. Create
wordlists with identical bodies under different paths, then mutate the first
byte, last byte, size, and mtime and assert the expected identity changes.

- [ ] **Step 2: Run the host test and verify RED**

Run:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I main -o /tmp/hs_crack_cache_test \
    tests/hs_crack_cache_test.c main/hs_crack_cache.c
/tmp/hs_crack_cache_test
```

Expected: compile failure because `hs_crack_cache.h` and its functions do not yet exist.

- [ ] **Step 3: Implement the CSV codec and fingerprints**

Implement RFC-4180-style field quoting: quote a field containing comma, quote,
CR, or LF and double embedded quotes. The reader must parse empty and quoted
fields without `strtok`. Compute a full PCAP CRC32 and wordlist first/last 4096
byte CRC32. Use file size and `st_mtime` from `stat`.

- [ ] **Step 4: Implement versioned attempt and resume persistence**

Emit schema version `1`. Preserve unknown-schema files by returning
`HS_CRACK_CACHE_INVALID` without opening a temporary output. For upsert, copy
unmatched rows, replace the matching identity once, check `ferror`, check
`fclose`, and only then replace the destination. A failed write must leave the
previous destination readable.

- [ ] **Step 5: Run host tests and target object compilation**

Run the host command from Step 2. Then run from an ESP-IDF shell:

```powershell
idf.py build
```

Expected: host tests pass and the target build produces no warnings.

---

### Task 2: Deterministic catalog and one-pass wordlist progress

**Files:**
- Modify: `main/hs_crack_cache.h`
- Modify: `main/hs_crack_cache.c`
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `main/main.c` around `hs_crack_scan_wordlists`, `hs_crack_progress`, `hs_crack_step`, and `hs_crack_task`
- Modify: `tests/test_handshake_cracker.py`

**Interfaces:**
- Consumes: Task 1 wordlist identities.
- Produces:

```c
typedef struct {
    char path[HS_CRACK_CACHE_PATH_MAX];
    char name[48];
    hs_crack_wordlist_id_t id;
} hs_crack_wordlist_entry_t;

void hs_crack_cache_sort_wordlists(hs_crack_wordlist_entry_t *items, size_t count);
int hs_crack_cache_byte_percent(uint64_t safe_offset, uint64_t file_size);
uint64_t hs_crack_cache_eta_seconds(uint64_t safe_offset, uint64_t file_size,
                                    uint64_t completed, int64_t elapsed_us);
```

- [ ] **Step 1: Write failing catalog and progress tests**

Assert case-insensitive order `a.txt`, `B.txt`, `z.lst`; hidden/internal names
are excluded by the scanner integration. Assert byte progress clamps to
`0..100`, handles zero length, and ETA is unavailable until both elapsed time
and byte progress are nonzero.

- [ ] **Step 2: Run tests and verify RED**

Run the Task 1 host command. Expected: unresolved catalog/progress functions.

- [ ] **Step 3: Implement deterministic sorting and byte progress**

Use a stable case-insensitive comparator with the original case-sensitive name
as the tie breaker. Keep exact tried-candidate counts for display, but derive
percentage and ETA from `safe_offset / file_size`. Update the scanner to admit
only regular `.txt`, `.lst`, and `.dic` files, while ignoring dotfiles and the
cache/temp filenames owned by this feature.

- [ ] **Step 4: Remove the count-only pass from `hs_crack_task`**

Delete the `counting_candidates` state transition and the second
`goto run_candidates`. Start workers before enumerating candidates. Generic is
enumerated once, and each selected wordlist is opened once. Capture `ftell()`
after every complete input line. Do not call PBKDF2 from a preliminary pass.

- [ ] **Step 5: Update the current verifier test harness**

Replace the stale extraction that searches for
`FILE *wl = fopen(HS_CRACK_WORDLIST` and `hs_crack_load_records`. Exercise
`hs_crack_read_word()` directly with real CRLF, final unterminated line,
oversized line, binary line, 7/8/63/64-character boundaries, and spaces.

- [ ] **Step 6: Verify host tests and target build**

Expected device behavior after flashing: the first post-download stage is
`Checking: SSID variants` or `Checking: WL: <name>`; `Counting:` never appears.

---

### Task 3: Completed-attempt lookup and summary CSV correctness

**Files:**
- Modify: `main/hs_crack_cache.h`
- Modify: `main/hs_crack_cache.c`
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `main/main.c` around `hs_crack_state_upsert`, `hs_crack_journal_result`, and `hs_crack_task`

**Interfaces:**
- Consumes: `hs_crack_capture_id_t`, `hs_crack_wordlist_id_t`, attempt lookup/upsert.
- Produces: `HS_CRACK_ATTEMPTS_CSV` at `/sdcard/lab/handshakes/crack_attempts.csv`; exact quoted `crack_state.csv` summary.

- [ ] **Step 1: Write failing state-transition tests**

Cover: completed unchanged `notfound` is found; changed wordlist is stale;
changed PCAP is not found; `found` preserves the exact password; generic cache
matches only the same `HS_CRACK_GENERIC_VERSION`; `error` never satisfies an
exhausted lookup; and forcing a rerun bypasses lookup without deleting the old
row before the new result exists.

- [ ] **Step 2: Run tests and verify RED**

Expected: at least the generic-version and forced-rerun cases fail.

- [ ] **Step 3: Replace the sanitizing `crack_state.csv` writer**

Move summary serialization through the quoted CSV codec. Preserve the public
columns `filename,ssid,size,generic,wordlist,password`. Remove replacement of
commas and quotes with spaces. Return write status to the caller rather than
discarding it.

- [ ] **Step 4: Integrate capture and wordlist identities into single-file flow**

After PCAP download and record extraction, compute capture identity. Before a
source runs, look up its completed attempt. Skip `found` or `notfound` only on
an exact identity match. On match, copy the cached password into the normal
success path. Persist a completed attempt before updating the summary.

- [ ] **Step 5: Add a temporary force-rerun state flag**

Add `bool force_rerun` to `hs_crack_ui_t`. It defaults false when opening the
popup and bypasses completed-attempt lookup for the current invocation only.
Task 6 adds the final UI button.

- [ ] **Step 6: Verify tests and target build**

Expected: a second unchanged run reports a cache hit without starting workers;
a modified wordlist starts workers normally.

---

### Task 4: Safe resume checkpoints for Single and Dual workers

**Files:**
- Modify: `main/hs_crack_cache.h`
- Modify: `main/hs_crack_cache.c`
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `main/main.c` around `hs_crack_worker_t`, `hs_crack_collect`, wordlist enumeration, and cancellation

**Interfaces:**
- Consumes: Task 1 resume API and Task 2 one-pass reader offsets.
- Produces:

```c
bool hs_crack_checkpoint_due(uint64_t completed_since_checkpoint,
                             int64_t elapsed_since_checkpoint_us);
```

with thresholds of 64 completed candidates or 300 seconds.

- [ ] **Step 1: Write failing checkpoint-policy tests**

Assert false at 63 candidates/299 seconds, true at 64 candidates, true at 300
seconds, and false immediately after resetting the checkpoint counters.

- [ ] **Step 2: Run tests and verify RED**

Expected: missing `hs_crack_checkpoint_due`.

- [ ] **Step 3: Add safe-offset tracking**

For a streamed list, keep `reader_offset` separate from `safe_offset`. When a
checkpoint is due, stop enqueueing, call the existing result collector in drain
mode, verify that pending jobs reach zero without crypto error, then promote
`reader_offset` to `safe_offset` and save `.crack_resume`.

- [ ] **Step 4: Add resume startup**

After an exact fingerprint match, `fseek` to `safe_offset`, restore `tried`, and
continue with the next full line. Reject offsets greater than file size. Generic
restarts from its beginning and never creates a resume record.

- [ ] **Step 5: Preserve only safe progress on cancellation**

Cancel stops new jobs and leaves the last persisted safe checkpoint unchanged.
Do not save the current reader position during cancellation. On `found` or a
fully drained EOF, persist the completed attempt and then remove `.crack_resume`.

- [ ] **Step 6: Verify Single and Dual behavior**

Use a fixture whose correct password is immediately after a checkpoint. Stop
and resume before it; both modes must find the password, proving the first
unchecked candidate was not skipped.

---

### Task 5: One-entry PSRAM wordlist cache

**Files:**
- Modify: `main/hs_crack_cache.h`
- Modify: `main/hs_crack_cache.c`
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `main/main.c` wordlist enumeration and cleanup paths

**Interfaces:**
- Consumes: exact wordlist fingerprints.
- Produces:

```c
typedef struct {
    uint8_t *data;
    size_t size;
    hs_crack_wordlist_id_t id;
} hs_crack_wordlist_blob_t;

bool hs_crack_cache_blob_eligible(uint64_t file_size, size_t free_psram);
hs_crack_cache_result_t hs_crack_cache_blob_load(
    const hs_crack_wordlist_id_t *id, hs_crack_wordlist_blob_t *blob);
void hs_crack_cache_blob_release(hs_crack_wordlist_blob_t *blob);
```

- [ ] **Step 1: Write failing eligibility and parser-equivalence tests**

Assert eligible at exactly 8 MiB only when at least file size plus 6 MiB is
free; reject larger files and insufficient reserve. Feed the same fixture
through FILE and blob readers and assert identical accepted candidates and byte
offsets.

- [ ] **Step 2: Run tests and verify RED**

Expected: missing blob API.

- [ ] **Step 3: Implement ESP-only PSRAM allocation**

Use `heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` and never
fall back to internal RAM. The host test build uses ordinary allocated fixture
buffers behind a test-only implementation branch. Key the blob by the full
wordlist identity and terminate the buffer with a sentinel NUL outside its
logical size.

- [ ] **Step 4: Integrate one-entry reuse**

Reuse a matching blob in repeated single-file runs and future batch calls.
Evict on fingerprint change, selection change, allocation pressure, or explicit
batch/session cleanup. Large lists continue through the FILE path.

- [ ] **Step 5: Verify memory constraints on target**

Check an eligible small list and a list larger than 8 MiB. Confirm logs show
PSRAM for the first and streaming for the second, with no decrease in internal
DMA reserve attributable to the blob.

---

### Task 6: Cache/resume UI states and final verification

**Files:**
- Modify: `main/main.c` popup creation, start callback, progress, and completion
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: final single-file cache/resume UX and stable APIs for the later batch manager.

- [ ] **Step 1: Add explicit pre-run decisions**

When a completed attempt matches, show `Cached: already exhausted` or
`Cached password found` with `Close` and `Force re-run`. When resume matches,
show `Resume available: N%` with `Resume` and `Start over`. `Start over` removes
only the matching resume record. These decisions occur after the background
task has downloaded and fingerprinted the capture; the task publishes the
decision through `lv_async_call` and waits on a small FreeRTOS event bit instead
of calling LVGL directly or blocking the LVGL thread.

- [ ] **Step 2: Replace counting copy with byte-progress copy**

Use status stages `Preparing`, `Checking generic`, `Checking WL`,
`Saving result`, and `Complete`. Display exact `tried`, byte percentage,
candidate rate, and ETA. Do not display `Counting` anywhere in the active flow.

- [ ] **Step 3: Surface non-fatal persistence failures**

If checkpoint or completed-attempt persistence fails, continue cracking and
append `Checkpoint unavailable` or `Result found; cache save failed` to the
detail label and logs. Do not report a failed cache write as `notfound`.

- [ ] **Step 4: Run the complete host suite**

Run:

```sh
/tmp/hs_crack_cache_test
python3 tests/test_hs_crack_crypto.py
```

Run `tests/test_handshake_cracker.py` with its documented mbedTLS path on a host
that has the toolchain. Expected: all enabled tests pass and no source-extraction
error remains.

- [ ] **Step 5: Run target build verification**

Run from the configured ESP-IDF shell:

```powershell
idf.py build
```

Expected: complete build and link succeed with zero warnings promoted to errors.

- [ ] **Step 6: Device acceptance checklist**

Verify: first run has no counting pass; cancel creates resumable progress;
resume finds a password beyond the checkpoint; restart invalidates a changed
list; completed `notfound` skips instantly; `Force re-run` starts workers;
`ALL` sorts and handles each list independently; commas/quotes in credentials
round-trip; Single and Dual agree; small lists use PSRAM and large lists stream.

- [ ] **Step 7: Review the final diff without committing**

Run `git diff --check`, inspect only cache-related files, and leave all changes
uncommitted for the user's review.
