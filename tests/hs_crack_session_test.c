#define _POSIX_C_SOURCE 200809L
#include "hs_crack_session.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint8_t *find_tlv(uint8_t *wire, size_t length, uint16_t wanted)
{
    uint32_t payload_length = get32(wire + HS_SESSION_WIRE_PAYLOAD_LENGTH_OFFSET);
    uint8_t *cursor = wire + HS_SESSION_WIRE_HEADER_BYTES;
    uint8_t *end = cursor + payload_length;
    assert(end + HS_SESSION_WIRE_TRAILER_BYTES == wire + length);
    while ((size_t)(end - cursor) >= 8) {
        uint32_t item_length = get32(cursor + 4);
        assert(item_length <= (uint32_t)(end - cursor - 8));
        if (get16(cursor) == wanted) return cursor;
        cursor += 8 + item_length;
    }
    return NULL;
}

static void repair_wire_crc(uint8_t *wire, size_t length)
{
    assert(length >= HS_SESSION_WIRE_HEADER_BYTES + HS_SESSION_WIRE_TRAILER_BYTES);
    uint32_t payload_length = get32(wire + HS_SESSION_WIRE_PAYLOAD_LENGTH_OFFSET);
    assert(length == HS_SESSION_WIRE_HEADER_BYTES + payload_length +
                     HS_SESSION_WIRE_TRAILER_BYTES);
    uint32_t payload_crc = crc32_update(0, wire + HS_SESSION_WIRE_HEADER_BYTES,
                                       payload_length);
    put32(wire + HS_SESSION_WIRE_PAYLOAD_CRC_OFFSET, payload_crc);
    put32(wire + HS_SESSION_WIRE_HEADER_CRC_OFFSET, 0);
    put32(wire + HS_SESSION_WIRE_HEADER_CRC_OFFSET,
          crc32_update(0, wire, HS_SESSION_WIRE_HEADER_BYTES));
    put32(wire + length - 4, payload_crc);
}

static void fill_session(hs_session_t *session)
{
    memset(session, 0, sizeof(*session));
    session->schema_version = HS_SESSION_SCHEMA_VERSION;
    session->sequence = 41;
    session->state = HS_SESSION_ACTIVE;
    for (unsigned i = 0; i < sizeof(session->session_id); ++i)
        session->session_id[i] = (uint8_t)(0xa0u + i);

    session->capture.size = 11202;
    session->capture.crc32 = 0x57bf22af;
    session->capture.validator_version = 1;
    session->capture.validation_state = 0;
    session->capture.validation_reason = 0;
    session->capture.record_count = 2;
    session->capture.packet_count = 99;
    session->capture.eapol_count = 4;
    session->capture.ap_nonce_count = 2;
    session->capture.sta_response_count = 2;
    session->capture.ssid_length = 8;
    memcpy(session->capture.ssid, "Lab\0SSID", 8);
    memcpy(session->capture.bssid, "\x02\0\0\0\0\x01", 6);

    session->origin.source = HS_SESSION_SOURCE_USB;
    strcpy(session->origin.local_path, "/sdcard/lab/handshakes/input.pcap");
    strcpy(session->origin.remote_path, "/sdcard/lab/handshakes/source name.pcap");

    session->config.method = 2;
    session->config.selected_source = 4;
    session->config.sync_capture = true;
    session->config.sync_wordlists = true;
    session->config.force_rerun = false;
    session->config.requested_workers = 3;

    session->wordlist_count = HS_SESSION_MAX_WORDLISTS;
    for (size_t i = 0; i < session->wordlist_count; ++i) {
        hs_session_wordlist_t *wordlist = &session->wordlists[i];
        snprintf(wordlist->path, sizeof(wordlist->path), "/sdcard/lab/wordlist/list-%zu.txt", i);
        wordlist->size = 1000 + i;
        wordlist->mtime = 2000 + i;
        wordlist->head_crc32 = 0x10000000u + (uint32_t)i;
        wordlist->tail_crc32 = 0x20000000u + (uint32_t)i;
        wordlist->full_crc32 = 0x30000000u + (uint32_t)i;
        wordlist->full_crc_valid = true;
    }

    session->phase.kind = HS_SESSION_PHASE_WORDLIST;
    session->phase.wordlist_index = 3;
    session->phase.generic_cursor = 17;

    session->shard_count = HS_SESSION_MAX_SHARDS;
    for (size_t i = 0; i < session->shard_count; ++i) {
        hs_session_shard_t *shard = &session->shards[i];
        shard->shard_id = (uint8_t)i;
        shard->kind = i == 0 ? HS_SESSION_SHARD_LOCAL : HS_SESSION_SHARD_REMOTE;
        shard->state = (hs_session_shard_state_t)(i % (HS_SESSION_SHARD_COMPLETE + 1));
        shard->owner = (int8_t)(i - 1);
        shard->previous_owner = (int8_t)(i - 2);
        shard->range_start = i * 10000;
        shard->range_end = shard->range_start + 9999;
        shard->safe_offset = shard->range_start + 123;
        shard->accounted_checked = 456 + i;
        shard->generation = 7 + (uint32_t)i;
        snprintf(shard->job_id, sizeof(shard->job_id), "session-shard-%zu-g%u", i,
                 shard->generation);
        snprintf(shard->retired_job_id, sizeof(shard->retired_job_id), "retired-%zu", i);
    }

    session->worker_count = HS_SESSION_MAX_WORKERS;
    for (size_t i = 0; i < session->worker_count; ++i) {
        hs_session_worker_t *worker = &session->workers[i];
        worker->transport = (hs_session_source_t)(HS_SESSION_SOURCE_GROVE + i);
        worker->state = (hs_session_worker_state_t)(i % (HS_SESSION_WORKER_LOST + 1));
        worker->miss_count = (uint8_t)i;
        worker->recovery_attempt = (uint8_t)(i + 1);
        worker->recovery_delay_ms = 5000 + (uint32_t)i;
        worker->safe_offset = 100 + i;
        worker->checked = 200 + i;
        worker->rate_milli_per_second = 300 + i;
        worker->capture_size = session->capture.size;
        worker->capture_crc32 = session->capture.crc32;
        worker->wordlist_size = session->wordlists[0].size;
        worker->wordlist_crc32 = session->wordlists[0].full_crc32;
        snprintf(worker->job_id, sizeof(worker->job_id), "worker-%zu", i);
        snprintf(worker->retired_job_id, sizeof(worker->retired_job_id), "old-worker-%zu", i);
    }

    session->metrics.total_tried = 123456;
    session->metrics.active_time_ms = 987654;
    session->metrics.last_rate_milli_per_second = 3210;
    session->metrics.last_eta_seconds = 4567;
    session->metrics.last_checkpoint_sequence = 40;
    session->metrics.resume_count = 2;
    session->has_result = true;
    session->result.outcome = HS_SESSION_OUTCOME_FOUND;
    session->result.verified_record_index = 1;
    session->result.reason = 7;
    session->result.password_length = 8;
    memcpy(session->result.password, "p\0asswd!", 8);
}

static void round_trip_and_bounds(void)
{
    hs_session_t input, decoded;
    fill_session(&input);
    uint8_t wire[HS_SESSION_MAX_WIRE_BYTES], second[HS_SESSION_MAX_WIRE_BYTES];
    size_t length = 0, second_length = 0;
    assert(hs_session_encode(&input, wire, sizeof(wire), &length) == HS_SESSION_OK);
    assert(length > HS_SESSION_WIRE_HEADER_BYTES + HS_SESSION_WIRE_TRAILER_BYTES);
    assert(hs_session_decode(wire, length, &decoded) == HS_SESSION_OK);
    assert(decoded.sequence == input.sequence && decoded.state == input.state);
    assert(!memcmp(decoded.session_id, input.session_id, sizeof(input.session_id)));
    assert(decoded.wordlist_count == HS_SESSION_MAX_WORDLISTS);
    assert(decoded.shard_count == HS_SESSION_MAX_SHARDS);
    assert(decoded.worker_count == HS_SESSION_MAX_WORKERS);
    assert(decoded.capture.ssid_length == 8 && !memcmp(decoded.capture.ssid, "Lab\0SSID", 8));
    assert(decoded.has_result && decoded.result.password_length == 8);
    assert(!memcmp(decoded.result.password, "p\0asswd!", 8));
    assert(hs_session_encode(&decoded, second, sizeof(second), &second_length) == HS_SESSION_OK);
    assert(second_length == length && !memcmp(second, wire, length));
    assert(hs_session_encode(&input, wire, length - 1, &second_length) == HS_SESSION_NO_SPACE);

    input.wordlist_count = HS_SESSION_MAX_WORDLISTS + 1;
    assert(hs_session_encode(&input, wire, sizeof(wire), &length) == HS_SESSION_RANGE);
    fill_session(&input);
    input.capture.ssid_length = sizeof(input.capture.ssid) + 1;
    assert(hs_session_encode(&input, wire, sizeof(wire), &length) == HS_SESSION_RANGE);
    fill_session(&input);
    memset(input.origin.remote_path, 'x', sizeof(input.origin.remote_path));
    assert(hs_session_encode(&input, wire, sizeof(wire), &length) == HS_SESSION_RANGE);
}

static void minimal_round_trip(void)
{
    hs_session_t input = {0}, decoded;
    input.schema_version = HS_SESSION_SCHEMA_VERSION;
    input.state = HS_SESSION_ACTIVE;
    input.session_id[0] = 1;
    uint8_t wire[HS_SESSION_MAX_WIRE_BYTES];
    size_t length = 0;
    assert(hs_session_encode(&input, wire, sizeof(wire), &length) == HS_SESSION_OK);
    assert(hs_session_decode(wire, length, &decoded) == HS_SESSION_OK);
    assert(decoded.wordlist_count == 0 && decoded.shard_count == 0 &&
           decoded.worker_count == 0 && !decoded.has_result);
    assert(decoded.session_id[0] == 1 && decoded.sequence == 0);
}

static void corruption_and_tlv_rules(void)
{
    hs_session_t session, out;
    fill_session(&session);
    uint8_t wire[HS_SESSION_MAX_WIRE_BYTES];
    size_t length = 0;
    assert(hs_session_encode(&session, wire, sizeof(wire), &length) == HS_SESSION_OK);
    assert(hs_session_decode(wire, length - 1, &out) == HS_SESSION_CORRUPT);

    uint8_t saved = wire[0];
    wire[0] ^= 1;
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);
    wire[0] = saved;
    wire[HS_SESSION_WIRE_HEADER_BYTES + 10] ^= 1;
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);
    wire[HS_SESSION_WIRE_HEADER_BYTES + 10] ^= 1;
    wire[length - 1] ^= 1;
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);
    wire[length - 1] ^= 1;

    wire[37] = 1; /* reserved header bytes must stay canonical zero */
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);
    wire[37] = 0;
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_OK);

    /* The first payload item is mandatory capture metadata. */
    put16(wire + HS_SESSION_WIRE_HEADER_BYTES, 0x7ffe);
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_UNSUPPORTED);
    put16(wire + HS_SESSION_WIRE_HEADER_BYTES, 1);
    uint8_t *result = find_tlv(wire, length, 9);
    assert(result);
    put16(result, 0x800a); /* optional unknown */
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_OK);
    assert(!out.has_result);

    /* A valid CRC does not make a TLV length extending beyond payload valid. */
    put32(wire + HS_SESSION_WIRE_HEADER_BYTES + 4, UINT32_MAX);
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);

    /* Tombstones may omit payload, but known payload carried by a CRC-valid
     * tombstone must still pass normal semantic checks. */
    assert(hs_session_encode(&session, wire, sizeof(wire), &length) == HS_SESSION_OK);
    uint8_t *origin = find_tlv(wire, length, 2);
    assert(origin && get32(origin + 4) > 0);
    origin[8] = 99; /* invalid source enum */
    wire[36] = HS_SESSION_TOMBSTONE;
    repair_wire_crc(wire, length);
    assert(hs_session_decode(wire, length, &out) == HS_SESSION_CORRUPT);
}

static void write_bytes(const char *path, const uint8_t *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, length, file) == length && fclose(file) == 0);
}

static void ab_store_and_tombstone(void)
{
    char directory[] = "/tmp/hs-session-test-XXXXXX";
    assert(mkdtemp(directory));
    char a[256], b[256];
    snprintf(a, sizeof(a), "%s/active.a", directory);
    snprintf(b, sizeof(b), "%s/active.b", directory);
    hs_session_t session, loaded;
    fill_session(&session);
    session.sequence = 999; /* save chooses the durable next sequence. */
    assert(hs_session_load_latest(a, b, &loaded, NULL) == HS_SESSION_NOT_FOUND);
    assert(hs_session_save_next(a, b, &session) == HS_SESSION_OK && session.sequence == 1);
    hs_session_slot_t slot;
    assert(hs_session_load_latest(a, b, &loaded, &slot) == HS_SESSION_OK);
    assert(slot == HS_SESSION_SLOT_A && loaded.sequence == 1);
    session.metrics.total_tried++;
    assert(hs_session_save_next(a, b, &session) == HS_SESSION_OK && session.sequence == 2);
    assert(hs_session_load_latest(a, b, &loaded, &slot) == HS_SESSION_OK);
    assert(slot == HS_SESSION_SLOT_B && loaded.metrics.total_tried == session.metrics.total_tried);

    hs_session_t different = session;
    different.session_id[0] ^= 0xff;
    assert(hs_session_save_next(a, b, &different) == HS_SESSION_MISMATCH);

    assert(truncate(b, 20) == 0); /* power-loss style partial newer slot */
    assert(hs_session_load_latest(a, b, &loaded, &slot) == HS_SESSION_OK);
    assert(slot == HS_SESSION_SLOT_A && loaded.sequence == 1);
    assert(hs_session_save_next(a, b, &session) == HS_SESSION_OK && session.sequence == 2);

    assert(hs_session_tombstone(a, b, session.session_id) == HS_SESSION_OK);
    assert(hs_session_load_latest(a, b, &loaded, &slot) == HS_SESSION_OK);
    assert(loaded.state == HS_SESSION_TOMBSTONE && loaded.sequence == 3);
    assert(!memcmp(loaded.session_id, session.session_id, sizeof(session.session_id)));
    assert(hs_session_save_next(a, b, &session) == HS_SESSION_MISMATCH);
    different.state = HS_SESSION_FINALIZING;
    assert(hs_session_save_next(a, b, &different) == HS_SESSION_MISMATCH);
    different.state = HS_SESSION_ACTIVE;
    assert(hs_session_save_next(a, b, &different) == HS_SESSION_OK);
    assert(different.sequence == 4);
    assert(hs_session_load_latest(a, b, &loaded, &slot) == HS_SESSION_OK);
    assert(loaded.state == HS_SESSION_ACTIVE && loaded.sequence == 4);
    assert(!memcmp(loaded.session_id, different.session_id, sizeof(different.session_id)));

    uint8_t wire[HS_SESSION_MAX_WIRE_BYTES];
    size_t length;
    fill_session(&session);
    session.sequence = UINT64_MAX;
    assert(hs_session_encode(&session, wire, sizeof(wire), &length) == HS_SESSION_OK);
    write_bytes(a, wire, length);
    unlink(b);
    assert(hs_session_save_next(a, b, &session) == HS_SESSION_RANGE);

    assert(unlink(a) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    round_trip_and_bounds();
    minimal_round_trip();
    corruption_and_tlv_rules();
    ab_store_and_tombstone();
    puts("hs_crack_session_test: PASS");
    return 0;
}
