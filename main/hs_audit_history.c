#define _POSIX_C_SOURCE 200809L

#include "hs_audit_history.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HS_AUDIT_HISTORY_LINE_MAX 4096U
#define HS_AUDIT_HISTORY_FIELD_COUNT (20U + HS_AUDIT_HISTORY_MAX_SELECTED_LISTS)

static bool text_terminated(const char *text, size_t capacity)
{
    return text && memchr(text, '\0', capacity) != NULL;
}

static bool valid_outcome(hs_session_outcome_t outcome)
{
    return outcome >= HS_SESSION_OUTCOME_NONE && outcome <= HS_SESSION_OUTCOME_ERROR;
}

static bool valid_record(const hs_audit_history_record_t *record)
{
    if (!record || record->schema_version != HS_AUDIT_HISTORY_SCHEMA_VERSION ||
        record->selected_list_count > HS_AUDIT_HISTORY_MAX_SELECTED_LISTS ||
        (int)record->origin_source < (int)HS_SESSION_SOURCE_LOCAL ||
        record->origin_source > HS_SESSION_SOURCE_MBUS ||
        record->requested_workers > HS_SESSION_MAX_WORKERS ||
        record->effective_workers > HS_SESSION_MAX_WORKERS ||
        !valid_outcome(record->outcome) ||
        !text_terminated(record->origin_local_path, sizeof(record->origin_local_path)) ||
        !text_terminated(record->origin_remote_path, sizeof(record->origin_remote_path))) {
        return false;
    }
    for (size_t i = 0; i < record->selected_list_count; ++i) {
        if (!text_terminated(record->selected_lists[i],
                             sizeof(record->selected_lists[i]))) return false;
    }
    return true;
}

static void copy_text(char *out, size_t capacity, const char *input)
{
    if (!capacity) return;
    size_t length = input ? strnlen(input, capacity - 1U) : 0;
    if (length) memcpy(out, input, length);
    out[length] = '\0';
}

static void session_id_hex(const uint8_t session_id[HS_SESSION_ID_BYTES], char out[33])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < HS_SESSION_ID_BYTES; ++i) {
        out[i * 2U] = digits[session_id[i] >> 4U];
        out[i * 2U + 1U] = digits[session_id[i] & 0x0fU];
    }
    out[32] = '\0';
}

static bool hex_session_id(const char *text, uint8_t out[HS_SESSION_ID_BYTES])
{
    if (!text || strlen(text) != 32U) return false;
    for (size_t i = 0; i < HS_SESSION_ID_BYTES; ++i) {
        unsigned value = 0;
        for (size_t nibble = 0; nibble < 2U; ++nibble) {
            unsigned char c = (unsigned char)text[i * 2U + nibble];
            if (c >= '0' && c <= '9') value = value * 16U + c - '0';
            else if (c >= 'a' && c <= 'f') value = value * 16U + c - 'a' + 10U;
            else if (c >= 'A' && c <= 'F') value = value * 16U + c - 'A' + 10U;
            else return false;
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

static hs_audit_history_result_t final_path(
    const char *directory, const hs_audit_history_record_t *record,
    char out[HS_AUDIT_HISTORY_PATH_MAX])
{
    char id_hex[33];
    session_id_hex(record->session_id, id_hex);
    int length = snprintf(out, HS_AUDIT_HISTORY_PATH_MAX,
                          "%s/episode-%s-%020" PRIu64 ".csv",
                          directory, id_hex, record->episode_sequence);
    return length >= 0 && (size_t)length < HS_AUDIT_HISTORY_PATH_MAX
               ? HS_AUDIT_HISTORY_OK : HS_AUDIT_HISTORY_RANGE;
}

static int csv_put(FILE *file, const char *value)
{
    bool quote = strpbrk(value, ",\"\r\n") != NULL;
    if (quote && fputc('"', file) == EOF) return -1;
    for (const char *p = value; *p; ++p) {
        if (*p == '"' && fputc('"', file) == EOF) return -1;
        if (fputc(*p, file) == EOF) return -1;
    }
    return quote && fputc('"', file) == EOF ? -1 : 0;
}

static int write_record(FILE *file, const hs_audit_history_record_t *record)
{
    char id_hex[33];
    session_id_hex(record->session_id, id_hex);
    if (fprintf(file, "%u,", (unsigned)HS_AUDIT_HISTORY_SCHEMA_VERSION) < 0 ||
        csv_put(file, id_hex) ||
        fprintf(file, ",%" PRIu64 ",%u,%" PRId64 ",%u,%" PRId64 ",%u,",
                record->episode_sequence, record->started_at_available ? 1U : 0U,
                record->started_at_unix_seconds, record->finished_at_available ? 1U : 0U,
                record->finished_at_unix_seconds, (unsigned)record->origin_source) < 0 ||
        csv_put(file, record->origin_local_path) || fputc(',', file) == EOF ||
        csv_put(file, record->origin_remote_path) ||
        fprintf(file, ",%zu", record->selected_list_count) < 0) {
        return -1;
    }
    for (size_t i = 0; i < HS_AUDIT_HISTORY_MAX_SELECTED_LISTS; ++i) {
        if (fputc(',', file) == EOF ||
            csv_put(file, i < record->selected_list_count ? record->selected_lists[i] : "")) {
            return -1;
        }
    }
    return fprintf(file, ",%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                         ",%" PRIu32 ",%u,%" PRId32 "\n",
                   (unsigned)record->requested_workers, (unsigned)record->effective_workers,
                   record->elapsed_time_ms, record->rate_milli_per_second,
                   record->last_eta_seconds, record->tried_count, record->resume_count,
                   (unsigned)record->outcome, record->reason) < 0 ? -1 : 0;
}

static size_t csv_split(char *line, char **fields, size_t capacity)
{
    size_t count = 0;
    char *read = line;
    char *write = line;
    while (*read && count < capacity) {
        fields[count++] = write;
        bool quoted = *read == '"';
        if (quoted) ++read;
        while (*read) {
            if (quoted && *read == '"') {
                if (read[1] == '"') {
                    *write++ = '"';
                    read += 2;
                } else {
                    ++read;
                    quoted = false;
                }
            } else if (!quoted && (*read == ',' || *read == '\r' || *read == '\n')) {
                break;
            } else {
                *write++ = *read++;
            }
        }
        if (quoted) return 0;
        if (*read && *read != ',' && *read != '\r' && *read != '\n') return 0;
        char delimiter = *read;
        *write++ = '\0';
        if (delimiter == ',') ++read;
        else {
            while (*read == '\r' || *read == '\n') ++read;
            if (*read) return 0;
            break;
        }
    }
    return *read == '\0' ? count : 0U;
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (!text || !*text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !end || *end) return false;
    *out = (uint64_t)value;
    return true;
}

static bool parse_i64(const char *text, int64_t *out)
{
    if (!text || !*text) return false;
    const unsigned char *p = (const unsigned char *)text;
    if (*p == '-') ++p;
    if (!*p) return false;
    for (; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno || !end || *end) return false;
    *out = (int64_t)value;
    return true;
}

static bool parse_u32(const char *text, uint32_t *out)
{
    uint64_t value;
    if (!parse_u64(text, &value) || value > UINT32_MAX) return false;
    *out = (uint32_t)value;
    return true;
}

static bool parse_bool(const char *text, bool *out)
{
    uint32_t value;
    if (!parse_u32(text, &value) || value > 1U) return false;
    *out = value != 0;
    return true;
}

static bool parse_record_line(char *line, hs_audit_history_record_t *out)
{
    char *fields[HS_AUDIT_HISTORY_FIELD_COUNT];
    size_t count = csv_split(line, fields, HS_AUDIT_HISTORY_FIELD_COUNT);
    uint32_t schema, source, requested, effective, resume_count, outcome;
    uint64_t sequence, selected_count, elapsed, rate, eta, tried;
    int64_t started, finished, reason;
    bool started_available, finished_available;
    if (count != HS_AUDIT_HISTORY_FIELD_COUNT ||
        !parse_u32(fields[0], &schema) || schema != HS_AUDIT_HISTORY_SCHEMA_VERSION ||
        !hex_session_id(fields[1], out->session_id) || !parse_u64(fields[2], &sequence) ||
        !parse_bool(fields[3], &started_available) || !parse_i64(fields[4], &started) ||
        !parse_bool(fields[5], &finished_available) || !parse_i64(fields[6], &finished) ||
        !parse_u32(fields[7], &source) || source > HS_SESSION_SOURCE_MBUS ||
        !parse_u64(fields[10], &selected_count) ||
        selected_count > HS_AUDIT_HISTORY_MAX_SELECTED_LISTS ||
        !parse_u32(fields[19], &requested) || requested > HS_SESSION_MAX_WORKERS ||
        !parse_u32(fields[20], &effective) || effective > HS_SESSION_MAX_WORKERS ||
        !parse_u64(fields[21], &elapsed) || !parse_u64(fields[22], &rate) ||
        !parse_u64(fields[23], &eta) || !parse_u64(fields[24], &tried) ||
        !parse_u32(fields[25], &resume_count) || !parse_u32(fields[26], &outcome) ||
        outcome > HS_SESSION_OUTCOME_ERROR || !parse_i64(fields[27], &reason) ||
        strlen(fields[8]) >= sizeof(out->origin_local_path) ||
        strlen(fields[9]) >= sizeof(out->origin_remote_path) ||
        reason < INT32_MIN || reason > INT32_MAX) {
        return false;
    }
    for (uint64_t i = 0; i < selected_count; ++i)
        if (strlen(fields[11U + i]) >= sizeof(out->selected_lists[0])) return false;
    memset(out, 0, sizeof(*out));
    if (!hex_session_id(fields[1], out->session_id)) return false;
    out->schema_version = (uint16_t)schema;
    out->episode_sequence = sequence;
    out->started_at_available = started_available;
    out->started_at_unix_seconds = started;
    out->finished_at_available = finished_available;
    out->finished_at_unix_seconds = finished;
    out->origin_source = (hs_session_source_t)source;
    copy_text(out->origin_local_path, sizeof(out->origin_local_path), fields[8]);
    copy_text(out->origin_remote_path, sizeof(out->origin_remote_path), fields[9]);
    out->selected_list_count = (size_t)selected_count;
    for (size_t i = 0; i < out->selected_list_count; ++i)
        copy_text(out->selected_lists[i], sizeof(out->selected_lists[i]), fields[11 + i]);
    out->requested_workers = (uint8_t)requested;
    out->effective_workers = (uint8_t)effective;
    out->elapsed_time_ms = elapsed;
    out->rate_milli_per_second = rate;
    out->last_eta_seconds = eta;
    out->tried_count = tried;
    out->resume_count = resume_count;
    out->outcome = (hs_session_outcome_t)outcome;
    out->reason = (int32_t)reason;
    return valid_record(out);
}

static bool same_record(const hs_audit_history_record_t *a,
                        const hs_audit_history_record_t *b)
{
    if (a->schema_version != b->schema_version ||
        memcmp(a->session_id, b->session_id, HS_SESSION_ID_BYTES) != 0 ||
        a->episode_sequence != b->episode_sequence ||
        a->started_at_available != b->started_at_available ||
        a->started_at_unix_seconds != b->started_at_unix_seconds ||
        a->finished_at_available != b->finished_at_available ||
        a->finished_at_unix_seconds != b->finished_at_unix_seconds ||
        a->origin_source != b->origin_source ||
        strcmp(a->origin_local_path, b->origin_local_path) != 0 ||
        strcmp(a->origin_remote_path, b->origin_remote_path) != 0 ||
        a->selected_list_count != b->selected_list_count ||
        a->requested_workers != b->requested_workers ||
        a->effective_workers != b->effective_workers ||
        a->elapsed_time_ms != b->elapsed_time_ms ||
        a->rate_milli_per_second != b->rate_milli_per_second ||
        a->last_eta_seconds != b->last_eta_seconds ||
        a->tried_count != b->tried_count || a->resume_count != b->resume_count ||
        a->outcome != b->outcome || a->reason != b->reason) return false;
    for (size_t i = 0; i < a->selected_list_count; ++i)
        if (strcmp(a->selected_lists[i], b->selected_lists[i]) != 0) return false;
    return true;
}

static bool read_record_file(const char *path, hs_audit_history_record_t *record)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char line[HS_AUDIT_HISTORY_LINE_MAX];
    size_t got = fread(line, 1, sizeof(line) - 1U, file);
    bool embedded_nul = memchr(line, '\0', got) != NULL;
    bool extra = fgetc(file) != EOF;
    bool io_error = ferror(file);
    bool closed = fclose(file) == 0;
    line[got] = '\0';
    return got > 0U && !embedded_nul && !extra && !io_error && closed &&
           parse_record_line(line, record);
}

hs_audit_history_result_t hs_audit_history_from_session(
    const hs_session_t *session, uint64_t episode_sequence,
    bool started_at_available, int64_t started_at_unix_seconds,
    bool finished_at_available, int64_t finished_at_unix_seconds,
    hs_audit_history_record_t *out)
{
    if (!session || !out || session->schema_version != HS_SESSION_SCHEMA_VERSION ||
        session->wordlist_count > HS_SESSION_MAX_WORDLISTS ||
        session->worker_count > HS_SESSION_MAX_WORKERS ||
        !text_terminated(session->origin.local_path, sizeof(session->origin.local_path)) ||
        !text_terminated(session->origin.remote_path, sizeof(session->origin.remote_path)))
        return HS_AUDIT_HISTORY_INVALID;
    for (size_t i = 0; i < session->wordlist_count; ++i)
        if (!text_terminated(session->wordlists[i].path, sizeof(session->wordlists[i].path)))
            return HS_AUDIT_HISTORY_INVALID;
    memset(out, 0, sizeof(*out));
    out->schema_version = HS_AUDIT_HISTORY_SCHEMA_VERSION;
    memcpy(out->session_id, session->session_id, sizeof(out->session_id));
    out->episode_sequence = episode_sequence;
    out->started_at_available = started_at_available;
    out->started_at_unix_seconds = started_at_unix_seconds;
    out->finished_at_available = finished_at_available;
    out->finished_at_unix_seconds = finished_at_unix_seconds;
    out->origin_source = session->origin.source;
    copy_text(out->origin_local_path, sizeof(out->origin_local_path), session->origin.local_path);
    copy_text(out->origin_remote_path, sizeof(out->origin_remote_path), session->origin.remote_path);
    out->selected_list_count = session->wordlist_count;
    for (size_t i = 0; i < out->selected_list_count; ++i)
        copy_text(out->selected_lists[i], sizeof(out->selected_lists[i]), session->wordlists[i].path);
    out->requested_workers = session->config.requested_workers;
    out->effective_workers = (uint8_t)session->worker_count;
    out->elapsed_time_ms = session->metrics.active_time_ms;
    out->rate_milli_per_second = session->metrics.last_rate_milli_per_second;
    out->last_eta_seconds = session->metrics.last_eta_seconds;
    out->tried_count = session->metrics.total_tried;
    out->resume_count = session->metrics.resume_count;
    out->outcome = session->has_result ? session->result.outcome : HS_SESSION_OUTCOME_INTERRUPTED;
    out->reason = session->has_result ? session->result.reason : 0;
    return valid_record(out) ? HS_AUDIT_HISTORY_OK : HS_AUDIT_HISTORY_INVALID;
}

hs_audit_history_result_t hs_audit_history_append(
    const char *directory, const hs_audit_history_record_t *record)
{
    if (!directory || !*directory || !valid_record(record)) return HS_AUDIT_HISTORY_INVALID;
    char final[HS_AUDIT_HISTORY_PATH_MAX];
    hs_audit_history_result_t path_result = final_path(directory, record, final);
    if (path_result != HS_AUDIT_HISTORY_OK) return path_result;
    if (access(final, F_OK) == 0) {
        hs_audit_history_record_t existing;
        return read_record_file(final, &existing) && same_record(&existing, record)
                   ? HS_AUDIT_HISTORY_OK : HS_AUDIT_HISTORY_INVALID;
    }
    if (errno != ENOENT) return HS_AUDIT_HISTORY_IO_ERROR;

    char temp[HS_AUDIT_HISTORY_PATH_MAX];
    int length = snprintf(temp, sizeof(temp), "%s.tmp.XXXXXX", final);
    if (length < 0 || (size_t)length >= sizeof(temp)) return HS_AUDIT_HISTORY_RANGE;
    int descriptor = mkstemp(temp);
    if (descriptor < 0) return HS_AUDIT_HISTORY_IO_ERROR;
    FILE *file = fdopen(descriptor, "wb");
    bool ok = file != NULL;
    if (file) {
        if (write_record(file, record) != 0 || fflush(file) != 0 ||
            fsync(descriptor) != 0) ok = false;
        if (fclose(file) != 0) ok = false;
    } else {
        close(descriptor);
    }
    if (!ok) {
        (void)remove(temp);
        return HS_AUDIT_HISTORY_IO_ERROR;
    }
    if (rename(temp, final) != 0) {
        int rename_error = errno;
        (void)remove(temp);
        if (rename_error == EEXIST) {
            hs_audit_history_record_t existing;
            return read_record_file(final, &existing) && same_record(&existing, record)
                       ? HS_AUDIT_HISTORY_OK : HS_AUDIT_HISTORY_INVALID;
        }
        return HS_AUDIT_HISTORY_IO_ERROR;
    }
    return HS_AUDIT_HISTORY_OK;
}

static bool history_filename(const char *name)
{
    size_t length = strlen(name);
    return length > 12U && strncmp(name, "episode-", 8U) == 0 &&
           strcmp(name + length - 4U, ".csv") == 0;
}

static int newest_first(const void *left, const void *right)
{
    const hs_audit_history_record_t *a = left;
    const hs_audit_history_record_t *b = right;
    if (a->episode_sequence != b->episode_sequence)
        return a->episode_sequence > b->episode_sequence ? -1 : 1;
    return memcmp(b->session_id, a->session_id, HS_SESSION_ID_BYTES);
}

hs_audit_history_result_t hs_audit_history_list(
    const char *directory, hs_audit_history_record_t *out, size_t capacity,
    size_t *count_out)
{
    if (!directory || !*directory || !count_out || (capacity && !out))
        return HS_AUDIT_HISTORY_INVALID;
    *count_out = 0;
    DIR *dir = opendir(directory);
    if (!dir) return errno == ENOENT ? HS_AUDIT_HISTORY_NOT_FOUND : HS_AUDIT_HISTORY_IO_ERROR;

    size_t count = 0;
    bool read_error = false;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            read_error = errno != 0;
            break;
        }
        if (!history_filename(entry->d_name)) continue;
        char path[HS_AUDIT_HISTORY_PATH_MAX];
        int length = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) continue;
        hs_audit_history_record_t parsed;
        if (!read_record_file(path, &parsed) || capacity == 0U) continue;
        size_t position = 0U;
        while (position < count && newest_first(&out[position], &parsed) <= 0)
            ++position;
        if (position >= capacity) continue;
        size_t previous_count = count;
        if (count < capacity) ++count;
        size_t move_count = previous_count < capacity ? previous_count - position
                                                      : capacity - position - 1U;
        if (move_count > 0U)
            memmove(&out[position + 1U], &out[position], move_count * sizeof(*out));
        out[position] = parsed;
    }
    closedir(dir);
    if (read_error) {
        return HS_AUDIT_HISTORY_IO_ERROR;
    }
    *count_out = count;
    return HS_AUDIT_HISTORY_OK;
}
