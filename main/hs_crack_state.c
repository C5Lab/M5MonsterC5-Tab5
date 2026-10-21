#define _POSIX_C_SOURCE 200809L
#include "hs_crack_state.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STATE_FILENAME_MAX 96U
#define STATE_SSID_MAX 33U
#define STATE_RESULT_MAX 16U
#define STATE_PASSWORD_MAX 65U
#define STATE_RECORD_LIMIT 65536U

typedef struct {
    char filename[STATE_FILENAME_MAX];
    char ssid[STATE_SSID_MAX];
    int64_t size;
    char generic[STATE_RESULT_MAX];
    char wordlist[STATE_RESULT_MAX];
    char password[STATE_PASSWORD_MAX];
} state_row_t;

static bool copy_value(char *destination, size_t capacity, const char *value)
{
    if (!destination || !capacity || !value) return false;
    size_t length = strlen(value);
    if (length >= capacity) return false;
    memcpy(destination, value, length + 1U);
    return true;
}

static int read_record(FILE *file, char **buffer, size_t *capacity,
                       size_t *length_out)
{
    if (!file || !buffer || !capacity || !length_out) return -1;
    if (!*buffer) {
        *capacity = 512U;
        *buffer = malloc(*capacity);
        if (!*buffer) return -1;
    }

    size_t length = 0U;
    bool in_quotes = false;
    bool field_start = true;
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (length + 2U > *capacity) {
            if (*capacity >= STATE_RECORD_LIMIT) return -1;
            size_t next = *capacity * 2U;
            if (next > STATE_RECORD_LIMIT) next = STATE_RECORD_LIMIT;
            char *grown = realloc(*buffer, next);
            if (!grown) return -1;
            *buffer = grown;
            *capacity = next;
        }
        (*buffer)[length++] = (char)c;

        if (c == '"') {
            if (!in_quotes && field_start) {
                in_quotes = true;
            } else if (in_quotes) {
                int next = fgetc(file);
                if (next == '"') {
                    if (length + 2U > *capacity) {
                        if (*capacity >= STATE_RECORD_LIMIT) return -1;
                        size_t grown_capacity = *capacity * 2U;
                        if (grown_capacity > STATE_RECORD_LIMIT)
                            grown_capacity = STATE_RECORD_LIMIT;
                        char *grown = realloc(*buffer, grown_capacity);
                        if (!grown) return -1;
                        *buffer = grown;
                        *capacity = grown_capacity;
                    }
                    (*buffer)[length++] = (char)next;
                    field_start = false;
                    continue;
                }
                in_quotes = false;
                if (next != EOF) ungetc(next, file);
            }
        }

        if (!in_quotes) {
            if (c == ',') field_start = true;
            else if (c == '\n') break;
            else if (c != '\r') field_start = false;
        }
    }
    if (ferror(file)) return -1;
    if (length == 0U && c == EOF) return 0;
    (*buffer)[length] = '\0';
    *length_out = length;
    return 1;
}

static bool parse_field(const char **cursor, char *output, size_t capacity,
                        bool last)
{
    if (!cursor || !*cursor || !output || !capacity) return false;
    const char *p = *cursor;
    size_t used = 0U;
    bool quoted = *p == '"';
    if (quoted) ++p;

    while (*p) {
        if (quoted) {
            if (*p == '"') {
                if (p[1] == '"') {
                    if (used + 1U >= capacity) return false;
                    output[used++] = '"';
                    p += 2;
                    continue;
                }
                ++p;
                quoted = false;
                break;
            }
        } else if (*p == ',' || *p == '\r' || *p == '\n') {
            break;
        }
        if (used + 1U >= capacity) return false;
        output[used++] = *p++;
    }
    if (quoted) return false;
    output[used] = '\0';

    if (*p == ',') {
        if (last) return false;
        *cursor = p + 1;
        return true;
    }
    if (!last) return false;
    while (*p == '\r' || *p == '\n') ++p;
    if (*p != '\0') return false;
    *cursor = p;
    return true;
}

static bool parse_row(const char *record, state_row_t *row)
{
    if (!record || !row) return false;
    memset(row, 0, sizeof(*row));
    char size_text[32];
    const char *cursor = record;
    if (!parse_field(&cursor, row->filename, sizeof(row->filename), false) ||
        !parse_field(&cursor, row->ssid, sizeof(row->ssid), false) ||
        !parse_field(&cursor, size_text, sizeof(size_text), false) ||
        !parse_field(&cursor, row->generic, sizeof(row->generic), false) ||
        !parse_field(&cursor, row->wordlist, sizeof(row->wordlist), false) ||
        !parse_field(&cursor, row->password, sizeof(row->password), true))
        return false;

    char *end = NULL;
    errno = 0;
    int64_t parsed = strtoll(size_text, &end, 10);
    if (errno || !end || *end || parsed < 0) return false;
    row->size = parsed;
    return row->filename[0] != '\0';
}

static bool put_field(FILE *file, const char *value)
{
    if (!file || fputc('"', file) == EOF) return false;
    for (const char *p = value ? value : ""; *p; ++p) {
        if (*p == '"' && fputc('"', file) == EOF) return false;
        if (fputc((unsigned char)*p, file) == EOF) return false;
    }
    return fputc('"', file) != EOF;
}

static bool write_row(FILE *file, const state_row_t *row)
{
    return put_field(file, row->filename) && fputc(',', file) != EOF &&
           put_field(file, row->ssid) &&
           fprintf(file, ",%" PRId64 ",", row->size) > 0 &&
           put_field(file, row->generic[0] ? row->generic : "none") &&
           fputc(',', file) != EOF &&
           put_field(file, row->wordlist[0] ? row->wordlist : "none") &&
           fputc(',', file) != EOF && put_field(file, row->password) &&
           fputc('\n', file) != EOF;
}

static bool make_backup_path(const char *csv_path, char *backup,
                             size_t capacity)
{
    int written = snprintf(backup, capacity, "%s.bak", csv_path);
    return written >= 0 && (size_t)written < capacity;
}

static bool publish_file(const char *csv_path, const char *temporary_path)
{
    if (rename(temporary_path, csv_path) == 0) return true;

    char backup[512];
    if (!make_backup_path(csv_path, backup, sizeof(backup))) return false;
    (void)unlink(backup);
    if (rename(csv_path, backup) != 0 && errno != ENOENT) return false;
    if (rename(temporary_path, csv_path) == 0) {
        (void)unlink(backup);
        return true;
    }
    (void)rename(backup, csv_path);
    return false;
}

bool hs_crack_state_upsert_file(
    const char *csv_path, const char *temporary_path,
    const char *filename, const char *ssid, int64_t size,
    const char *generic, const char *wordlist, const char *password)
{
    if (!csv_path || !csv_path[0] || !temporary_path || !temporary_path[0] ||
        strcmp(csv_path, temporary_path) == 0 || !filename || !filename[0])
        return false;

    char backup[512];
    if (!make_backup_path(csv_path, backup, sizeof(backup))) return false;
    if (access(csv_path, F_OK) != 0 && access(backup, F_OK) == 0) {
        if (rename(backup, csv_path) != 0) return false;
    } else if (access(csv_path, F_OK) == 0) {
        /* A complete primary file wins over a stale publish backup. */
        (void)unlink(backup);
    }

    state_row_t merged = { .size = 0 };
    if (!copy_value(merged.filename, sizeof(merged.filename), filename) ||
        !copy_value(merged.generic, sizeof(merged.generic), "none") ||
        !copy_value(merged.wordlist, sizeof(merged.wordlist), "none"))
        return false;

    FILE *input = fopen(csv_path, "rb");
    if (!input && errno != ENOENT) return false;
    (void)unlink(temporary_path);
    FILE *output = fopen(temporary_path, "wb");
    if (!output) {
        if (input) fclose(input);
        return false;
    }

    bool ok = true;
    bool found = false;
    bool output_ends_in_newline = true;
    char *record = NULL;
    size_t record_capacity = 0U;
    if (input) {
        size_t record_length = 0U;
        int read_result;
        while ((read_result = read_record(input, &record, &record_capacity,
                                          &record_length)) > 0) {
            state_row_t row;
            if (parse_row(record, &row)) {
                if (strcmp(row.filename, filename) == 0) {
                    merged = row;
                    found = true;
                    continue;
                }
                ok = write_row(output, &row);
                output_ends_in_newline = true;
            } else {
                ok = fwrite(record, 1, record_length, output) == record_length;
                output_ends_in_newline = record_length > 0U &&
                                         record[record_length - 1U] == '\n';
            }
            if (!ok) break;
        }
        if (read_result < 0) ok = false;
        if (fclose(input) != 0) ok = false;
    }
    free(record);

    if (ok && !output_ends_in_newline) ok = fputc('\n', output) != EOF;
    if (ok && ssid) ok = copy_value(merged.ssid, sizeof(merged.ssid), ssid);
    if (ok && size > 0) merged.size = size;
    if (ok && generic)
        ok = copy_value(merged.generic, sizeof(merged.generic), generic);
    if (ok && wordlist)
        ok = copy_value(merged.wordlist, sizeof(merged.wordlist), wordlist);
    if (ok && password)
        ok = copy_value(merged.password, sizeof(merged.password), password);
    if (ok && !found && !ssid)
        merged.ssid[0] = '\0';
    if (ok) ok = write_row(output, &merged);
    if (ok) ok = fflush(output) == 0;
    if (ok) ok = fsync(fileno(output)) == 0;
    if (fclose(output) != 0) ok = false;
    if (!ok) {
        (void)unlink(temporary_path);
        return false;
    }
    if (!publish_file(csv_path, temporary_path)) {
        (void)unlink(temporary_path);
        return false;
    }
    return true;
}
