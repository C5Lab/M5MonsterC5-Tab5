#define _POSIX_C_SOURCE 200809L
#include "hs_capture_validation.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HS_VALIDATION_SCHEMA_VERSION 1U
#define HS_VALIDATION_ENCODED_SIZE 68U

static const uint8_t s_magic[4] = {'H', 'S', 'V', '1'};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc;
}

static void put_u32(uint8_t **cursor, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) *(*cursor)++ = (uint8_t)(value >> (i * 8U));
}

static void put_u64(uint8_t **cursor, uint64_t value)
{
    for (unsigned i = 0; i < 8U; ++i) *(*cursor)++ = (uint8_t)(value >> (i * 8U));
}

static uint32_t get_u32(const uint8_t **cursor)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4U; ++i) value |= (uint32_t)*(*cursor)++ << (i * 8U);
    return value;
}

static uint64_t get_u64(const uint8_t **cursor)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8U; ++i) value |= (uint64_t)*(*cursor)++ << (i * 8U);
    return value;
}

bool hs_capture_validation_sidecar_path(const char *capture_path,
                                        char *sidecar_path,
                                        size_t capacity)
{
    if (!capture_path || !capture_path[0] || !sidecar_path || capacity == 0U)
        return false;
    const char *slash = strrchr(capture_path, '/');
    const char *backslash = strrchr(capture_path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    const char *base = slash ? slash + 1 : capture_path;
    if (!base[0]) return false;
    size_t prefix = (size_t)(base - capture_path);
    int written = snprintf(sidecar_path, capacity, "%.*s.%s.audit",
                           (int)prefix, capture_path, base);
    return written > 0 && (size_t)written < capacity;
}

static bool identity_valid(const hs_capture_validation_identity_t *identity)
{
    return identity && identity->validator_version != 0U &&
           (identity->format == HS_CAPTURE_FILE_PCAP ||
            identity->format == HS_CAPTURE_FILE_HCCAPX);
}

static bool report_valid(const hs_capture_report_t *report)
{
    return report && report->state <= HS_CAPTURE_CANCELLED &&
           report->reason <= HS_CAPTURE_REASON_UNSUPPORTED_FORMAT;
}

static bool identity_equal(const hs_capture_validation_identity_t *left,
                           const hs_capture_validation_identity_t *right)
{
    return left->size == right->size && left->crc32 == right->crc32 &&
           left->validator_version == right->validator_version &&
           left->format == right->format;
}

static bool encode(const hs_capture_validation_entry_t *entry,
                   uint8_t bytes[HS_VALIDATION_ENCODED_SIZE])
{
    if (!entry || !identity_valid(&entry->identity) ||
        !report_valid(&entry->report)) return false;
    memset(bytes, 0, HS_VALIDATION_ENCODED_SIZE);
    uint8_t *cursor = bytes;
    memcpy(cursor, s_magic, sizeof(s_magic));
    cursor += sizeof(s_magic);
    put_u32(&cursor, HS_VALIDATION_SCHEMA_VERSION);
    put_u64(&cursor, entry->identity.size);
    put_u32(&cursor, entry->identity.crc32);
    put_u32(&cursor, entry->identity.validator_version);
    put_u32(&cursor, (uint32_t)entry->identity.format);
    put_u32(&cursor, (uint32_t)entry->report.state);
    put_u32(&cursor, (uint32_t)entry->report.reason);
    put_u32(&cursor, entry->report.packet_count);
    put_u32(&cursor, entry->report.eapol_count);
    put_u32(&cursor, entry->report.ap_nonce_count);
    put_u32(&cursor, entry->report.sta_response_count);
    put_u32(&cursor, entry->report.malformed_count);
    put_u32(&cursor, entry->report.unsupported_keyver_count);
    put_u32(&cursor, entry->report.record_count);
    uint32_t crc = crc32_update(UINT32_MAX, bytes,
                                HS_VALIDATION_ENCODED_SIZE - 4U) ^ UINT32_MAX;
    put_u32(&cursor, crc);
    return (size_t)(cursor - bytes) == HS_VALIDATION_ENCODED_SIZE;
}

static bool decode(const uint8_t bytes[HS_VALIDATION_ENCODED_SIZE],
                   hs_capture_validation_entry_t *entry)
{
    if (memcmp(bytes, s_magic, sizeof(s_magic)) != 0) return false;
    uint32_t stored_crc = 0;
    const uint8_t *crc_cursor = bytes + HS_VALIDATION_ENCODED_SIZE - 4U;
    stored_crc = get_u32(&crc_cursor);
    uint32_t actual_crc = crc32_update(UINT32_MAX, bytes,
                                       HS_VALIDATION_ENCODED_SIZE - 4U) ^ UINT32_MAX;
    if (stored_crc != actual_crc) return false;

    memset(entry, 0, sizeof(*entry));
    const uint8_t *cursor = bytes + sizeof(s_magic);
    if (get_u32(&cursor) != HS_VALIDATION_SCHEMA_VERSION) return false;
    entry->identity.size = get_u64(&cursor);
    entry->identity.crc32 = get_u32(&cursor);
    entry->identity.validator_version = get_u32(&cursor);
    entry->identity.format = (hs_capture_file_format_t)get_u32(&cursor);
    entry->report.state = (hs_capture_state_t)get_u32(&cursor);
    entry->report.reason = (hs_capture_reason_t)get_u32(&cursor);
    entry->report.packet_count = get_u32(&cursor);
    entry->report.eapol_count = get_u32(&cursor);
    entry->report.ap_nonce_count = get_u32(&cursor);
    entry->report.sta_response_count = get_u32(&cursor);
    entry->report.malformed_count = get_u32(&cursor);
    entry->report.unsupported_keyver_count = get_u32(&cursor);
    entry->report.record_count = get_u32(&cursor);
    return identity_valid(&entry->identity) && report_valid(&entry->report);
}

hs_capture_validation_result_t hs_capture_validation_save(
    const char *capture_path, const hs_capture_validation_entry_t *entry)
{
    char sidecar[384];
    char temporary[392];
    char backup[392];
    uint8_t bytes[HS_VALIDATION_ENCODED_SIZE];
    if (!hs_capture_validation_sidecar_path(capture_path, sidecar,
                                            sizeof(sidecar)) ||
        !encode(entry, bytes)) return HS_CAPTURE_VALIDATION_INVALID_ARGUMENT;
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", sidecar);
    if (written <= 0 || (size_t)written >= sizeof(temporary))
        return HS_CAPTURE_VALIDATION_INVALID_ARGUMENT;
    written = snprintf(backup, sizeof(backup), "%s.bak", sidecar);
    if (written <= 0 || (size_t)written >= sizeof(backup))
        return HS_CAPTURE_VALIDATION_INVALID_ARGUMENT;

    FILE *file = fopen(temporary, "wb");
    if (!file) return HS_CAPTURE_VALIDATION_IO_ERROR;
    bool ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) &&
              fflush(file) == 0;
    int fd = fileno(file);
    if (ok && fd >= 0) ok = fsync(fd) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        unlink(temporary);
        return HS_CAPTURE_VALIDATION_IO_ERROR;
    }
    (void)unlink(backup);
    bool had_previous = rename(sidecar, backup) == 0;
    if (!had_previous && errno != ENOENT) {
        unlink(temporary);
        return HS_CAPTURE_VALIDATION_IO_ERROR;
    }
    if (rename(temporary, sidecar) != 0) {
        if (had_previous) (void)rename(backup, sidecar);
        unlink(temporary);
        return HS_CAPTURE_VALIDATION_IO_ERROR;
    }
    if (had_previous) (void)unlink(backup);
    return HS_CAPTURE_VALIDATION_OK;
}

hs_capture_validation_result_t hs_capture_validation_load(
    const char *capture_path,
    const hs_capture_validation_identity_t *expected_identity,
    hs_capture_validation_entry_t *entry_out)
{
    char sidecar[384];
    uint8_t bytes[HS_VALIDATION_ENCODED_SIZE];
    if (!expected_identity || !entry_out ||
        !hs_capture_validation_sidecar_path(capture_path, sidecar,
                                            sizeof(sidecar)))
        return HS_CAPTURE_VALIDATION_INVALID_ARGUMENT;
    FILE *file = fopen(sidecar, "rb");
    if (!file) return HS_CAPTURE_VALIDATION_NOT_FOUND;
    size_t got = fread(bytes, 1, sizeof(bytes), file);
    bool extra = fgetc(file) != EOF;
    bool read_error = ferror(file) != 0;
    fclose(file);
    if (read_error) return HS_CAPTURE_VALIDATION_IO_ERROR;
    if (got != sizeof(bytes) || extra || !decode(bytes, entry_out))
        return HS_CAPTURE_VALIDATION_CORRUPT;
    if (!identity_equal(&entry_out->identity, expected_identity))
        return HS_CAPTURE_VALIDATION_STALE;
    return HS_CAPTURE_VALIDATION_OK;
}
