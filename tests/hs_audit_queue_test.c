#define _POSIX_C_SOURCE 200809L
#include "hs_audit_queue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static hs_audit_queue_item_t item(uint64_t id, const char *name)
{
    hs_audit_queue_item_t value = {0};
    value.id = id;
    value.state = HS_AUDIT_ITEM_QUEUED;
    value.capture_size = 1000U + id;
    value.capture_crc32 = 0x12340000U + (uint32_t)id;
    snprintf(value.local_path, sizeof(value.local_path), "/captures/%s", name);
    snprintf(value.name, sizeof(value.name), "%s", name);
    return value;
}

static void lifecycle_and_bounds(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 77U);
    assert(queue.state == HS_AUDIT_BATCH_READY);
    hs_audit_queue_item_t first = item(1, "first.pcap");
    assert(hs_audit_queue_append(&queue, &first) == HS_AUDIT_QUEUE_OK);
    assert(hs_audit_queue_append(&queue, &first) == HS_AUDIT_QUEUE_DUPLICATE);
    assert(!hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_FOUND));
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    assert(!hs_audit_queue_remove(&queue, 0));
    queue.items[0].progress_percent = 40;
    queue.items[0].tried = 123;
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_PAUSED));
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_FOUND));
    assert(!hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    assert(hs_audit_queue_remove(&queue, 0));

    for (size_t i = 0; i < HS_AUDIT_QUEUE_MAX_ITEMS; ++i) {
        hs_audit_queue_item_t value = item(i + 10U, "fill.pcap");
        value.capture_crc32 += (uint32_t)i;
        assert(hs_audit_queue_append(&queue, &value) == HS_AUDIT_QUEUE_OK);
    }
    hs_audit_queue_item_t overflow = item(999, "overflow.pcap");
    assert(hs_audit_queue_append(&queue, &overflow) == HS_AUDIT_QUEUE_FULL);
}

static void candidate_source_locking(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 78U);
    assert(hs_audit_queue_can_change_candidate_source(&queue));

    hs_audit_queue_item_t first = item(2, "queued.pcap");
    assert(hs_audit_queue_append(&queue, &first) == HS_AUDIT_QUEUE_OK);
    assert(hs_audit_queue_can_change_candidate_source(&queue));

    queue.state = HS_AUDIT_BATCH_RUNNING;
    assert(!hs_audit_queue_can_change_candidate_source(&queue));
    queue.state = HS_AUDIT_BATCH_PAUSED;
    assert(!hs_audit_queue_can_change_candidate_source(&queue));

    queue.state = HS_AUDIT_BATCH_READY;
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    assert(!hs_audit_queue_can_change_candidate_source(&queue));
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_PAUSED));
    assert(!hs_audit_queue_can_change_candidate_source(&queue));
}

static void starting_a_new_batch_clears_only_queue_execution_state(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 0x12345678U);
    queue.sequence = 9U;
    queue.method = 2U;
    strcpy(queue.wordlist_path, "/lists/rockyou.txt");
    queue.wordlist_size = 12345U;
    queue.wordlist_head_crc32 = 0x11111111U;
    queue.wordlist_tail_crc32 = 0x22222222U;
    queue.started_at_seconds = 100U;
    queue.active_time_ms = 200U;

    hs_audit_queue_item_t first = item(3, "finished.pcap");
    assert(hs_audit_queue_append(&queue, &first) == HS_AUDIT_QUEUE_OK);
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_NOT_FOUND));
    queue.state = HS_AUDIT_BATCH_COMPLETE;
    queue.current_index = 0U;

    hs_audit_queue_reset_batch(&queue);

    assert(queue.schema_version == HS_AUDIT_QUEUE_SCHEMA_VERSION);
    assert(queue.batch_id == 0x12345678U);
    assert(queue.sequence == 9U);
    assert(queue.state == HS_AUDIT_BATCH_READY);
    assert(queue.current_index == SIZE_MAX);
    assert(queue.count == 0U);
    assert(queue.method == 0U);
    assert(queue.wordlist_path[0] == '\0');
    assert(queue.wordlist_size == 0U);
    assert(queue.wordlist_head_crc32 == 0U);
    assert(queue.wordlist_tail_crc32 == 0U);
    assert(queue.started_at_seconds == 0U);
    assert(queue.active_time_ms == 0U);
    assert(hs_audit_queue_can_change_candidate_source(&queue));
}

static void cancelling_a_batch_preserves_results_and_resume_metadata(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 0x55aaU);

    hs_audit_queue_item_t queued = item(20, "queued.pcap");
    queued.has_session = true;
    memset(queued.session_id, 0x20, sizeof(queued.session_id));
    queued.tried = 120U;
    strcpy(queued.reason, "resume checkpoint");
    assert(hs_audit_queue_append(&queue, &queued) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t running = item(21, "running.pcap");
    running.state = HS_AUDIT_ITEM_RUNNING;
    running.has_session = true;
    memset(running.session_id, 0x21, sizeof(running.session_id));
    running.tried = 321U;
    assert(hs_audit_queue_append(&queue, &running) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t paused = item(22, "paused.pcap");
    paused.state = HS_AUDIT_ITEM_PAUSED;
    paused.has_session = true;
    memset(paused.session_id, 0x22, sizeof(paused.session_id));
    assert(hs_audit_queue_append(&queue, &paused) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t found = item(23, "found.pcap");
    found.state = HS_AUDIT_ITEM_FOUND;
    found.progress_percent = 100U;
    strcpy(found.reason, "secret,password\"quoted");
    assert(hs_audit_queue_append(&queue, &found) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t invalid = item(24, "invalid.pcap");
    invalid.state = HS_AUDIT_ITEM_INVALID;
    strcpy(invalid.reason, "missing M2");
    assert(hs_audit_queue_append(&queue, &invalid) == HS_AUDIT_QUEUE_OK);

    queue.state = HS_AUDIT_BATCH_RUNNING;
    queue.current_index = 1U;

    assert(hs_audit_queue_cancel_all(&queue) == 3U);
    assert(queue.state == HS_AUDIT_BATCH_CANCELLED);
    assert(queue.current_index == SIZE_MAX);
    assert(queue.items[0].state == HS_AUDIT_ITEM_CANCELLED);
    assert(queue.items[1].state == HS_AUDIT_ITEM_CANCELLED);
    assert(queue.items[2].state == HS_AUDIT_ITEM_CANCELLED);
    assert(queue.items[3].state == HS_AUDIT_ITEM_FOUND);
    assert(queue.items[4].state == HS_AUDIT_ITEM_INVALID);
    assert(queue.items[0].has_session && queue.items[0].tried == 120U);
    assert(queue.items[1].has_session && queue.items[1].tried == 321U);
    assert(queue.items[2].has_session);
    assert(queue.items[0].session_id[0] == 0x20U);
    assert(queue.items[1].session_id[0] == 0x21U);
    assert(queue.items[2].session_id[0] == 0x22U);
    assert(strcmp(queue.items[0].reason, "resume checkpoint") == 0);
    assert(strcmp(queue.items[3].reason, "secret,password\"quoted") == 0);
    assert(strcmp(queue.items[4].reason, "missing M2") == 0);

    /* The operation is idempotent and an empty queue can still be cancelled. */
    assert(hs_audit_queue_cancel_all(&queue) == 0U);
    hs_audit_queue_reset_batch(&queue);
    assert(hs_audit_queue_cancel_all(&queue) == 0U);
    assert(queue.state == HS_AUDIT_BATCH_CANCELLED);
    assert(queue.current_index == SIZE_MAX);
}

static void cancelled_item_can_be_restored_for_resume(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 0x77aaU);

    hs_audit_queue_item_t cancelled = item(30, "cancelled.pcap");
    cancelled.state = HS_AUDIT_ITEM_CANCELLED;
    cancelled.has_session = true;
    memset(cancelled.session_id, 0x30, sizeof(cancelled.session_id));
    cancelled.progress_percent = 37U;
    cancelled.worker_count = 3U;
    cancelled.tried = 4567U;
    cancelled.elapsed_ms = 8901U;
    cancelled.eta_seconds = 1234U;
    strcpy(cancelled.reason, "cancelled by user");
    assert(hs_audit_queue_append(&queue, &cancelled) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t found = item(31, "found.pcap");
    found.state = HS_AUDIT_ITEM_FOUND;
    strcpy(found.reason, "password");
    assert(hs_audit_queue_append(&queue, &found) == HS_AUDIT_QUEUE_OK);

    queue.state = HS_AUDIT_BATCH_CANCELLED;
    queue.current_index = SIZE_MAX;

    assert(hs_audit_queue_resume_cancelled(&queue, 0U));
    assert(queue.items[0].state == HS_AUDIT_ITEM_PAUSED);
    assert(queue.state == HS_AUDIT_BATCH_PAUSED);
    assert(queue.current_index == 0U);
    assert(queue.items[0].has_session);
    assert(queue.items[0].session_id[0] == 0x30U);
    assert(queue.items[0].progress_percent == 37U);
    assert(queue.items[0].worker_count == 3U);
    assert(queue.items[0].tried == 4567U);
    assert(queue.items[0].elapsed_ms == 8901U);
    assert(queue.items[0].eta_seconds == 1234U);
    assert(strcmp(queue.items[0].reason, "cancelled by user") == 0);
    assert(hs_audit_queue_next(&queue) == 0U);

    assert(!hs_audit_queue_resume_cancelled(&queue, 0U));
    assert(!hs_audit_queue_resume_cancelled(&queue, 1U));
    assert(queue.items[1].state == HS_AUDIT_ITEM_FOUND);
    assert(!hs_audit_queue_resume_cancelled(&queue, queue.count));
    assert(!hs_audit_queue_resume_cancelled(NULL, 0U));
}

static void terminal_batch_can_be_explicitly_recovered(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 0x7799U);

    hs_audit_queue_item_t finished = item(35, "finished.pcap");
    finished.state = HS_AUDIT_ITEM_FOUND;
    finished.progress_percent = 100U;
    strcpy(finished.reason, "known-password");
    assert(hs_audit_queue_append(&queue, &finished) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t resumable = item(36, "resumable.pcap");
    resumable.state = HS_AUDIT_ITEM_CANCELLED;
    resumable.has_session = true;
    memset(resumable.session_id, 0x36, sizeof(resumable.session_id));
    resumable.progress_percent = 41U;
    resumable.worker_count = 3U;
    resumable.tried = 9876U;
    resumable.elapsed_ms = 4321U;
    strcpy(resumable.reason, "cancelled");
    assert(hs_audit_queue_append(&queue, &resumable) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t restart = item(37, "restart.pcap");
    restart.state = HS_AUDIT_ITEM_ERROR;
    restart.progress_percent = 12U;
    restart.worker_count = 2U;
    restart.tried = 345U;
    restart.elapsed_ms = 678U;
    restart.eta_seconds = 999U;
    strcpy(restart.reason, "worker error");
    assert(hs_audit_queue_append(&queue, &restart) == HS_AUDIT_QUEUE_OK);

    hs_audit_queue_item_t invalid = item(38, "invalid.pcap");
    invalid.state = HS_AUDIT_ITEM_INVALID;
    strcpy(invalid.reason, "missing handshake");
    assert(hs_audit_queue_append(&queue, &invalid) == HS_AUDIT_QUEUE_OK);

    queue.state = HS_AUDIT_BATCH_CANCELLED;
    queue.current_index = SIZE_MAX;

    assert(hs_audit_queue_recoverable_count(&queue) == 2U);
    assert(hs_audit_queue_recover_unfinished(&queue) == 2U);
    assert(queue.state == HS_AUDIT_BATCH_PAUSED);
    assert(queue.current_index == 1U);
    assert(queue.items[0].state == HS_AUDIT_ITEM_FOUND);
    assert(strcmp(queue.items[0].reason, "known-password") == 0);
    assert(queue.items[1].state == HS_AUDIT_ITEM_PAUSED);
    assert(queue.items[1].has_session);
    assert(queue.items[1].progress_percent == 41U);
    assert(queue.items[1].tried == 9876U);
    assert(queue.items[1].elapsed_ms == 4321U);
    assert(queue.items[1].reason[0] == '\0');
    assert(queue.items[2].state == HS_AUDIT_ITEM_QUEUED);
    assert(!queue.items[2].has_session);
    assert(queue.items[2].progress_percent == 0U);
    assert(queue.items[2].worker_count == 0U);
    assert(queue.items[2].tried == 0U);
    assert(queue.items[2].elapsed_ms == 0U);
    assert(queue.items[2].eta_seconds == 0U);
    assert(queue.items[2].reason[0] == '\0');
    assert(queue.items[3].state == HS_AUDIT_ITEM_INVALID);
    assert(strcmp(queue.items[3].reason, "missing handshake") == 0);
    assert(hs_audit_queue_next(&queue) == 1U);
    assert(hs_audit_queue_recoverable_count(&queue) == 0U);
    assert(hs_audit_queue_recover_unfinished(&queue) == 0U);
    assert(hs_audit_queue_recoverable_count(NULL) == 0U);
    assert(hs_audit_queue_recover_unfinished(NULL) == 0U);

    hs_audit_queue_t active;
    hs_audit_queue_init(&active, 0x77aaU);
    hs_audit_queue_item_t running = item(39, "running.pcap");
    running.state = HS_AUDIT_ITEM_RUNNING;
    assert(hs_audit_queue_append(&active, &running) == HS_AUDIT_QUEUE_OK);
    hs_audit_queue_item_t old_error = item(40, "old-error.pcap");
    old_error.state = HS_AUDIT_ITEM_ERROR;
    assert(hs_audit_queue_append(&active, &old_error) == HS_AUDIT_QUEUE_OK);
    active.state = HS_AUDIT_BATCH_RUNNING;
    active.current_index = 0U;
    assert(hs_audit_queue_recover_unfinished(&active) == 0U);
    assert(active.items[0].state == HS_AUDIT_ITEM_RUNNING);
    assert(active.items[1].state == HS_AUDIT_ITEM_ERROR);
}

static void running_batch_accepts_work_at_the_tail(void)
{
    hs_audit_queue_t queue;
    hs_audit_queue_init(&queue, 0x88bbU);
    hs_audit_queue_item_t active = item(40, "active.pcap");
    active.state = HS_AUDIT_ITEM_RUNNING;
    assert(hs_audit_queue_append(&queue, &active) == HS_AUDIT_QUEUE_OK);
    queue.state = HS_AUDIT_BATCH_RUNNING;
    queue.current_index = 0U;

    hs_audit_queue_item_t appended = item(41, "appended.pcap");
    assert(hs_audit_queue_append(&queue, &appended) == HS_AUDIT_QUEUE_OK);
    assert(queue.count == 2U);
    assert(queue.state == HS_AUDIT_BATCH_RUNNING);
    assert(queue.current_index == 0U);
    assert(queue.items[0].state == HS_AUDIT_ITEM_RUNNING);
    assert(queue.items[1].state == HS_AUDIT_ITEM_QUEUED);
    assert(hs_audit_queue_next(&queue) == 1U);
}

static void round_trip_and_corruption(void)
{
    hs_audit_queue_t queue, decoded;
    hs_audit_queue_init(&queue, 0xabcdefU);
    queue.sequence = 12;
    queue.state = HS_AUDIT_BATCH_RUNNING;
    queue.method = 2;
    strcpy(queue.wordlist_path, "/lists/rockyou.txt");
    queue.wordlist_size = 15645263;
    queue.wordlist_head_crc32 = 0x11223344U;
    queue.wordlist_tail_crc32 = 0x55667788U;
    hs_audit_queue_item_t value = item(4, "capture.pcap");
    value.state = HS_AUDIT_ITEM_RUNNING;
    value.has_session = true;
    memset(value.session_id, 0xa5, sizeof(value.session_id));
    value.progress_percent = 37;
    value.tried = 999;
    value.elapsed_ms = 12345;
    value.eta_seconds = 88;
    value.worker_count = 3;
    strcpy(value.reason, "working");
    assert(hs_audit_queue_append(&queue, &value) == HS_AUDIT_QUEUE_OK);

    uint8_t wire[HS_AUDIT_QUEUE_MAX_WIRE_BYTES];
    size_t length = 0;
    assert(hs_audit_queue_encode(&queue, wire, sizeof(wire), &length) ==
           HS_AUDIT_QUEUE_OK);
    assert(hs_audit_queue_decode(wire, length, &decoded) == HS_AUDIT_QUEUE_OK);
    assert(decoded.sequence == 12 && decoded.batch_id == 0xabcdefU);
    assert(decoded.count == 1 && decoded.items[0].tried == 999);
    assert(strcmp(decoded.items[0].local_path, "/captures/capture.pcap") == 0);
    wire[length / 2U] ^= 1U;
    assert(hs_audit_queue_decode(wire, length, &decoded) ==
           HS_AUDIT_QUEUE_CORRUPT);
}

static void persistence_fallback_and_reconcile(void)
{
    char dir[] = "/tmp/hs-audit-queue-XXXXXX";
    assert(mkdtemp(dir));
    char a[256], b[256];
    snprintf(a, sizeof(a), "%s/batch.a", dir);
    snprintf(b, sizeof(b), "%s/batch.b", dir);

    hs_audit_queue_t queue, loaded, scratch;
    hs_audit_queue_init(&queue, 99);
    hs_audit_queue_item_t first = item(1, "one.pcap");
    assert(hs_audit_queue_append(&queue, &first) == HS_AUDIT_QUEUE_OK);
    assert(hs_audit_queue_save_next_with_scratch(a, b, &queue, &scratch) ==
           HS_AUDIT_QUEUE_OK);
    assert(queue.sequence == 1);
    assert(hs_audit_queue_transition(&queue, 0, HS_AUDIT_ITEM_RUNNING));
    queue.state = HS_AUDIT_BATCH_RUNNING;
    assert(hs_audit_queue_save_next_with_scratch(a, b, &queue, &scratch) ==
           HS_AUDIT_QUEUE_OK);
    assert(queue.sequence == 2);
    strcpy(queue.items[0].reason, "checkpoint after both slots exist");
    assert(hs_audit_queue_save_next_with_scratch(a, b, &queue, &scratch) ==
           HS_AUDIT_QUEUE_OK);
    assert(queue.sequence == 3);

    assert(hs_audit_queue_load_latest_with_scratch(a, b, &loaded, &scratch,
                                                   NULL) ==
           HS_AUDIT_QUEUE_OK);
    assert(loaded.sequence == 3 && loaded.items[0].state == HS_AUDIT_ITEM_RUNNING);
    assert(strcmp(loaded.items[0].reason,
                  "checkpoint after both slots exist") == 0);
    assert(hs_audit_queue_reconcile_after_boot(&loaded));
    assert(loaded.state == HS_AUDIT_BATCH_PAUSED);
    assert(loaded.items[0].state == HS_AUDIT_ITEM_PAUSED);

    FILE *corrupt = fopen(a, "r+b");
    assert(corrupt);
    assert(fseek(corrupt, 20, SEEK_SET) == 0);
    assert(fputc(0x7f, corrupt) != EOF);
    assert(fclose(corrupt) == 0);
    assert(hs_audit_queue_load_latest_with_scratch(a, b, &loaded, &scratch,
                                                   NULL) ==
           HS_AUDIT_QUEUE_OK);
    assert(loaded.sequence == 2);

    assert(unlink(a) == 0 && unlink(b) == 0 && rmdir(dir) == 0);
}

int main(void)
{
    lifecycle_and_bounds();
    candidate_source_locking();
    starting_a_new_batch_clears_only_queue_execution_state();
    cancelling_a_batch_preserves_results_and_resume_metadata();
    cancelled_item_can_be_restored_for_resume();
    terminal_batch_can_be_explicitly_recovered();
    running_batch_accepts_work_at_the_tail();
    round_trip_and_corruption();
    persistence_fallback_and_reconcile();
    puts("hs_audit_queue_test: PASS");
    return 0;
}
