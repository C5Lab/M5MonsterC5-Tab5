#define _POSIX_C_SOURCE 200809L
#include "hs_audit_queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_HEADER_BYTES 40U
#define QUEUE_TRAILER_BYTES 4U
#define QUEUE_BATCH_BYTES 232U
#define QUEUE_ITEM_BYTES 424U

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8U);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8U; ++i) value |= (uint64_t)p[i] << (8U * i);
    return value;
}

static uint32_t crc32_bytes(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^
                  (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ UINT32_MAX;
}

static bool terminated(const char *value, size_t capacity)
{
    return value && memchr(value, '\0', capacity) != NULL;
}

static bool item_state_valid(hs_audit_item_state_t state)
{
    return state >= HS_AUDIT_ITEM_QUEUED && state <= HS_AUDIT_ITEM_CANCELLED;
}

static bool batch_state_valid(hs_audit_batch_state_t state)
{
    return state >= HS_AUDIT_BATCH_READY && state <= HS_AUDIT_BATCH_CANCELLED;
}

static bool terminal(hs_audit_item_state_t state)
{
    return state == HS_AUDIT_ITEM_FOUND || state == HS_AUDIT_ITEM_NOT_FOUND ||
           state == HS_AUDIT_ITEM_ERROR || state == HS_AUDIT_ITEM_INVALID ||
           state == HS_AUDIT_ITEM_CANCELLED;
}

static bool transition_allowed(hs_audit_item_state_t from,
                               hs_audit_item_state_t to)
{
    if (from == to) return true;
    if (terminal(from)) return false;
    switch (from) {
    case HS_AUDIT_ITEM_QUEUED:
        return to == HS_AUDIT_ITEM_SYNCING || to == HS_AUDIT_ITEM_RUNNING ||
               to == HS_AUDIT_ITEM_INVALID || to == HS_AUDIT_ITEM_ERROR ||
               to == HS_AUDIT_ITEM_CANCELLED;
    case HS_AUDIT_ITEM_SYNCING:
        return to == HS_AUDIT_ITEM_QUEUED || to == HS_AUDIT_ITEM_PAUSED ||
               to == HS_AUDIT_ITEM_ERROR || to == HS_AUDIT_ITEM_INVALID ||
               to == HS_AUDIT_ITEM_CANCELLED;
    case HS_AUDIT_ITEM_RUNNING:
        return to == HS_AUDIT_ITEM_PAUSED || to == HS_AUDIT_ITEM_FOUND ||
               to == HS_AUDIT_ITEM_NOT_FOUND || to == HS_AUDIT_ITEM_ERROR ||
               to == HS_AUDIT_ITEM_CANCELLED;
    case HS_AUDIT_ITEM_PAUSED:
        return to == HS_AUDIT_ITEM_SYNCING || to == HS_AUDIT_ITEM_RUNNING ||
               to == HS_AUDIT_ITEM_ERROR || to == HS_AUDIT_ITEM_CANCELLED;
    default:
        return false;
    }
}

void hs_audit_queue_init(hs_audit_queue_t *queue, uint64_t batch_id)
{
    if (!queue) return;
    memset(queue, 0, sizeof(*queue));
    queue->schema_version = HS_AUDIT_QUEUE_SCHEMA_VERSION;
    queue->batch_id = batch_id;
    queue->state = HS_AUDIT_BATCH_READY;
    queue->current_index = SIZE_MAX;
}

hs_audit_queue_result_t hs_audit_queue_append(
    hs_audit_queue_t *queue, const hs_audit_queue_item_t *item)
{
    if (!queue || !item || !item->id || !item_state_valid(item->state) ||
        !terminated(item->local_path, sizeof(item->local_path)) ||
        !item->local_path[0] || !terminated(item->name, sizeof(item->name)) ||
        !terminated(item->reason, sizeof(item->reason))) {
        return HS_AUDIT_QUEUE_RANGE;
    }
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->items[i].id == item->id ||
            (queue->items[i].capture_size == item->capture_size &&
             queue->items[i].capture_crc32 == item->capture_crc32)) {
            return HS_AUDIT_QUEUE_DUPLICATE;
        }
    }
    if (queue->count >= HS_AUDIT_QUEUE_MAX_ITEMS) return HS_AUDIT_QUEUE_FULL;
    queue->items[queue->count++] = *item;
    if (queue->state == HS_AUDIT_BATCH_COMPLETE ||
        queue->state == HS_AUDIT_BATCH_CANCELLED) {
        queue->state = HS_AUDIT_BATCH_READY;
    }
    return HS_AUDIT_QUEUE_OK;
}

bool hs_audit_queue_transition(hs_audit_queue_t *queue, size_t index,
                               hs_audit_item_state_t state)
{
    if (!queue || index >= queue->count || !item_state_valid(state) ||
        !transition_allowed(queue->items[index].state, state)) return false;
    queue->items[index].state = state;
    if (state == HS_AUDIT_ITEM_RUNNING || state == HS_AUDIT_ITEM_SYNCING)
        queue->current_index = index;
    return true;
}

bool hs_audit_queue_remove(hs_audit_queue_t *queue, size_t index)
{
    if (!queue || index >= queue->count) return false;
    hs_audit_item_state_t state = queue->items[index].state;
    if (state == HS_AUDIT_ITEM_RUNNING || state == HS_AUDIT_ITEM_SYNCING)
        return false;
    if (index + 1U < queue->count)
        memmove(&queue->items[index], &queue->items[index + 1U],
                (queue->count - index - 1U) * sizeof(queue->items[0]));
    --queue->count;
    memset(&queue->items[queue->count], 0, sizeof(queue->items[0]));
    if (queue->current_index == index) queue->current_index = SIZE_MAX;
    else if (queue->current_index != SIZE_MAX && queue->current_index > index)
        --queue->current_index;
    if (!queue->count) queue->state = HS_AUDIT_BATCH_READY;
    return true;
}

size_t hs_audit_queue_next(const hs_audit_queue_t *queue)
{
    if (!queue) return SIZE_MAX;
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->items[i].state == HS_AUDIT_ITEM_PAUSED) return i;
    }
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->items[i].state == HS_AUDIT_ITEM_QUEUED) return i;
    }
    return SIZE_MAX;
}

bool hs_audit_queue_reconcile_after_boot(hs_audit_queue_t *queue)
{
    if (!queue) return false;
    bool changed = false;
    for (size_t i = 0; i < queue->count; ++i) {
        hs_audit_queue_item_t *item = &queue->items[i];
        if (item->state == HS_AUDIT_ITEM_RUNNING ||
            item->state == HS_AUDIT_ITEM_SYNCING) {
            item->state = HS_AUDIT_ITEM_PAUSED;
            queue->current_index = i;
            changed = true;
        }
    }
    if (queue->state == HS_AUDIT_BATCH_RUNNING) {
        queue->state = HS_AUDIT_BATCH_PAUSED;
        changed = true;
    }
    return changed;
}

static bool queue_valid(const hs_audit_queue_t *queue)
{
    if (!queue || queue->schema_version != HS_AUDIT_QUEUE_SCHEMA_VERSION ||
        !batch_state_valid(queue->state) || queue->count > HS_AUDIT_QUEUE_MAX_ITEMS ||
        (queue->current_index != SIZE_MAX && queue->current_index >= queue->count) ||
        !terminated(queue->wordlist_path, sizeof(queue->wordlist_path))) return false;
    for (size_t i = 0; i < queue->count; ++i) {
        const hs_audit_queue_item_t *item = &queue->items[i];
        if (!item->id || !item_state_valid(item->state) ||
            item->progress_percent > 100U ||
            !terminated(item->local_path, sizeof(item->local_path)) ||
            !item->local_path[0] || !terminated(item->name, sizeof(item->name)) ||
            !item->name[0] ||
            !terminated(item->reason, sizeof(item->reason))) return false;
    }
    return true;
}

hs_audit_queue_result_t hs_audit_queue_encode(
    const hs_audit_queue_t *queue, uint8_t *output, size_t capacity,
    size_t *length_out)
{
    if (!queue || !output || !length_out || !queue_valid(queue))
        return HS_AUDIT_QUEUE_RANGE;
    size_t payload_length = QUEUE_BATCH_BYTES + queue->count * QUEUE_ITEM_BYTES;
    size_t total = QUEUE_HEADER_BYTES + payload_length + QUEUE_TRAILER_BYTES;
    if (total > capacity || total > HS_AUDIT_QUEUE_MAX_WIRE_BYTES)
        return HS_AUDIT_QUEUE_NO_SPACE;
    memset(output, 0, total);
    memcpy(output, "HSAQ", 4);
    put16(output + 4, HS_AUDIT_QUEUE_SCHEMA_VERSION);
    put16(output + 6, QUEUE_HEADER_BYTES);
    put64(output + 8, queue->sequence);
    put64(output + 16, queue->batch_id);
    put32(output + 24, (uint32_t)payload_length);

    uint8_t *p = output + QUEUE_HEADER_BYTES;
    p[0] = (uint8_t)queue->state;
    p[1] = queue->method;
    put16(p + 2, (uint16_t)queue->count);
    put16(p + 4, queue->current_index == SIZE_MAX
                     ? UINT16_MAX : (uint16_t)queue->current_index);
    memcpy(p + 8, queue->wordlist_path, sizeof(queue->wordlist_path));
    put64(p + 200, queue->wordlist_size);
    put32(p + 208, queue->wordlist_head_crc32);
    put32(p + 212, queue->wordlist_tail_crc32);
    put64(p + 216, queue->started_at_seconds);
    put64(p + 224, queue->active_time_ms);
    p += QUEUE_BATCH_BYTES;

    for (size_t i = 0; i < queue->count; ++i, p += QUEUE_ITEM_BYTES) {
        const hs_audit_queue_item_t *item = &queue->items[i];
        put64(p, item->id);
        p[8] = (uint8_t)item->state;
        p[9] = item->has_session ? 1U : 0U;
        p[10] = item->progress_percent;
        p[11] = item->worker_count;
        put64(p + 16, item->capture_size);
        put32(p + 24, item->capture_crc32);
        memcpy(p + 28, item->session_id, sizeof(item->session_id));
        put64(p + 48, item->tried);
        put64(p + 56, item->elapsed_ms);
        put64(p + 64, item->eta_seconds);
        memcpy(p + 72, item->local_path, sizeof(item->local_path));
        memcpy(p + 264, item->name, sizeof(item->name));
        memcpy(p + 360, item->reason, sizeof(item->reason));
    }
    uint32_t payload_crc = crc32_bytes(output + QUEUE_HEADER_BYTES,
                                       payload_length);
    put32(output + 28, payload_crc);
    put32(output + 32, 0);
    put32(output + 32, crc32_bytes(output, QUEUE_HEADER_BYTES));
    put32(output + QUEUE_HEADER_BYTES + payload_length, payload_crc);
    *length_out = total;
    return HS_AUDIT_QUEUE_OK;
}

hs_audit_queue_result_t hs_audit_queue_decode(
    const uint8_t *wire, size_t length, hs_audit_queue_t *queue_out)
{
    if (!wire || !queue_out || length < QUEUE_HEADER_BYTES +
                                      QUEUE_BATCH_BYTES + QUEUE_TRAILER_BYTES)
        return HS_AUDIT_QUEUE_CORRUPT;
    if (memcmp(wire, "HSAQ", 4) != 0 || get16(wire + 6) != QUEUE_HEADER_BYTES)
        return HS_AUDIT_QUEUE_CORRUPT;
    if (get16(wire + 4) != HS_AUDIT_QUEUE_SCHEMA_VERSION)
        return HS_AUDIT_QUEUE_UNSUPPORTED;
    uint32_t payload_length = get32(wire + 24);
    if ((size_t)payload_length + QUEUE_HEADER_BYTES + QUEUE_TRAILER_BYTES != length)
        return HS_AUDIT_QUEUE_CORRUPT;
    uint8_t header[QUEUE_HEADER_BYTES];
    memcpy(header, wire, sizeof(header));
    uint32_t expected_header_crc = get32(header + 32);
    put32(header + 32, 0);
    uint32_t payload_crc = crc32_bytes(wire + QUEUE_HEADER_BYTES,
                                       payload_length);
    if (crc32_bytes(header, sizeof(header)) != expected_header_crc ||
        payload_crc != get32(wire + 28) ||
        payload_crc != get32(wire + length - QUEUE_TRAILER_BYTES))
        return HS_AUDIT_QUEUE_CORRUPT;

    const uint8_t *p = wire + QUEUE_HEADER_BYTES;
    uint16_t count = get16(p + 2);
    if (count > HS_AUDIT_QUEUE_MAX_ITEMS ||
        payload_length != QUEUE_BATCH_BYTES + (uint32_t)count * QUEUE_ITEM_BYTES)
        return HS_AUDIT_QUEUE_CORRUPT;
    /* Decode directly into the caller-owned object.  hs_audit_queue_t is
     * deliberately large (it contains all 32 persisted rows), so keeping a
     * transactional copy here costs almost 14 KiB of embedded task stack. */
    hs_audit_queue_init(queue_out, get64(wire + 16));
    queue_out->sequence = get64(wire + 8);
    queue_out->state = (hs_audit_batch_state_t)p[0];
    queue_out->method = p[1];
    queue_out->count = count;
    uint16_t current = get16(p + 4);
    queue_out->current_index = current == UINT16_MAX ? SIZE_MAX : current;
    memcpy(queue_out->wordlist_path, p + 8, sizeof(queue_out->wordlist_path));
    queue_out->wordlist_size = get64(p + 200);
    queue_out->wordlist_head_crc32 = get32(p + 208);
    queue_out->wordlist_tail_crc32 = get32(p + 212);
    queue_out->started_at_seconds = get64(p + 216);
    queue_out->active_time_ms = get64(p + 224);
    p += QUEUE_BATCH_BYTES;
    for (size_t i = 0; i < queue_out->count; ++i, p += QUEUE_ITEM_BYTES) {
        hs_audit_queue_item_t *item = &queue_out->items[i];
        item->id = get64(p);
        item->state = (hs_audit_item_state_t)p[8];
        item->has_session = p[9] != 0;
        item->progress_percent = p[10];
        item->worker_count = p[11];
        item->capture_size = get64(p + 16);
        item->capture_crc32 = get32(p + 24);
        memcpy(item->session_id, p + 28, sizeof(item->session_id));
        item->tried = get64(p + 48);
        item->elapsed_ms = get64(p + 56);
        item->eta_seconds = get64(p + 64);
        memcpy(item->local_path, p + 72, sizeof(item->local_path));
        memcpy(item->name, p + 264, sizeof(item->name));
        memcpy(item->reason, p + 360, sizeof(item->reason));
    }
    if (!queue_valid(queue_out)) {
        memset(queue_out, 0, sizeof(*queue_out));
        return HS_AUDIT_QUEUE_CORRUPT;
    }
    return HS_AUDIT_QUEUE_OK;
}

static hs_audit_queue_result_t read_slot(const char *path,
                                         hs_audit_queue_t *queue)
{
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? HS_AUDIT_QUEUE_NOT_FOUND
                                      : HS_AUDIT_QUEUE_IO_ERROR;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return HS_AUDIT_QUEUE_IO_ERROR; }
    long size = ftell(file);
    if (size <= 0 || size > (long)HS_AUDIT_QUEUE_MAX_WIRE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return HS_AUDIT_QUEUE_CORRUPT;
    }
    uint8_t *wire = malloc((size_t)size);
    if (!wire) { fclose(file); return HS_AUDIT_QUEUE_IO_ERROR; }
    bool ok = fread(wire, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0) ok = false;
    hs_audit_queue_result_t result = ok
        ? hs_audit_queue_decode(wire, (size_t)size, queue)
        : HS_AUDIT_QUEUE_IO_ERROR;
    free(wire);
    return result;
}

hs_audit_queue_result_t hs_audit_queue_load_latest_with_scratch(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue_out,
    hs_audit_queue_t *scratch, hs_audit_queue_slot_t *slot_out)
{
    if (!slot_a || !slot_b || !queue_out || !scratch ||
        queue_out == scratch) return HS_AUDIT_QUEUE_RANGE;
    hs_audit_queue_result_t ar = read_slot(slot_a, queue_out);
    hs_audit_queue_result_t br = read_slot(slot_b, scratch);
    bool av = ar == HS_AUDIT_QUEUE_OK, bv = br == HS_AUDIT_QUEUE_OK;
    if (!av && !bv) {
        if (slot_out) *slot_out = HS_AUDIT_QUEUE_SLOT_NONE;
        if (ar == HS_AUDIT_QUEUE_NOT_FOUND && br == HS_AUDIT_QUEUE_NOT_FOUND)
            return HS_AUDIT_QUEUE_NOT_FOUND;
        return ar == HS_AUDIT_QUEUE_IO_ERROR || br == HS_AUDIT_QUEUE_IO_ERROR
                   ? HS_AUDIT_QUEUE_IO_ERROR : HS_AUDIT_QUEUE_CORRUPT;
    }
    bool use_a = av && (!bv || queue_out->sequence >= scratch->sequence);
    if (!use_a) *queue_out = *scratch;
    if (slot_out) *slot_out = use_a ? HS_AUDIT_QUEUE_SLOT_A
                                    : HS_AUDIT_QUEUE_SLOT_B;
    return HS_AUDIT_QUEUE_OK;
}

hs_audit_queue_result_t hs_audit_queue_load_latest(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue_out,
    hs_audit_queue_slot_t *slot_out)
{
    hs_audit_queue_t *scratch = malloc(sizeof(*scratch));
    if (!scratch) return HS_AUDIT_QUEUE_IO_ERROR;
    hs_audit_queue_result_t result = hs_audit_queue_load_latest_with_scratch(
        slot_a, slot_b, queue_out, scratch, slot_out);
    free(scratch);
    return result;
}

static hs_audit_queue_result_t write_slot(const char *path,
                                          const hs_audit_queue_t *queue)
{
    char temporary[512];
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return HS_AUDIT_QUEUE_RANGE;
    uint8_t *wire = malloc(HS_AUDIT_QUEUE_MAX_WIRE_BYTES);
    if (!wire) return HS_AUDIT_QUEUE_IO_ERROR;
    size_t length = 0;
    hs_audit_queue_result_t result = hs_audit_queue_encode(
        queue, wire, HS_AUDIT_QUEUE_MAX_WIRE_BYTES, &length);
    if (result != HS_AUDIT_QUEUE_OK) { free(wire); return result; }
    FILE *file = fopen(temporary, "wb");
    bool ok = file != NULL;
    if (ok) ok = fwrite(wire, 1, length, file) == length;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (file && fclose(file) != 0) ok = false;
    free(wire);
    if (!ok) { unlink(temporary); return HS_AUDIT_QUEUE_IO_ERROR; }
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        return HS_AUDIT_QUEUE_IO_ERROR;
    }
    return HS_AUDIT_QUEUE_OK;
}

hs_audit_queue_result_t hs_audit_queue_save_next_with_scratch(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue,
    hs_audit_queue_t *scratch)
{
    if (!slot_a || !slot_b || !queue || !queue_valid(queue) ||
        !scratch || queue == scratch || queue->sequence == UINT64_MAX)
        return HS_AUDIT_QUEUE_RANGE;

    hs_audit_queue_slot_t slot = HS_AUDIT_QUEUE_SLOT_NONE;
    uint64_t latest_sequence = queue->sequence;
    uint64_t latest_batch_id = queue->batch_id;
    hs_audit_queue_result_t ar = read_slot(slot_a, scratch);
    bool av = ar == HS_AUDIT_QUEUE_OK;
    if (av) {
        slot = HS_AUDIT_QUEUE_SLOT_A;
        latest_sequence = scratch->sequence;
        latest_batch_id = scratch->batch_id;
    }
    hs_audit_queue_result_t br = read_slot(slot_b, scratch);
    bool bv = br == HS_AUDIT_QUEUE_OK;
    if (bv && (!av || scratch->sequence > latest_sequence)) {
        slot = HS_AUDIT_QUEUE_SLOT_B;
        latest_sequence = scratch->sequence;
        latest_batch_id = scratch->batch_id;
    }

    hs_audit_queue_result_t loaded = HS_AUDIT_QUEUE_OK;
    if (!av && !bv) {
        if (ar == HS_AUDIT_QUEUE_NOT_FOUND && br == HS_AUDIT_QUEUE_NOT_FOUND)
            loaded = HS_AUDIT_QUEUE_NOT_FOUND;
        else if (ar == HS_AUDIT_QUEUE_IO_ERROR || br == HS_AUDIT_QUEUE_IO_ERROR)
            loaded = HS_AUDIT_QUEUE_IO_ERROR;
        else
            loaded = HS_AUDIT_QUEUE_CORRUPT;
    }
    if (loaded == HS_AUDIT_QUEUE_OK && latest_batch_id != queue->batch_id)
        return HS_AUDIT_QUEUE_RANGE;
    if (loaded != HS_AUDIT_QUEUE_OK && loaded != HS_AUDIT_QUEUE_NOT_FOUND &&
        loaded != HS_AUDIT_QUEUE_CORRUPT) return loaded;
    uint64_t base = loaded == HS_AUDIT_QUEUE_OK ? latest_sequence : queue->sequence;
    if (base == UINT64_MAX) return HS_AUDIT_QUEUE_RANGE;
    queue->sequence = base + 1U;
    const char *target = slot == HS_AUDIT_QUEUE_SLOT_A ? slot_b : slot_a;
    hs_audit_queue_result_t result = write_slot(target, queue);
    if (result != HS_AUDIT_QUEUE_OK) queue->sequence = base;
    return result;
}

hs_audit_queue_result_t hs_audit_queue_save_next(
    const char *slot_a, const char *slot_b, hs_audit_queue_t *queue)
{
    hs_audit_queue_t *scratch = malloc(sizeof(*scratch));
    if (!scratch) return HS_AUDIT_QUEUE_IO_ERROR;
    hs_audit_queue_result_t result = hs_audit_queue_save_next_with_scratch(
        slot_a, slot_b, queue, scratch);
    free(scratch);
    return result;
}
