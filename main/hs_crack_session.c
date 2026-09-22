#define _POSIX_C_SOURCE 200809L
#include "hs_crack_session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TLV_CAPTURE 1U
#define TLV_ORIGIN 2U
#define TLV_CONFIG 3U
#define TLV_WORDLIST 4U
#define TLV_PHASE 5U
#define TLV_SHARD 6U
#define TLV_WORKER 7U
#define TLV_METRICS 8U
#define TLV_RESULT 9U
#define TLV_OPTIONAL 0x8000U
#define TLV_TYPE_MASK 0x7fffU

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t position;
    bool failed;
} writer_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t position;
    bool failed;
} reader_t;

static uint32_t crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    crc = ~crc;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static void put16_at(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32_at(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void put64_at(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint16_t get16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t get32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t get64_at(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}

static void write_bytes(writer_t *writer, const void *data, size_t length)
{
    if (writer->failed || length > writer->capacity - writer->position) {
        writer->failed = true;
        return;
    }
    memcpy(writer->data + writer->position, data, length);
    writer->position += length;
}

static void write_u8(writer_t *writer, uint8_t value)
{
    write_bytes(writer, &value, 1);
}

static void write_u16(writer_t *writer, uint16_t value)
{
    uint8_t bytes[2];
    put16_at(bytes, value);
    write_bytes(writer, bytes, sizeof(bytes));
}

static void write_u32(writer_t *writer, uint32_t value)
{
    uint8_t bytes[4];
    put32_at(bytes, value);
    write_bytes(writer, bytes, sizeof(bytes));
}

static void write_u64(writer_t *writer, uint64_t value)
{
    uint8_t bytes[8];
    put64_at(bytes, value);
    write_bytes(writer, bytes, sizeof(bytes));
}

static size_t bounded_string_length(const char *text, size_t capacity, bool *valid)
{
    const char *end = memchr(text, '\0', capacity);
    *valid = end != NULL;
    return end ? (size_t)(end - text) : 0;
}

static void write_string(writer_t *writer, const char *text, size_t capacity)
{
    bool valid;
    size_t length = bounded_string_length(text, capacity, &valid);
    if (!valid || length > UINT16_MAX) {
        writer->failed = true;
        return;
    }
    write_u16(writer, (uint16_t)length);
    write_bytes(writer, text, length);
}

static size_t begin_tlv(writer_t *writer, uint16_t type)
{
    size_t header = writer->position;
    write_u16(writer, type);
    write_u16(writer, 0);
    write_u32(writer, 0);
    return header;
}

static void end_tlv(writer_t *writer, size_t header)
{
    if (writer->failed || header > writer->position || writer->position - header < 8 ||
        writer->position - header - 8 > UINT32_MAX) {
        writer->failed = true;
        return;
    }
    put32_at(writer->data + header + 4, (uint32_t)(writer->position - header - 8));
}

static bool read_bytes(reader_t *reader, void *output, size_t length)
{
    if (reader->failed || length > reader->length - reader->position) {
        reader->failed = true;
        return false;
    }
    if (output) memcpy(output, reader->data + reader->position, length);
    reader->position += length;
    return true;
}

static uint8_t read_u8(reader_t *reader)
{
    uint8_t value = 0;
    read_bytes(reader, &value, 1);
    return value;
}

static uint16_t read_u16(reader_t *reader)
{
    uint8_t bytes[2] = {0};
    read_bytes(reader, bytes, sizeof(bytes));
    return get16_at(bytes);
}

static uint32_t read_u32(reader_t *reader)
{
    uint8_t bytes[4] = {0};
    read_bytes(reader, bytes, sizeof(bytes));
    return get32_at(bytes);
}

static uint64_t read_u64(reader_t *reader)
{
    uint8_t bytes[8] = {0};
    read_bytes(reader, bytes, sizeof(bytes));
    return get64_at(bytes);
}

static bool read_string(reader_t *reader, char *output, size_t capacity)
{
    uint16_t length = read_u16(reader);
    if (reader->failed || length >= capacity) {
        reader->failed = true;
        return false;
    }
    if (!read_bytes(reader, output, length)) return false;
    output[length] = '\0';
    return true;
}

static bool valid_session_header(const hs_session_t *session)
{
    return session && session->schema_version == HS_SESSION_SCHEMA_VERSION &&
           session->state >= HS_SESSION_ACTIVE && session->state <= HS_SESSION_TOMBSTONE &&
           session->wordlist_count <= HS_SESSION_MAX_WORDLISTS &&
           session->shard_count <= HS_SESSION_MAX_SHARDS &&
           session->worker_count <= HS_SESSION_MAX_WORKERS &&
           session->capture.ssid_length <= HS_SESSION_SSID_MAX &&
           session->result.password_length <= HS_SESSION_PASSWORD_MAX;
}

static bool validate_semantics(const hs_session_t *session)
{
    bool valid;
    if (!valid_session_header(session)) return false;
    (void)bounded_string_length(session->origin.local_path,
                                sizeof(session->origin.local_path), &valid);
    if (!valid) return false;
    (void)bounded_string_length(session->origin.remote_path,
                                sizeof(session->origin.remote_path), &valid);
    if (!valid) return false;
    if (session->origin.source > HS_SESSION_SOURCE_MBUS ||
        session->phase.kind > HS_SESSION_PHASE_FINALIZING ||
        session->config.requested_workers > HS_SESSION_MAX_WORKERS) return false;
    for (size_t i = 0; i < session->wordlist_count; ++i) {
        (void)bounded_string_length(session->wordlists[i].path,
                                    sizeof(session->wordlists[i].path), &valid);
        if (!valid) return false;
    }
    for (size_t i = 0; i < session->shard_count; ++i) {
        const hs_session_shard_t *shard = &session->shards[i];
        if (shard->kind > HS_SESSION_SHARD_REMOTE || shard->state > HS_SESSION_SHARD_COMPLETE ||
            shard->range_start > shard->safe_offset || shard->safe_offset > shard->range_end)
            return false;
        (void)bounded_string_length(shard->job_id, sizeof(shard->job_id), &valid);
        if (!valid) return false;
        (void)bounded_string_length(shard->retired_job_id,
                                    sizeof(shard->retired_job_id), &valid);
        if (!valid) return false;
    }
    for (size_t i = 0; i < session->worker_count; ++i) {
        const hs_session_worker_t *worker = &session->workers[i];
        if (worker->transport > HS_SESSION_SOURCE_MBUS ||
            worker->state > HS_SESSION_WORKER_LOST) return false;
        (void)bounded_string_length(worker->job_id, sizeof(worker->job_id), &valid);
        if (!valid) return false;
        (void)bounded_string_length(worker->retired_job_id,
                                    sizeof(worker->retired_job_id), &valid);
        if (!valid) return false;
    }
    return !session->has_result || session->result.outcome <= HS_SESSION_OUTCOME_ERROR;
}

bool hs_session_matches_active_wordlist(
    const hs_session_t *session, uint64_t capture_size, uint32_t capture_crc32,
    const char *wordlist_path, uint64_t wordlist_size, uint64_t wordlist_mtime,
    uint32_t wordlist_head_crc32, uint32_t wordlist_tail_crc32)
{
    if (!session || !wordlist_path || session->state != HS_SESSION_ACTIVE ||
        session->phase.kind != HS_SESSION_PHASE_WORDLIST ||
        session->wordlist_count != 1U) {
        return false;
    }
    const hs_session_wordlist_t *saved = &session->wordlists[0];
    return session->capture.size == capture_size &&
           session->capture.crc32 == capture_crc32 &&
           strcmp(saved->path, wordlist_path) == 0 &&
           saved->size == wordlist_size &&
           saved->mtime == wordlist_mtime &&
           saved->head_crc32 == wordlist_head_crc32 &&
           saved->tail_crc32 == wordlist_tail_crc32;
}

int hs_session_find_active_wordlist(
    const hs_session_t *session, uint64_t capture_size, uint32_t capture_crc32,
    const hs_session_wordlist_t *catalog, size_t catalog_count)
{
    if (!catalog && catalog_count) return -1;
    for (size_t i = 0; i < catalog_count; ++i) {
        const hs_session_wordlist_t *candidate = &catalog[i];
        if (hs_session_matches_active_wordlist(
                session, capture_size, capture_crc32, candidate->path,
                candidate->size, candidate->mtime, candidate->head_crc32,
                candidate->tail_crc32)) {
            return i <= INT32_MAX ? (int)i : -1;
        }
    }
    return -1;
}

hs_session_result_t hs_session_encode(const hs_session_t *session, uint8_t *output,
                                      size_t capacity, size_t *length_out)
{
    if (!session || !output || !length_out) return HS_SESSION_RANGE;
    *length_out = 0;
    if (!validate_semantics(session)) return HS_SESSION_RANGE;
    if (capacity > HS_SESSION_MAX_WIRE_BYTES) capacity = HS_SESSION_MAX_WIRE_BYTES;
    if (capacity < HS_SESSION_WIRE_HEADER_BYTES + HS_SESSION_WIRE_TRAILER_BYTES)
        return HS_SESSION_NO_SPACE;

    memset(output, 0, HS_SESSION_WIRE_HEADER_BYTES);
    writer_t writer = {.data = output, .capacity = capacity,
                       .position = HS_SESSION_WIRE_HEADER_BYTES};

    if (session->state != HS_SESSION_TOMBSTONE) {
        size_t tlv = begin_tlv(&writer, TLV_CAPTURE);
        write_u64(&writer, session->capture.size);
        write_u32(&writer, session->capture.crc32);
        write_u32(&writer, session->capture.validator_version);
        write_u32(&writer, session->capture.validation_state);
        write_u32(&writer, session->capture.validation_reason);
        write_u32(&writer, session->capture.record_count);
        write_u32(&writer, session->capture.packet_count);
        write_u32(&writer, session->capture.eapol_count);
        write_u32(&writer, session->capture.ap_nonce_count);
        write_u32(&writer, session->capture.sta_response_count);
        write_u8(&writer, session->capture.ssid_length);
        write_bytes(&writer, session->capture.ssid, session->capture.ssid_length);
        write_bytes(&writer, session->capture.bssid, sizeof(session->capture.bssid));
        end_tlv(&writer, tlv);

        tlv = begin_tlv(&writer, TLV_ORIGIN);
        write_u8(&writer, (uint8_t)session->origin.source);
        write_string(&writer, session->origin.local_path, sizeof(session->origin.local_path));
        write_string(&writer, session->origin.remote_path, sizeof(session->origin.remote_path));
        end_tlv(&writer, tlv);

        tlv = begin_tlv(&writer, TLV_CONFIG);
        write_u8(&writer, session->config.method);
        write_u8(&writer, session->config.selected_source);
        write_u8(&writer, session->config.sync_capture ? 1 : 0);
        write_u8(&writer, session->config.sync_wordlists ? 1 : 0);
        write_u8(&writer, session->config.force_rerun ? 1 : 0);
        write_u8(&writer, session->config.requested_workers);
        end_tlv(&writer, tlv);

        for (size_t i = 0; i < session->wordlist_count; ++i) {
            const hs_session_wordlist_t *wordlist = &session->wordlists[i];
            tlv = begin_tlv(&writer, TLV_WORDLIST);
            write_string(&writer, wordlist->path, sizeof(wordlist->path));
            write_u64(&writer, wordlist->size);
            write_u64(&writer, wordlist->mtime);
            write_u32(&writer, wordlist->head_crc32);
            write_u32(&writer, wordlist->tail_crc32);
            write_u32(&writer, wordlist->full_crc32);
            write_u8(&writer, wordlist->full_crc_valid ? 1 : 0);
            end_tlv(&writer, tlv);
        }

        tlv = begin_tlv(&writer, TLV_PHASE);
        write_u8(&writer, (uint8_t)session->phase.kind);
        write_u16(&writer, session->phase.wordlist_index);
        write_u32(&writer, session->phase.generic_cursor);
        end_tlv(&writer, tlv);

        for (size_t i = 0; i < session->shard_count; ++i) {
            const hs_session_shard_t *shard = &session->shards[i];
            tlv = begin_tlv(&writer, TLV_SHARD);
            write_u8(&writer, shard->shard_id);
            write_u8(&writer, (uint8_t)shard->kind);
            write_u8(&writer, (uint8_t)shard->state);
            write_u8(&writer, (uint8_t)shard->owner);
            write_u8(&writer, (uint8_t)shard->previous_owner);
            write_u64(&writer, shard->range_start);
            write_u64(&writer, shard->range_end);
            write_u64(&writer, shard->safe_offset);
            write_u64(&writer, shard->accounted_checked);
            write_u32(&writer, shard->generation);
            write_string(&writer, shard->job_id, sizeof(shard->job_id));
            write_string(&writer, shard->retired_job_id, sizeof(shard->retired_job_id));
            end_tlv(&writer, tlv);
        }

        for (size_t i = 0; i < session->worker_count; ++i) {
            const hs_session_worker_t *worker = &session->workers[i];
            tlv = begin_tlv(&writer, TLV_WORKER);
            write_u8(&writer, (uint8_t)worker->transport);
            write_u8(&writer, (uint8_t)worker->state);
            write_u8(&writer, worker->miss_count);
            write_u8(&writer, worker->recovery_attempt);
            write_u32(&writer, worker->recovery_delay_ms);
            write_u64(&writer, worker->safe_offset);
            write_u64(&writer, worker->checked);
            write_u64(&writer, worker->rate_milli_per_second);
            write_u64(&writer, worker->capture_size);
            write_u32(&writer, worker->capture_crc32);
            write_u64(&writer, worker->wordlist_size);
            write_u32(&writer, worker->wordlist_crc32);
            write_string(&writer, worker->job_id, sizeof(worker->job_id));
            write_string(&writer, worker->retired_job_id, sizeof(worker->retired_job_id));
            end_tlv(&writer, tlv);
        }

        tlv = begin_tlv(&writer, TLV_METRICS);
        write_u64(&writer, session->metrics.total_tried);
        write_u64(&writer, session->metrics.active_time_ms);
        write_u64(&writer, session->metrics.last_rate_milli_per_second);
        write_u64(&writer, session->metrics.last_eta_seconds);
        write_u64(&writer, session->metrics.last_checkpoint_sequence);
        write_u32(&writer, session->metrics.resume_count);
        end_tlv(&writer, tlv);

        if (session->has_result) {
            tlv = begin_tlv(&writer, TLV_RESULT);
            write_u8(&writer, (uint8_t)session->result.outcome);
            write_u32(&writer, session->result.verified_record_index);
            write_u32(&writer, (uint32_t)session->result.reason);
            write_u8(&writer, session->result.password_length);
            write_bytes(&writer, session->result.password, session->result.password_length);
            end_tlv(&writer, tlv);
        }
    }

    if (writer.failed || writer.position > UINT32_MAX ||
        writer.position + HS_SESSION_WIRE_TRAILER_BYTES > writer.capacity)
        return HS_SESSION_NO_SPACE;
    uint32_t payload_length = (uint32_t)(writer.position - HS_SESSION_WIRE_HEADER_BYTES);
    uint32_t payload_crc = crc32_update(0, output + HS_SESSION_WIRE_HEADER_BYTES,
                                        payload_length);
    write_bytes(&writer, "HSAC", 4);
    write_u32(&writer, payload_crc);
    if (writer.failed) return HS_SESSION_NO_SPACE;

    memcpy(output, "HSA1", 4);
    put16_at(output + 4, HS_SESSION_SCHEMA_VERSION);
    put16_at(output + 6, HS_SESSION_WIRE_HEADER_BYTES);
    put32_at(output + HS_SESSION_WIRE_PAYLOAD_LENGTH_OFFSET, payload_length);
    put64_at(output + 12, session->sequence);
    memcpy(output + 20, session->session_id, HS_SESSION_ID_BYTES);
    output[36] = (uint8_t)session->state;
    put32_at(output + HS_SESSION_WIRE_PAYLOAD_CRC_OFFSET, payload_crc);
    put32_at(output + HS_SESSION_WIRE_HEADER_CRC_OFFSET, 0);
    put32_at(output + HS_SESSION_WIRE_HEADER_CRC_OFFSET,
             crc32_update(0, output, HS_SESSION_WIRE_HEADER_BYTES));
    *length_out = writer.position;
    return HS_SESSION_OK;
}

static bool decode_capture(reader_t *reader, hs_session_capture_t *capture)
{
    capture->size = read_u64(reader);
    capture->crc32 = read_u32(reader);
    capture->validator_version = read_u32(reader);
    capture->validation_state = read_u32(reader);
    capture->validation_reason = read_u32(reader);
    capture->record_count = read_u32(reader);
    capture->packet_count = read_u32(reader);
    capture->eapol_count = read_u32(reader);
    capture->ap_nonce_count = read_u32(reader);
    capture->sta_response_count = read_u32(reader);
    capture->ssid_length = read_u8(reader);
    if (capture->ssid_length > sizeof(capture->ssid)) return false;
    return read_bytes(reader, capture->ssid, capture->ssid_length) &&
           read_bytes(reader, capture->bssid, sizeof(capture->bssid));
}

static bool decode_origin(reader_t *reader, hs_session_origin_t *origin)
{
    origin->source = (hs_session_source_t)read_u8(reader);
    return read_string(reader, origin->local_path, sizeof(origin->local_path)) &&
           read_string(reader, origin->remote_path, sizeof(origin->remote_path));
}

static bool decode_config(reader_t *reader, hs_session_config_t *config)
{
    config->method = read_u8(reader);
    config->selected_source = read_u8(reader);
    uint8_t sync_capture = read_u8(reader), sync_wordlists = read_u8(reader);
    uint8_t force_rerun = read_u8(reader);
    config->requested_workers = read_u8(reader);
    if (sync_capture > 1 || sync_wordlists > 1 || force_rerun > 1) return false;
    config->sync_capture = sync_capture != 0;
    config->sync_wordlists = sync_wordlists != 0;
    config->force_rerun = force_rerun != 0;
    return true;
}

static bool decode_wordlist(reader_t *reader, hs_session_wordlist_t *wordlist)
{
    if (!read_string(reader, wordlist->path, sizeof(wordlist->path))) return false;
    wordlist->size = read_u64(reader);
    wordlist->mtime = read_u64(reader);
    wordlist->head_crc32 = read_u32(reader);
    wordlist->tail_crc32 = read_u32(reader);
    wordlist->full_crc32 = read_u32(reader);
    uint8_t valid = read_u8(reader);
    if (valid > 1) return false;
    wordlist->full_crc_valid = valid != 0;
    return true;
}

static bool decode_shard(reader_t *reader, hs_session_shard_t *shard)
{
    shard->shard_id = read_u8(reader);
    shard->kind = (hs_session_shard_kind_t)read_u8(reader);
    shard->state = (hs_session_shard_state_t)read_u8(reader);
    shard->owner = (int8_t)read_u8(reader);
    shard->previous_owner = (int8_t)read_u8(reader);
    shard->range_start = read_u64(reader);
    shard->range_end = read_u64(reader);
    shard->safe_offset = read_u64(reader);
    shard->accounted_checked = read_u64(reader);
    shard->generation = read_u32(reader);
    return read_string(reader, shard->job_id, sizeof(shard->job_id)) &&
           read_string(reader, shard->retired_job_id, sizeof(shard->retired_job_id));
}

static bool decode_worker(reader_t *reader, hs_session_worker_t *worker)
{
    worker->transport = (hs_session_source_t)read_u8(reader);
    worker->state = (hs_session_worker_state_t)read_u8(reader);
    worker->miss_count = read_u8(reader);
    worker->recovery_attempt = read_u8(reader);
    worker->recovery_delay_ms = read_u32(reader);
    worker->safe_offset = read_u64(reader);
    worker->checked = read_u64(reader);
    worker->rate_milli_per_second = read_u64(reader);
    worker->capture_size = read_u64(reader);
    worker->capture_crc32 = read_u32(reader);
    worker->wordlist_size = read_u64(reader);
    worker->wordlist_crc32 = read_u32(reader);
    return read_string(reader, worker->job_id, sizeof(worker->job_id)) &&
           read_string(reader, worker->retired_job_id, sizeof(worker->retired_job_id));
}

hs_session_result_t hs_session_decode(const uint8_t *wire, size_t length,
                                      hs_session_t *session_out)
{
    if (!wire || !session_out || length < HS_SESSION_WIRE_HEADER_BYTES +
                                             HS_SESSION_WIRE_TRAILER_BYTES ||
        length > HS_SESSION_MAX_WIRE_BYTES) return HS_SESSION_CORRUPT;
    if (memcmp(wire, "HSA1", 4) || get16_at(wire + 6) != HS_SESSION_WIRE_HEADER_BYTES)
        return HS_SESSION_CORRUPT;
    if (get16_at(wire + 4) != HS_SESSION_SCHEMA_VERSION) return HS_SESSION_UNSUPPORTED;
    if (wire[37] != 0 || wire[38] != 0 || wire[39] != 0) return HS_SESSION_CORRUPT;
    uint32_t payload_length = get32_at(wire + HS_SESSION_WIRE_PAYLOAD_LENGTH_OFFSET);
    if ((uint64_t)HS_SESSION_WIRE_HEADER_BYTES + payload_length +
            HS_SESSION_WIRE_TRAILER_BYTES != length) return HS_SESSION_CORRUPT;
    uint8_t header[HS_SESSION_WIRE_HEADER_BYTES];
    memcpy(header, wire, sizeof(header));
    uint32_t expected_header_crc = get32_at(header + HS_SESSION_WIRE_HEADER_CRC_OFFSET);
    put32_at(header + HS_SESSION_WIRE_HEADER_CRC_OFFSET, 0);
    if (crc32_update(0, header, sizeof(header)) != expected_header_crc)
        return HS_SESSION_CORRUPT;
    uint32_t payload_crc = get32_at(wire + HS_SESSION_WIRE_PAYLOAD_CRC_OFFSET);
    const uint8_t *payload = wire + HS_SESSION_WIRE_HEADER_BYTES;
    const uint8_t *trailer = payload + payload_length;
    if (crc32_update(0, payload, payload_length) != payload_crc ||
        memcmp(trailer, "HSAC", 4) || get32_at(trailer + 4) != payload_crc)
        return HS_SESSION_CORRUPT;

    memset(session_out, 0, sizeof(*session_out));
    session_out->schema_version = HS_SESSION_SCHEMA_VERSION;
    session_out->sequence = get64_at(wire + 12);
    memcpy(session_out->session_id, wire + 20, HS_SESSION_ID_BYTES);
    session_out->state = (hs_session_state_t)wire[36];
    if (session_out->state < HS_SESSION_ACTIVE || session_out->state > HS_SESSION_TOMBSTONE)
        return HS_SESSION_CORRUPT;

    bool have_capture = false, have_origin = false, have_config = false;
    bool have_phase = false, have_metrics = false, have_result = false;
    reader_t payload_reader = {.data = payload, .length = payload_length};
    while (payload_reader.position < payload_reader.length) {
        if (payload_reader.length - payload_reader.position < 8) return HS_SESSION_CORRUPT;
        uint16_t wire_type = read_u16(&payload_reader);
        uint16_t flags = read_u16(&payload_reader);
        uint32_t item_length = read_u32(&payload_reader);
        if (payload_reader.failed || flags != 0 ||
            item_length > payload_reader.length - payload_reader.position)
            return HS_SESSION_CORRUPT;
        reader_t item = {.data = payload_reader.data + payload_reader.position,
                         .length = item_length};
        payload_reader.position += item_length;
        uint16_t type = wire_type & TLV_TYPE_MASK;
        bool known = true, duplicate = false;
        switch (type) {
        case TLV_CAPTURE:
            duplicate = have_capture;
            have_capture = true;
            if (!decode_capture(&item, &session_out->capture)) item.failed = true;
            break;
        case TLV_ORIGIN:
            duplicate = have_origin;
            have_origin = true;
            if (!decode_origin(&item, &session_out->origin)) item.failed = true;
            break;
        case TLV_CONFIG:
            duplicate = have_config;
            have_config = true;
            if (!decode_config(&item, &session_out->config)) item.failed = true;
            break;
        case TLV_WORDLIST:
            if (session_out->wordlist_count >= HS_SESSION_MAX_WORDLISTS) item.failed = true;
            else if (!decode_wordlist(&item,
                                      &session_out->wordlists[session_out->wordlist_count++]))
                item.failed = true;
            break;
        case TLV_PHASE:
            duplicate = have_phase;
            have_phase = true;
            session_out->phase.kind = (hs_session_phase_kind_t)read_u8(&item);
            session_out->phase.wordlist_index = read_u16(&item);
            session_out->phase.generic_cursor = read_u32(&item);
            break;
        case TLV_SHARD:
            if (session_out->shard_count >= HS_SESSION_MAX_SHARDS) item.failed = true;
            else if (!decode_shard(&item, &session_out->shards[session_out->shard_count++]))
                item.failed = true;
            break;
        case TLV_WORKER:
            if (session_out->worker_count >= HS_SESSION_MAX_WORKERS) item.failed = true;
            else if (!decode_worker(&item, &session_out->workers[session_out->worker_count++]))
                item.failed = true;
            break;
        case TLV_METRICS:
            duplicate = have_metrics;
            have_metrics = true;
            session_out->metrics.total_tried = read_u64(&item);
            session_out->metrics.active_time_ms = read_u64(&item);
            session_out->metrics.last_rate_milli_per_second = read_u64(&item);
            session_out->metrics.last_eta_seconds = read_u64(&item);
            session_out->metrics.last_checkpoint_sequence = read_u64(&item);
            session_out->metrics.resume_count = read_u32(&item);
            break;
        case TLV_RESULT:
            duplicate = have_result;
            have_result = true;
            session_out->has_result = true;
            session_out->result.outcome = (hs_session_outcome_t)read_u8(&item);
            session_out->result.verified_record_index = read_u32(&item);
            session_out->result.reason = (int32_t)read_u32(&item);
            session_out->result.password_length = read_u8(&item);
            if (session_out->result.password_length > sizeof(session_out->result.password) ||
                !read_bytes(&item, session_out->result.password,
                            session_out->result.password_length)) item.failed = true;
            break;
        default:
            known = false;
            break;
        }
        if (!known) {
            if (!(wire_type & TLV_OPTIONAL)) return HS_SESSION_UNSUPPORTED;
            continue;
        }
        if (wire_type & TLV_OPTIONAL) return HS_SESSION_UNSUPPORTED;
        if (duplicate || item.failed || item.position != item.length) return HS_SESSION_CORRUPT;
    }
    if (session_out->state != HS_SESSION_TOMBSTONE &&
        !(have_capture && have_origin && have_config && have_phase && have_metrics))
        return HS_SESSION_CORRUPT;
    if (!validate_semantics(session_out)) return HS_SESSION_CORRUPT;
    return HS_SESSION_OK;
}

static hs_session_result_t read_slot(const char *path, hs_session_t *session)
{
    if (!path || !session) return HS_SESSION_RANGE;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? HS_SESSION_NOT_FOUND : HS_SESSION_IO_ERROR;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return HS_SESSION_IO_ERROR; }
    long size = ftell(file);
    if (size < 0 || (uint64_t)size > HS_SESSION_MAX_WIRE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) { fclose(file); return HS_SESSION_CORRUPT; }
    uint8_t *wire = malloc((size_t)size);
    if (!wire) { fclose(file); return HS_SESSION_IO_ERROR; }
    size_t got = fread(wire, 1, (size_t)size, file);
    bool failed = got != (size_t)size || ferror(file) || fclose(file) != 0;
    hs_session_result_t result = failed ? HS_SESSION_IO_ERROR
                                        : hs_session_decode(wire, got, session);
    free(wire);
    return result;
}

hs_session_result_t hs_session_load_latest(const char *slot_a, const char *slot_b,
                                           hs_session_t *session_out,
                                           hs_session_slot_t *slot_out)
{
    if (!slot_a || !slot_b || !session_out) return HS_SESSION_RANGE;
    hs_session_t *slots = calloc(2, sizeof(*slots));
    if (!slots) return HS_SESSION_IO_ERROR;
    hs_session_t *a = &slots[0], *b = &slots[1];
    hs_session_result_t result_a = read_slot(slot_a, a);
    hs_session_result_t result_b = read_slot(slot_b, b);
    bool valid_a = result_a == HS_SESSION_OK, valid_b = result_b == HS_SESSION_OK;
    if (valid_a || valid_b) {
        bool choose_b = valid_b && (!valid_a || b->sequence > a->sequence);
        *session_out = choose_b ? *b : *a;
        if (slot_out) *slot_out = choose_b ? HS_SESSION_SLOT_B : HS_SESSION_SLOT_A;
        free(slots);
        return HS_SESSION_OK;
    }
    if (slot_out) *slot_out = HS_SESSION_SLOT_NONE;
    hs_session_result_t result = HS_SESSION_CORRUPT;
    if (result_a == HS_SESSION_NOT_FOUND && result_b == HS_SESSION_NOT_FOUND)
        result = HS_SESSION_NOT_FOUND;
    else if (result_a == HS_SESSION_IO_ERROR || result_b == HS_SESSION_IO_ERROR)
        result = HS_SESSION_IO_ERROR;
    else if (result_a == HS_SESSION_UNSUPPORTED || result_b == HS_SESSION_UNSUPPORTED)
        result = HS_SESSION_UNSUPPORTED;
    free(slots);
    return result;
}

static hs_session_result_t write_slot(const char *path, const uint8_t *wire, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (!file) return HS_SESSION_IO_ERROR;
    bool failed = fwrite(wire, 1, length, file) != length || fflush(file) != 0;
    if (!failed && fsync(fileno(file)) != 0) failed = true;
    if (fclose(file) != 0) failed = true;
    return failed ? HS_SESSION_IO_ERROR : HS_SESSION_OK;
}

hs_session_result_t hs_session_save_next(const char *slot_a, const char *slot_b,
                                         hs_session_t *session)
{
    if (!slot_a || !slot_b || !session) return HS_SESSION_RANGE;
    hs_session_t *slots = calloc(3, sizeof(*slots));
    if (!slots) return HS_SESSION_IO_ERROR;
    hs_session_t *a = &slots[0], *b = &slots[1], *next = &slots[2];
    hs_session_result_t result_a = read_slot(slot_a, a);
    hs_session_result_t result_b = read_slot(slot_b, b);
    bool valid_a = result_a == HS_SESSION_OK, valid_b = result_b == HS_SESSION_OK;
    uint64_t latest = 0;
    const hs_session_t *latest_session = NULL;
    if (valid_a && a->sequence > latest) latest = a->sequence;
    if (valid_b && b->sequence > latest) latest = b->sequence;
    if (valid_a || valid_b) {
        latest_session = valid_b && (!valid_a || b->sequence > a->sequence) ? b : a;
        bool same_id = memcmp(latest_session->session_id, session->session_id,
                              HS_SESSION_ID_BYTES) == 0;
        if (latest_session->state == HS_SESSION_TOMBSTONE) {
            if ((same_id && session->state != HS_SESSION_TOMBSTONE) ||
                (!same_id && session->state != HS_SESSION_ACTIVE)) {
                free(slots);
                return HS_SESSION_MISMATCH;
            }
        } else if (!same_id) {
            free(slots);
            return HS_SESSION_MISMATCH;
        }
    }
    if (latest == UINT64_MAX) {
        free(slots);
        return HS_SESSION_RANGE;
    }
    const char *target;
    if (!valid_a) target = slot_a;
    else if (!valid_b) target = slot_b;
    else target = a->sequence <= b->sequence ? slot_a : slot_b;

    *next = *session;
    next->schema_version = HS_SESSION_SCHEMA_VERSION;
    next->sequence = latest + 1;
    uint8_t *wire = malloc(HS_SESSION_MAX_WIRE_BYTES);
    if (!wire) {
        free(slots);
        return HS_SESSION_IO_ERROR;
    }
    size_t length = 0;
    hs_session_result_t result = hs_session_encode(next, wire,
                                                   HS_SESSION_MAX_WIRE_BYTES, &length);
    if (result == HS_SESSION_OK) result = write_slot(target, wire, length);
    free(wire);
    if (result == HS_SESSION_OK) {
        session->schema_version = next->schema_version;
        session->sequence = next->sequence;
    }
    free(slots);
    return result;
}

hs_session_result_t hs_session_tombstone(const char *slot_a, const char *slot_b,
                                         const uint8_t session_id[HS_SESSION_ID_BYTES])
{
    if (!session_id) return HS_SESSION_RANGE;
    hs_session_t *sessions = calloc(2, sizeof(*sessions));
    if (!sessions) return HS_SESSION_IO_ERROR;
    hs_session_t *latest = &sessions[0], *tombstone = &sessions[1];
    hs_session_result_t result = hs_session_load_latest(slot_a, slot_b,
                                                        latest, NULL);
    if (result == HS_SESSION_OK &&
        memcmp(latest->session_id, session_id, HS_SESSION_ID_BYTES)) {
        result = HS_SESSION_MISMATCH;
    }
    if (result == HS_SESSION_OK) {
        tombstone->schema_version = HS_SESSION_SCHEMA_VERSION;
        tombstone->state = HS_SESSION_TOMBSTONE;
        memcpy(tombstone->session_id, session_id, HS_SESSION_ID_BYTES);
        result = hs_session_save_next(slot_a, slot_b, tombstone);
    }
    free(sessions);
    return result;
}
