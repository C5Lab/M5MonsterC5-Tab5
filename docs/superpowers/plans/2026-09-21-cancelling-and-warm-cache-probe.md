# CRACK/1 Cancelling and Warm-Cache Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Tab5 recognize JanOS `CANCELLING` records and keep worker cache probes at 115200, negotiating the selected fast baud only when the requested file is absent.

**Architecture:** Extend the CRACK/1 parser with one non-terminal message type. Separate cache checking from the raw file-transfer body so `hs_crack_remote_sync_file()` can decide whether a baud transition is necessary before entering receive mode. The first missing-file attempt may use the configured fast rate; retries remain bounded, resumable, and performed at 115200 as before.

**Tech Stack:** ESP-IDF 5.4.1, FreeRTOS, JanOS CRACK/1 protocol 4, CH34x USB CDC, C host tests, Python production-function harnesses.

**Spec:** `docs/Handshake_Crack_Manager_Roadmap.md`; acceptance plan: `docs/superpowers/plans/2026-09-21-stage-0-transport-closure.md`; JanOS producer: `C:/Users/mati/Documents/GitHub/projectZero/ESP32C5/main/crack_worker.c:1352`

## Global Constraints

- Modify Tab5 only. JanOS remains version `1.7.5` and CRACK/1 remains protocol `4`.
- The user compiles and flashes firmware. Do not run an ESP-IDF firmware build.
- Probe commands run on the normal `115200` console for Grove, USB, and M-BUS.
- A cache hit must not negotiate, restore, or otherwise touch the configured fast baud.
- A cache miss may negotiate the per-link rate selected in Setup: Grove/M-BUS or USB CH34x.
- After a failed fast transfer, preserve the existing confirmed restore, safe-boundary checks, three-attempt limit, 115200 fallback, and resume behavior.
- `CANCELLING` is informational and non-terminal. Cancellation completes only after correlated `DONE`, non-running `STATUS`, or `REJECTED code=no_running_job`.
- Preserve the dirty worktree. Do not stage, commit, reset, clean, or modify unrelated files.

## Implementation checkpoint — 2026-09-21

- Implemented: `CANCELLING` parsing with a required job ID and no terminal result.
- Implemented: cache classification before baud negotiation; cache hits return ready without set/restore calls.
- Implemented: missing files negotiate the configured fast rate only on attempt one; probe errors remain on 115200.
- Verified locally: parser RED before implementation, clean `-Werror` cross-compilation after implementation, worker ordering contract 11/11, USB CDC configuration tests 4/4, CMake registration check, and scoped `git diff --check`.
- Independently reviewed: no Critical or Important findings.
- Pending because host GCC is unavailable in this Windows environment: native execution of the generated C harnesses.
- Pending for the user: Tab5 firmware build/flash and warm-cache, cold-cache, and active-cancel serial-log checks from Task 3.

---

### Task 1: Parse `CANCELLING` as a known CRACK/1 message

**Files:**
- Modify: `main/hs_crack_remote_core.h:7-22`
- Modify: `main/hs_crack_remote_core.c:294-443`
- Test: `tests/hs_crack_remote_core_test.c:92-130`

**Interfaces:**
- Consumes: JanOS line `[CRACK/1] CANCELLING job=<job-id>`.
- Produces: `HS_REMOTE_CANCELLING` with the correlated value in `message.job`; no terminal result is inferred.

- [ ] **Step 1: Add a failing parser test**

Add these assertions to the existing parser test near the `STATUS` and `DONE` cases:

```c
CHECK(hs_remote_parse_line(
    "[CRACK/1] CANCELLING job=j-1", &message));
CHECK(message.type == HS_REMOTE_CANCELLING);
CHECK(strcmp(message.job, "j-1") == 0);
CHECK(message.result == HS_REMOTE_RESULT_NONE);
CHECK(!hs_remote_parse_line("[CRACK/1] CANCELLING", &message));
```

The production change that must make this test fail is removing the `CANCELLING` command mapping or accepting it without a job identity.

- [ ] **Step 2: Run the parser test and verify RED**

Run in a host GCC environment:

```sh
gcc -std=c11 -Wall -Wextra -Werror -I main \
    tests/hs_crack_remote_core_test.c \
    main/hs_crack_remote_core.c main/hs_crack_cache.c \
    -o /tmp/hs_crack_remote_core_test
/tmp/hs_crack_remote_core_test
```

Expected: compilation fails because `HS_REMOTE_CANCELLING` is undefined, or the new parse assertion fails because the line is currently unknown.

- [ ] **Step 3: Implement the minimal parser support**

Add `HS_REMOTE_CANCELLING` after `HS_REMOTE_STARTED` in `hs_remote_message_type_t`. Add this command mapping beside `STARTED` and `STATUS`:

```c
COMMAND("CANCELLING", HS_REMOTE_CANCELLING)
```

After generic token extraction and before the `FILE` branch, require a job identity:

```c
if (message->type == HS_REMOTE_CANCELLING) {
    return message->job[0] != '\0';
}
```

Do not assign `HS_REMOTE_RESULT_CANCELLED`: JanOS has acknowledged the request but the worker task may still be leaving PBKDF2 and has not emitted its terminal state.

- [ ] **Step 4: Run the parser test and verify GREEN**

Run the Step 2 command again.

Expected: exit zero and no warnings.

- [ ] **Step 5: Verify cancellation consumption semantics**

Read `hs_crack_remote_cancel_one()` and confirm that a parsed `HS_REMOTE_CANCELLING` falls through the wait loop without setting `confirmed=true`. The following later records must retain their current meaning:

```text
DONE job=j-1 result=cancelled       -> confirmed
STATUS job=j-1 state=cancelled      -> confirmed
REJECTED code=no_running_job        -> confirmed
CANCELLING job=j-1                  -> known, still waiting
```

---

### Task 2: Check the worker cache before negotiating fast baud

**Files:**
- Modify: `main/main.c:61669-61991`
- Modify: `main/main.c:62472-62540`
- Test: `tests/test_fast_uart_sync_fallback_contract.py`
- Test: `tests/test_worker_stage_retry_contract.py`

**Interfaces:**
- Produces enum local to `main.c`:

```c
typedef enum {
    HS_REMOTE_CACHE_ERROR = -1,
    HS_REMOTE_CACHE_MISSING = 0,
    HS_REMOTE_CACHE_PRESENT = 1,
} hs_remote_cache_state_t;
```

- Produces helper:

```c
static hs_remote_cache_state_t hs_crack_remote_check_cache(
    hs_crack_remote_worker_t *worker, const char *kind,
    uint64_t size, uint32_t crc32);
```

- Changes `hs_crack_remote_upload()` to perform only the `receive` plus raw transfer path for a file already proven missing.
- `hs_crack_remote_sync_file()` consumes the cache state and owns the optional baud transition.

- [ ] **Step 1: Extend the production-function harness with cache outcomes**

In `tests/test_fast_uart_sync_fallback_contract.py`, add the enum above and a fake `hs_crack_remote_check_cache()` before appending the production `hs_crack_remote_sync_file()` function. Track:

```c
static unsigned cache_calls;
static hs_remote_cache_state_t cache_results[3];

static hs_remote_cache_state_t hs_crack_remote_check_cache(
    hs_crack_remote_worker_t *worker, const char *kind,
    uint64_t size, uint32_t crc32)
{
    (void)worker;
    (void)kind;
    (void)size;
    (void)crc32;
    return cache_results[cache_calls++];
}
```

Reset all three entries to `HS_REMOTE_CACHE_MISSING` in the existing fixture reset function.

- [ ] **Step 2: Add the failing warm-cache behavior test**

Add this case before the current fast-transfer cases:

```c
reset();
usb.failed = false;
cache_results[0] = HS_REMOTE_CACHE_PRESENT;
CHECK(hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
CHECK(cache_calls == 1U);
CHECK(set_baud_calls == 0U);
CHECK(restore_calls == 0U);
CHECK(upload_calls == 0U);
```

The production change that must make this test fail is moving baud negotiation back before cache classification.

- [ ] **Step 3: Add the failing cold-cache ordering test**

Use a missing USB cache and a successful transfer:

```c
reset();
usb.failed = false;
cache_results[0] = HS_REMOTE_CACHE_MISSING;
set_baud_result = JANOS_BAUD_FAST;
upload_result[0] = true;
CHECK(hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
CHECK(cache_calls == 1U);
CHECK(set_baud_calls == 1U);
CHECK(set_baud_rate == 1500000);
CHECK(upload_calls == 1U);
CHECK(restore_calls == 1U);
```

Also retain the existing assertions proving Grove/M-BUS use `janos_ft_baud` while USB uses `janos_usb_ft_baud`.

- [ ] **Step 4: Add the failing probe-error safety test**

```c
reset();
usb.failed = false;
cache_results[0] = HS_REMOTE_CACHE_ERROR;
cache_results[1] = HS_REMOTE_CACHE_ERROR;
cache_results[2] = HS_REMOTE_CACHE_ERROR;
CHECK(!hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
CHECK(cache_calls == 3U);
CHECK(set_baud_calls == 0U);
CHECK(restore_calls == 0U);
CHECK(upload_calls == 0U);
```

This pins the safe behavior: an uncertain cache probe never enters baud negotiation or raw receive mode.

- [ ] **Step 5: Run the sync harness and verify RED**

```sh
python3 tests/test_fast_uart_sync_fallback_contract.py
```

Expected: the warm-cache case reports a baud transition because production currently calls `janos_uart_set_baud()` before probing.

- [ ] **Step 6: Extract cache checking from the upload body**

Move the following responsibilities from the beginning of `hs_crack_remote_upload()` into `hs_crack_remote_check_cache()`:

- worker status `checking cache`,
- preparation UI text,
- `hs_crack_remote_probe()`,
- probe failure diagnostics and safe-boundary reporting,
- `probe ok present=...` logging.

Return exactly:

```c
if (probe_failed) return HS_REMOTE_CACHE_ERROR;
if (present) return HS_REMOTE_CACHE_PRESENT;
return HS_REMOTE_CACHE_MISSING;
```

Leave `hs_crack_remote_upload()` beginning with `transferring`, block-size selection, `crack_worker receive`, READY validation, prefix verification, and the existing raw transfer logic.

- [ ] **Step 7: Reorder `hs_crack_remote_sync_file()`**

For every bounded stage attempt:

1. Call `hs_crack_remote_check_cache()` while the console is at 115200.
2. On `PRESENT`, report `ready` and return true without any baud operation.
3. On `ERROR`, skip baud negotiation and treat the attempt as failed so the existing retry/backoff policy applies.
4. On `MISSING`, negotiate the selected fast rate only on attempt 1.
5. Call `hs_crack_remote_upload()` only for `MISSING`.
6. Preserve current handling for lost boundaries, confirmed restoration, cancellation, exhaustion, and 115200 retry.

Keep the first cold attempt UI reason as `fast sync` only after `JANOS_BAUD_FAST`; use `sync` when negotiation remains at 115200. Warm cache should go directly from `checking cache` to `ready`.

- [ ] **Step 8: Run focused tests and verify GREEN**

```sh
python3 tests/test_fast_uart_sync_fallback_contract.py
python3 tests/test_worker_stage_retry_contract.py
```

Expected: both exit zero. Update `test_worker_stage_retry_contract.py` only if its ordering contract needs to assert that cache checking appears before the single `janos_uart_set_baud()` call; do not replace executable behavior with source-text-only coverage.

---

### Task 3: Run regression gates and document the new contract

**Files:**
- Modify: `tests/README.md`
- Verify: `main/main.c`
- Verify: `main/hs_crack_remote_core.c`
- Verify: `main/hs_crack_remote_core.h`
- Verify: `tests/hs_crack_remote_core_test.c`
- Verify: `tests/test_fast_uart_sync_fallback_contract.py`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: host evidence and an exact manual hardware checklist for the user.

- [ ] **Step 1: Update host-test documentation**

Document that:

- `hs_crack_remote_core_test.c` recognizes `CANCELLING` without treating it as terminal;
- `test_fast_uart_sync_fallback_contract.py` proves warm-cache hits perform no baud transition;
- cold-cache synchronization negotiates the correct per-link configured rate;
- probe errors remain at 115200 and never enter raw mode.

- [ ] **Step 2: Run the focused and neighboring host regression suite**

```sh
gcc -std=c11 -Wall -Wextra -Werror -I main \
    tests/hs_crack_remote_core_test.c \
    main/hs_crack_remote_core.c main/hs_crack_cache.c \
    -o /tmp/hs_crack_remote_core_test
/tmp/hs_crack_remote_core_test
python3 tests/test_fast_uart_sync_fallback_contract.py
python3 tests/test_usb_fast_baud_contract.py
python3 tests/test_usb_probe_recovery_contract.py
python3 tests/test_usb_start_recovery_contract.py
python3 tests/test_worker_stage_retry_contract.py
```

Expected: every command exits zero without warnings.

- [ ] **Step 3: Run scoped diff checks**

```powershell
git diff --check -- main/main.c main/hs_crack_remote_core.c main/hs_crack_remote_core.h tests/hs_crack_remote_core_test.c tests/test_fast_uart_sync_fallback_contract.py tests/test_worker_stage_retry_contract.py tests/README.md
```

Expected: no scoped whitespace error. CRLF conversion notices are informational.

- [ ] **Step 4: Hand off the warm-cache hardware test**

After the user compiles and flashes Tab5, run unchanged capture and wordlist identities. Required log order for USB:

```text
WORKER USB stage=wordlist ... reason=checking cache
probing wordlist on USB
probe ok present=1 (wordlist)
WORKER USB stage=wordlist ... reason=ready
```

Between those lines there must be no `uart_baud`, `Local bridge configured: 921600`, `Console running at ...`, `READY`, data block, FIN, or restore sequence.

- [ ] **Step 5: Hand off the cold-cache hardware test**

Reset only the selected wordlist identity and repeat. Required order:

```text
probe ok present=0 (wordlist)
Local bridge configured: <selected USB speed>
Console running at <selected USB speed>
READY ...
data/ACK32 ... FIN ... SYNCED
Local bridge configured: 115200
Console back at 115200
WORKER USB stage=wordlist ... reason=ready
```

Expected: the speed selected in Setup is used only after the missing-cache result.

- [ ] **Step 6: Hand off the cancellation parser test**

Cancel an active distributed crack. Expected:

```text
[CRACK/1] CANCELLING job=<matching job>
```

is consumed without `unparsed CRACK line`, followed by a terminal correlated result or the existing bounded `cancel not confirmed` warning if the terminal response is genuinely absent.

## Completion Gate

- `CANCELLING` parses with a required job ID and remains non-terminal.
- A warm cache hit performs zero fast-baud set and restore operations.
- A cold cache miss still uses the selected Grove/M-BUS or USB speed.
- Probe failure performs no baud transition and enters only the existing bounded retry path.
- Fast-transfer failure still restores or safely abandons the boundary exactly as before.
- Resume, ACK32/FIN32, three-attempt accounting, and local fallback contracts remain green.
- The user supplies final compiled-device logs; the agent does not build firmware.
