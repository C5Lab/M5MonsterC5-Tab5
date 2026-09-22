#ifndef HS_AUDIT_QUEUE_H
#define HS_AUDIT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_AUDIT_QUEUE_SCHEMA_VERSION 1U
#define HS_AUDIT_QUEUE_MAX_ITEMS 32U
#define HS_AUDIT_QUEUE_PATH_MAX 192U
#define HS_AUDIT_QUEUE_NAME_MAX 96U
#define HS_AUDIT_QUEUE_REASON_MAX 64U
#define HS_AUDIT_QUEUE_SESSION_ID_BYTES 16U
#define HS_AUDIT_QUEUE_MAX_WIRE_BYTES 32768U

typedef enum {
    HS_AUDIT_QUEUE_OK = 0,
    HS_AUDIT_QUEUE_NOT_FOUND,
    HS_AUDIT_QUEUE_IO_ERROR,
    HS_AUDIT_QUEUE_NO_SPACE,
    HS_AUDIT_QUEUE_RANGE,
    HS_AUDIT_QUEUE_CORRUPT,
    HS_AUDIT_QUEUE_UNSUPPORTED,
    HS_AUDIT_QUEUE_DUPLICATE,
    HS_AUDIT_QUEUE_FULL,
} hs_audit_queue_result_t;

typedef enum {
    HS_AUDIT_BATCH_READY = 0,
    HS_AUDIT_BATCH_RUNNING,
    HS_AUDIT_BATCH_PAUSED,
    HS_AUDIT_BATCH_COMPLETE,
    HS_AUDIT_BATCH_CANCELLED,
} hs_audit_batch_state_t;

typedef enum {
    HS_AUDIT_ITEM_QUEUED = 0,
    HS_AUDIT_ITEM_SYNCING,
    HS_AUDIT_ITEM_RUNNING,
    HS_AUDIT_ITEM_PAUSED,
    HS_AUDIT_ITEM_FOUND,
    HS_AUDIT_ITEM_NOT_FOUND,
    HS_AUDIT_ITEM_ERROR,
    HS_AUDIT_ITEM_INVALID,
    HS_AUDIT_ITEM_CANCELLED,
} hs_audit_item_state_t;

typedef enum {
    HS_AUDIT_QUEUE_SLOT_NONE = 0,
    HS_AUDIT_QUEUE_SLOT_A,
    HS_AUDIT_QUEUE_SLOT_B,
} hs_audit_queue_slot_t;

typedef struct {
    uint64_t id;
    hs_audit_item_state_t state;
    uint64_t capture_size;
    uint32_t capture_crc32;
    char local_path[HS_AUDIT_QUEUE_PATH_MAX];
    char name[HS_AUDIT_QUEUE_NAME_MAX];
    bool has_session;
    uint8_t session_id[HS_AUDIT_QUEUE_SESSION_ID_BYTES];
    uint8_t progress_percent;
    uint8_t worker_count;
    uint64_t tried;
    uint64_t elapsed_ms;
    uint64_t eta_seconds;
    char reason[HS_AUDIT_QUEUE_REASON_MAX];
} hs_audit_queue_item_t;

typedef struct {
    uint16_t schema_version;
    uint64_t sequence;
    uint64_t batch_id;
    hs_audit_batch_state_t state;
    uint8_t method;
    char wordlist_path[HS_AUDIT_QUEUE_PATH_MAX];
    uint64_t wordlist_size;
    uint32_t wordlist_head_crc32;
    uint32_t wordlist_tail_crc32;
    uint64_t started_at_seconds;
    uint64_t active_time_ms;
    size_t current_index;
    size_t count;
    hs_audit_queue_item_t items[HS_AUDIT_QUEUE_MAX_ITEMS];
} hs_audit_queue_t;

void hs_audit_queue_init(hs_audit_queue_t *queue, uint64_t batch_id);
hs_audit_queue_result_t hs_audit_queue_append(
    hs_audit_queue_t *queue, const hs_audit_queue_item_t *item);
bool hs_audit_queue_transition(hs_audit_queue_t *queue, size_t index,
                               hs_audit_item_state_t state);
bool hs_audit_queue_remove(hs_audit_queue_t *queue, size_t index);
size_t hs_audit_queue_next(const hs_audit_queue_t *queue);
bool hs_audit_queue_reconcile_after_boot(hs_audit_queue_t *queue);

hs_audit_queue_result_t hs_audit_queue_encode(
    const hs_audit_queue_t *queue, uint8_t *output, size_t capacity,
    size_t *length_out);
hs_audit_queue_result_t hs_audit_queue_decode(
    const uint8_t *wire, size_t length, hs_audit_queue_t *queue_out);
hs_audit_queue_result_t hs_audit_queue_load_latest(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue_out,
    hs_audit_queue_slot_t *slot_out);
hs_audit_queue_result_t hs_audit_queue_load_latest_with_scratch(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue_out,
    hs_audit_queue_t *scratch, hs_audit_queue_slot_t *slot_out);
hs_audit_queue_result_t hs_audit_queue_save_next(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue);
hs_audit_queue_result_t hs_audit_queue_save_next_with_scratch(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue,
    hs_audit_queue_t *scratch);

#endif
