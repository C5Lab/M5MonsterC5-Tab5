"""Production-source contract for ledger-driven worker reassignment.

The pure scheduler has native behavioral tests.  This contract guards the
orchestration in ``main.c``: both cracking phases must use the same scheduler
tick, idle workers must lease ledger suffixes, and final local fallback must
drain the ledger rather than the historical ``worker->failed`` flag.
"""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(rf"static\s+[^\n]+?\b{name}\s*\([^;]+?\n\{{", SOURCE, re.S)
    assert match, f"missing function {name}"
    start = match.start()
    depth = 0
    opened = False
    for index in range(start, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
            opened = True
        elif SOURCE[index] == "}":
            depth -= 1
            if opened and depth == 0:
                return SOURCE[start : index + 1]
    raise AssertionError(f"unterminated function {name}")


def test_scheduler_tick_polls_active_workers_before_due_recovery():
    tick = production_function("hs_crack_remote_scheduler_tick")
    active = tick.index("worker->active && !worker->recovery_pending")
    recovering = tick.index("worker->recovery_pending")
    assert active < recovering
    assert "hs_crack_remote_poll(worker, recovery" in tick
    assert "hs_crack_remote_assign_pending" in tick


def test_idle_worker_leases_only_authoritative_pending_suffix():
    assign = production_function("hs_crack_remote_assign_pending")
    assert "HS_SCHED_PENDING" in assign
    assert "HS_SCHED_RECOVERING" in assign
    assert "hs_sched_pending_start(shard)" in assign
    assert "shard->range_end" in assign
    start = assign.index("hs_crack_remote_start")
    lease = assign.index("hs_sched_lease_pending")
    assert start < lease
    assert "worker->sched_shard = shard" in assign
    assert "REASSIGN" in assign


def test_both_cracking_phases_use_the_shared_tick_and_recovery_context():
    task = production_function("hs_crack_task")
    assert task.count("hs_crack_remote_scheduler_tick(") >= 2
    assert "hs_crack_remote_recovery_ctx_t recovery_ctx" in task
    assert ".wordlist_path = hs_crack_ui.wl_paths[wi]" in task
    assert "remote_shards" in task


def test_final_fallback_drains_ledger_not_failed_worker_flags():
    task = production_function("hs_crack_task")
    marker = "A remote shard is complete only through its ledger"
    assert marker in task
    drain = task[task.index(marker) : task.index("fclose(wl)", task.index(marker))]
    assert "remote_shards" in drain
    assert "hs_sched_pending_start(shard)" in drain
    assert "shard->range_end" in drain
    assert "worker->failed" not in drain
    assert "hs_sched_complete(shard" in drain


def test_cancel_blocks_new_leases_and_partial_run_is_not_completed():
    assign = production_function("hs_crack_remote_assign_pending")
    assert "hs_crack_ui.cancel_requested" in assign
    task = production_function("hs_crack_task")
    completion = task.index("hs_sched_complete(shard")
    cancel_guard = task.rfind("hs_crack_ui.cancel_requested", 0, completion)
    assert cancel_guard >= 0


def test_recovery_attempts_are_rate_limited_to_five_seconds():
    recover = production_function("hs_crack_remote_try_recover")
    wait = production_function("hs_crack_remote_recovery_wait")
    assert "now < worker->next_recovery_us" in recover
    assert "HS_CRACK_RECOVERY_INTERVAL_US" in wait
    assert "#define HS_CRACK_RECOVERY_INTERVAL_US 5000000LL" in SOURCE


def test_recovery_lifecycle_is_status_first_and_cache_safe():
    recover = production_function("hs_crack_remote_try_recover")
    ping = recover.index("hs_crack_remote_recovery_ping")
    status = recover.index("hs_crack_remote_recovery_status")
    capabilities = recover.index("hs_crack_remote_capabilities")
    capture = recover.index('hs_crack_remote_sync_file(worker, true, "capture"')
    wordlist = recover.index('hs_crack_remote_sync_file(worker, true, "wordlist"')
    start = recover.index("hs_crack_remote_start")
    lease = recover.index("hs_sched_lease_pending")
    assert ping < status < capabilities < capture < wordlist < start < lease
    assert "hs_sched_reattach" in recover
    assert "HS_SCHED_VERIFY_FOUND" in recover
    assert 'hs_crack_remote_recovery_wait(worker, "job ambiguous")' in recover
    assert "state != HS_SCHED_RECOVERING" in recover
    assert "worker->sched_shard = NULL" in recover
    retired = recover[
        recover.index("if (action == HS_SCHED_CANCEL_RETIRED)"):
        recover.index("bool restart =", recover.index("if (action == HS_SCHED_CANCEL_RETIRED)"))
    ]
    assert "hs_sched_note_progress" in retired
    assert "hs_crack_remote_add_checked" in retired


def test_checked_accounting_is_correlated_to_the_active_generation():
    add_checked = production_function("hs_crack_remote_add_checked")
    assert "hs_crack_remote_owns_sched_generation" in add_checked
    owns = production_function("hs_crack_remote_owns_sched_generation")
    assert "shard->owner == hs_crack_sched_owner(worker->tab)" in owns
    assert "strcmp(shard->active_job, worker->job) == 0" in owns


def run_production_tick_harness():
    owner = production_function("hs_crack_sched_owner")
    owner_name = production_function("hs_crack_sched_owner_name")
    owns_generation = production_function("hs_crack_remote_owns_sched_generation")
    add_checked = production_function("hs_crack_remote_add_checked")
    assign = production_function("hs_crack_remote_assign_pending")
    tick = production_function("hs_crack_remote_scheduler_tick")
    harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "hs_crack_remote_core.h"
#include "hs_crack_scheduler.h"

typedef int tab_id_t;
typedef int uart_port_t;
#define TAB_GROVE 0
#define TAB_USB 1
#define TAB_MBUS 2
#define HS_CRACK_REMOTE_MAX 3U
#define TAG "host"

typedef struct {
    tab_id_t tab;
    uart_port_t port;
    bool usable, active, finished, failed, counted, cancel_pending;
    bool usb_lock_set, owner_claimed;
    uint8_t stage_attempt, stage_max;
    char stage[16], stage_reason[24], job[32];
    uint64_t shard_start, shard_end, checked, accounted;
    hs_remote_lease_t lease;
    hs_sched_shard_t *sched_shard;
    bool recovery_pending;
    uint8_t recovery_attempts;
    int64_t next_recovery_us;
    char retired_job[32];
} hs_crack_remote_worker_t;

typedef struct {
    uint64_t capture_size;
    uint32_t capture_crc;
    const char *wordlist_path;
    uint64_t wordlist_size;
    uint32_t wordlist_crc;
} hs_crack_remote_recovery_ctx_t;

static struct {
    bool cancel_requested;
    char worker_status[3][60];
} hs_crack_ui;

static unsigned poll_order[8], poll_count, start_count, publish_count;
static uint64_t started_at, started_end;
static int failures;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    failures++; } } while (0)

static void log_ignore(const char *tag, const char *fmt, ...)
{ (void)tag; (void)fmt; }
'''
    harness += r'''
#define ESP_LOGI log_ignore
#define ESP_LOGW log_ignore
static const char *tab_transport_name(int tab)
{ return tab == TAB_USB ? "USB" : (tab == TAB_MBUS ? "MBus" : "Grove"); }
static void hs_crack_remote_publish_counts(
    const hs_crack_remote_worker_t *workers, size_t count)
{ (void)workers; (void)count; publish_count++; }
static bool hs_crack_remote_cancel_job_id(
    hs_crack_remote_worker_t *worker, const char *job)
{ (void)worker; (void)job; return true; }
static bool hs_crack_remote_start(
    hs_crack_remote_worker_t *worker, uint64_t capture_size,
    uint32_t capture_crc, uint64_t wordlist_size, uint32_t wordlist_crc,
    uint64_t start, uint64_t end, unsigned sequence)
{
    (void)capture_size; (void)capture_crc; (void)wordlist_size;
    (void)wordlist_crc;
    start_count++;
    started_at = start;
    started_end = end;
    snprintf(worker->job, sizeof(worker->job), "new-%u", sequence);
    worker->active = true;
    worker->finished = false;
    worker->failed = false;
    return true;
}
static bool hs_crack_remote_poll(
    hs_crack_remote_worker_t *worker,
    const hs_crack_remote_recovery_ctx_t *recovery,
    uint32_t *tried, char *found_pw, size_t found_pw_size)
{
    (void)recovery; (void)tried; (void)found_pw; (void)found_pw_size;
    poll_order[poll_count++] = (unsigned)worker->tab;
    return false;
}
'''
    harness += ("\n" + owner + "\n" + owner_name + "\n" +
                owns_generation + "\n" + add_checked + "\n" +
                assign + "\n" + tick)
    harness += r'''
int main(void)
{
    hs_crack_remote_worker_t workers[3] = {0};
    hs_sched_shard_t shards[3];
    hs_crack_remote_recovery_ctx_t recovery = {
        .capture_size = 393, .capture_crc = 1,
        .wordlist_path = "words.txt", .wordlist_size = 1000,
        .wordlist_crc = 2,
    };
    uint32_t tried = 0;
    char found[64] = {0};

    hs_sched_init(&shards[0], 0, 400, HS_SCHED_OWNER_GROVE);
    CHECK(hs_sched_bind_active_job(&shards[0], "active"));
    workers[0].tab = TAB_GROVE;
    workers[0].usable = true;
    workers[0].active = true;
    workers[0].sched_shard = &shards[0];

    hs_sched_init(&shards[1], 400, 1000, HS_SCHED_OWNER_USB);
    CHECK(hs_sched_bind_active_job(&shards[1], "old"));
    CHECK(hs_sched_note_progress(&shards[1], 420));
    CHECK(hs_sched_note_loss(&shards[1], "old"));
    workers[1].tab = TAB_USB;
    workers[1].usable = true;
    workers[1].failed = true;
    workers[1].recovery_pending = true;
    workers[1].sched_shard = &shards[1];
    snprintf(workers[1].job, sizeof(workers[1].job), "old");
    workers[1].accounted = 5;

    hs_sched_init(&shards[2], 0, 0, HS_SCHED_OWNER_NONE);
    workers[2].tab = TAB_MBUS;
    workers[2].usable = true;
    workers[2].finished = true;

    CHECK(hs_crack_remote_scheduler_tick(
              workers, 3, shards, 3, &recovery, &tried,
              found, sizeof(found)) == -1);
    CHECK(poll_count == 2);
    CHECK(poll_order[0] == TAB_GROVE);
    CHECK(poll_order[1] == TAB_USB);
    CHECK(start_count == 1);
    CHECK(started_at == 420);
    CHECK(started_end == 1000);
    CHECK(shards[1].state == HS_SCHED_LEASED);
    CHECK(shards[1].owner == HS_SCHED_OWNER_MBUS);
    CHECK(workers[2].sched_shard == &shards[1]);
    CHECK(publish_count == 1);

    hs_crack_remote_add_checked(&workers[1], &tried, 9);
    CHECK(tried == 4);
    CHECK(shards[1].generation_accounted == 0);
    hs_crack_remote_add_checked(&workers[2], &tried, 7);
    CHECK(tried == 11);
    CHECK(shards[1].generation_accounted == 7);

    hs_crack_ui.cancel_requested = true;
    CHECK(!hs_crack_remote_assign_pending(
        workers, 3, shards, 3, &recovery));
    CHECK(start_count == 1);

    if (failures) return 1;
    puts("worker_reassignment_runtime: PASS");
    return 0;
}
'''

    with tempfile.TemporaryDirectory(prefix="worker_reassignment_") as temp_dir:
        binary = str(Path(temp_dir) / "worker_reassignment_test")
        subprocess.run(
            [
                "gcc", "-x", "c", "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(ROOT / "main"), "-",
                str(ROOT / "main" / "hs_crack_scheduler.c"),
                "-o", binary,
            ],
            input=harness,
            text=True,
            check=True,
        )
        subprocess.run([binary], check=True)


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    run_production_tick_harness()
    print(f"test_worker_reassignment_contract: PASS ({len(tests)} tests)")
