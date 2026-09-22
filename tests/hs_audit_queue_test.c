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

    assert(hs_audit_queue_load_latest_with_scratch(a, b, &loaded, &scratch,
                                                   NULL) ==
           HS_AUDIT_QUEUE_OK);
    assert(loaded.sequence == 2 && loaded.items[0].state == HS_AUDIT_ITEM_RUNNING);
    assert(hs_audit_queue_reconcile_after_boot(&loaded));
    assert(loaded.state == HS_AUDIT_BATCH_PAUSED);
    assert(loaded.items[0].state == HS_AUDIT_ITEM_PAUSED);

    FILE *corrupt = fopen(b, "r+b");
    assert(corrupt);
    assert(fseek(corrupt, 20, SEEK_SET) == 0);
    assert(fputc(0x7f, corrupt) != EOF);
    assert(fclose(corrupt) == 0);
    assert(hs_audit_queue_load_latest_with_scratch(a, b, &loaded, &scratch,
                                                   NULL) ==
           HS_AUDIT_QUEUE_OK);
    assert(loaded.sequence == 1);

    assert(unlink(a) == 0 && unlink(b) == 0 && rmdir(dir) == 0);
}

int main(void)
{
    lifecycle_and_bounds();
    round_trip_and_corruption();
    persistence_fallback_and_reconcile();
    puts("hs_audit_queue_test: PASS");
    return 0;
}
