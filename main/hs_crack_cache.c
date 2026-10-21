#include "hs_crack_cache.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define HS_CACHE_IO_CHUNK 4096U
#define HS_CACHE_LINE_MAX 768U
#define HS_CACHE_FIELD_MAX 20U

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

uint32_t hs_crack_cache_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static hs_crack_cache_result_t file_crc32_checked(
    FILE *file, hs_crack_cache_checkpoint_fn checkpoint, void *context,
    uint32_t *out)
{
    uint8_t buffer[HS_CACHE_IO_CHUNK];
    uint32_t crc = 0;
    size_t since_checkpoint = 0;
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        crc = hs_crack_cache_crc32_update(crc, buffer, got);
        since_checkpoint += got;
        if (checkpoint && since_checkpoint >= 256U * 1024U) {
            since_checkpoint = 0;
            if (!checkpoint(context)) return HS_CRACK_CACHE_IO_ERROR;
        }
    }
    if (ferror(file)) return HS_CRACK_CACHE_IO_ERROR;
    if (checkpoint && since_checkpoint > 0 && !checkpoint(context))
        return HS_CRACK_CACHE_IO_ERROR;
    *out = crc;
    return HS_CRACK_CACHE_OK;
}

static hs_crack_cache_result_t file_crc32(FILE *file, uint32_t *out)
{
    return file_crc32_checked(file, NULL, NULL, out);
}

hs_crack_cache_result_t hs_crack_cache_file_crc32(
    const char *path, uint32_t *out)
{
    if (!path || !out) return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return HS_CRACK_CACHE_IO_ERROR;
    hs_crack_cache_result_t result = file_crc32(file, out);
    fclose(file);
    return result;
}

hs_crack_cache_result_t hs_crack_cache_file_crc32_checked(
    const char *path, hs_crack_cache_checkpoint_fn checkpoint,
    void *context, uint32_t *out)
{
    if (!path || !out) return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return HS_CRACK_CACHE_IO_ERROR;
    hs_crack_cache_result_t result =
        file_crc32_checked(file, checkpoint, context, out);
    fclose(file);
    return result;
}

hs_crack_cache_result_t hs_crack_cache_capture_id(
    const char *path, const char *ssid, const char *bssid,
    hs_crack_capture_id_t *out)
{
    if (!path || !out) return HS_CRACK_CACHE_INVALID;
    struct stat st;
    if (stat(path, &st) != 0) return HS_CRACK_CACHE_IO_ERROR;
    FILE *file = fopen(path, "rb");
    if (!file) return HS_CRACK_CACHE_IO_ERROR;
    uint32_t crc = 0;
    hs_crack_cache_result_t result = file_crc32(file, &crc);
    fclose(file);
    if (result != HS_CRACK_CACHE_OK) return result;

    memset(out, 0, sizeof(*out));
    out->size = (uint64_t)st.st_size;
    out->crc32 = crc;
    copy_text(out->ssid, sizeof(out->ssid), ssid);
    copy_text(out->bssid, sizeof(out->bssid), bssid);
    return HS_CRACK_CACHE_OK;
}

hs_crack_cache_result_t hs_crack_cache_wordlist_id(
    const char *path, hs_crack_wordlist_id_t *out)
{
    if (!path || !out) return HS_CRACK_CACHE_INVALID;
    struct stat st;
    if (stat(path, &st) != 0) return HS_CRACK_CACHE_IO_ERROR;
    FILE *file = fopen(path, "rb");
    if (!file) return HS_CRACK_CACHE_IO_ERROR;

    uint8_t buffer[HS_CACHE_IO_CHUNK];
    size_t got = fread(buffer, 1, sizeof(buffer), file);
    if (ferror(file)) {
        fclose(file);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    uint32_t head = hs_crack_cache_crc32_update(0, buffer, got);

    uint64_t size = (uint64_t)st.st_size;
    uint64_t tail_start = size > sizeof(buffer) ? size - sizeof(buffer) : 0;
    if (fseek(file, (long)tail_start, SEEK_SET) != 0) {
        fclose(file);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    got = fread(buffer, 1, sizeof(buffer), file);
    if (ferror(file)) {
        fclose(file);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    uint32_t tail = hs_crack_cache_crc32_update(0, buffer, got);
    fclose(file);

    memset(out, 0, sizeof(*out));
    copy_text(out->path, sizeof(out->path), path);
    out->size = size;
    out->mtime = (int64_t)st.st_mtime;
    out->head_crc32 = head;
    out->tail_crc32 = tail;
    return HS_CRACK_CACHE_OK;
}

bool hs_crack_cache_capture_equal(
    const hs_crack_capture_id_t *a, const hs_crack_capture_id_t *b)
{
    return a && b && a->size == b->size && a->crc32 == b->crc32 &&
           strcmp(a->ssid, b->ssid) == 0 && strcmp(a->bssid, b->bssid) == 0;
}

bool hs_crack_cache_wordlist_equal(
    const hs_crack_wordlist_id_t *a, const hs_crack_wordlist_id_t *b)
{
    return a && b && a->size == b->size && a->mtime == b->mtime &&
           a->head_crc32 == b->head_crc32 && a->tail_crc32 == b->tail_crc32 &&
           strcmp(a->path, b->path) == 0;
}

static int csv_put(FILE *file, const char *value)
{
    if (!value) value = "";
    bool quote = strpbrk(value, ",\"\r\n") != NULL;
    if (quote && fputc('"', file) == EOF) return -1;
    for (const char *p = value; *p; ++p) {
        if (*p == '"' && fputc('"', file) == EOF) return -1;
        if (fputc(*p, file) == EOF) return -1;
    }
    if (quote && fputc('"', file) == EOF) return -1;
    return 0;
}

static size_t csv_split(char *line, char **fields, size_t capacity)
{
    size_t count = 0;
    char *read = line;
    char *write = line;
    while (*read && count < capacity) {
        fields[count++] = write;
        bool quoted = false;
        if (*read == '"') {
            quoted = true;
            ++read;
        }
        while (*read) {
            if (quoted && *read == '"') {
                if (read[1] == '"') {
                    *write++ = '"';
                    read += 2;
                    continue;
                }
                ++read;
                quoted = false;
                continue;
            }
            if (!quoted && (*read == ',' || *read == '\r' || *read == '\n')) break;
            *write++ = *read++;
        }
        char delimiter = *read;
        *write++ = '\0';
        if (delimiter == ',') ++read;
        else {
            while (*read == '\r' || *read == '\n') ++read;
            break;
        }
    }
    return count;
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (!text || !*text) return false;
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

static bool parse_full_crc_line(char *line, hs_crack_wordlist_id_t *wordlist,
                                uint32_t *full_crc32)
{
    char *f[8];
    size_t n = csv_split(line, f, sizeof(f) / sizeof(f[0]));
    uint32_t schema;
    if (n != 7 || !parse_u32(f[0], &schema) ||
        schema != HS_CRACK_CACHE_SCHEMA_VERSION ||
        !parse_u64(f[2], &wordlist->size) ||
        !parse_i64(f[3], &wordlist->mtime) ||
        !parse_u32(f[4], &wordlist->head_crc32) ||
        !parse_u32(f[5], &wordlist->tail_crc32) ||
        !parse_u32(f[6], full_crc32)) {
        return false;
    }
    copy_text(wordlist->path, sizeof(wordlist->path), f[1]);
    return true;
}

hs_crack_cache_result_t hs_crack_cache_find_full_crc(
    const char *cache_path, const hs_crack_wordlist_id_t *wordlist,
    uint32_t *out)
{
    if (!cache_path || !wordlist || !out) return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(cache_path, "rb");
    if (!file) return errno == ENOENT ? HS_CRACK_CACHE_NOT_FOUND
                                      : HS_CRACK_CACHE_IO_ERROR;
    char line[HS_CACHE_LINE_MAX];
    hs_crack_cache_result_t result = HS_CRACK_CACHE_NOT_FOUND;
    while (fgets(line, sizeof(line), file)) {
        hs_crack_wordlist_id_t parsed = {0};
        uint32_t crc32 = 0;
        if (parse_full_crc_line(line, &parsed, &crc32) &&
            hs_crack_cache_wordlist_equal(wordlist, &parsed)) {
            *out = crc32;
            result = HS_CRACK_CACHE_OK;
        }
    }
    if (ferror(file)) result = HS_CRACK_CACHE_IO_ERROR;
    fclose(file);
    return result;
}

static bool parse_attempt_line(char *line, hs_crack_attempt_t *out)
{
    char *f[HS_CACHE_FIELD_MAX];
    size_t n = csv_split(line, f, HS_CACHE_FIELD_MAX);
    uint32_t schema, status, generic_version;
    uint64_t generic;
    if (n != 16 || !parse_u32(f[0], &schema) ||
        schema != HS_CRACK_CACHE_SCHEMA_VERSION ||
        !parse_u32(f[1], &out->capture.crc32) ||
        !parse_u64(f[2], &out->capture.size) ||
        !parse_u64(f[5], &generic) || generic > 1 ||
        !parse_u32(f[11], &generic_version) ||
        !parse_u32(f[12], &status) || status > HS_CRACK_ATTEMPT_ERROR ||
        !parse_u64(f[13], &out->tried) ||
        !parse_u32(f[15], &out->match_index)) {
        return false;
    }
    copy_text(out->capture.ssid, sizeof(out->capture.ssid), f[3]);
    copy_text(out->capture.bssid, sizeof(out->capture.bssid), f[4]);
    out->generic = generic != 0;
    copy_text(out->wordlist.path, sizeof(out->wordlist.path), f[6]);
    if (!parse_u64(f[7], &out->wordlist.size) ||
        !parse_i64(f[8], &out->wordlist.mtime) ||
        !parse_u32(f[9], &out->wordlist.head_crc32) ||
        !parse_u32(f[10], &out->wordlist.tail_crc32)) {
        return false;
    }
    out->generic_version = generic_version;
    out->status = (hs_crack_attempt_status_t)status;
    copy_text(out->password, sizeof(out->password), f[14]);
    return true;
}

static bool attempt_key_equal(const hs_crack_attempt_t *a,
                              const hs_crack_attempt_t *b)
{
    if (!hs_crack_cache_capture_equal(&a->capture, &b->capture) ||
        a->generic != b->generic) return false;
    return a->generic ? a->generic_version == b->generic_version
                      : hs_crack_cache_wordlist_equal(&a->wordlist, &b->wordlist);
}

static int write_attempt(FILE *file, const hs_crack_attempt_t *a)
{
    if (fprintf(file, "%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",",
                (uint32_t)HS_CRACK_CACHE_SCHEMA_VERSION, a->capture.crc32,
                a->capture.size) < 0 ||
        csv_put(file, a->capture.ssid) || fputc(',', file) == EOF ||
        csv_put(file, a->capture.bssid) ||
        fprintf(file, ",%u,", a->generic ? 1U : 0U) < 0 ||
        csv_put(file, a->generic ? "" : a->wordlist.path) ||
        fprintf(file, ",%" PRIu64 ",%" PRId64
                ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
                ",%" PRIu64 ",",
                a->generic ? 0 : a->wordlist.size,
                a->generic ? 0 : a->wordlist.mtime,
                a->generic ? 0 : a->wordlist.head_crc32,
                a->generic ? 0 : a->wordlist.tail_crc32,
                a->generic_version, (uint32_t)a->status, a->tried) < 0 ||
        csv_put(file, a->password) ||
        fprintf(file, ",%" PRIu32 "\n", a->match_index) < 0) {
        return -1;
    }
    return 0;
}

hs_crack_cache_result_t hs_crack_cache_find_attempt(
    const char *path, const hs_crack_capture_id_t *capture,
    bool generic, uint32_t generic_version,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_attempt_t *out)
{
    if (!path || !capture || !out || (!generic && !wordlist))
        return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? HS_CRACK_CACHE_NOT_FOUND
                                      : HS_CRACK_CACHE_IO_ERROR;
    hs_crack_attempt_t key = {0};
    key.capture = *capture;
    key.generic = generic;
    key.generic_version = generic_version;
    if (wordlist) key.wordlist = *wordlist;

    char line[HS_CACHE_LINE_MAX];
    hs_crack_cache_result_t result = HS_CRACK_CACHE_NOT_FOUND;
    while (fgets(line, sizeof(line), file)) {
        hs_crack_attempt_t parsed = {0};
        if (parse_attempt_line(line, &parsed) && attempt_key_equal(&key, &parsed)) {
            if (parsed.status != HS_CRACK_ATTEMPT_ERROR) {
                *out = parsed;
                result = HS_CRACK_CACHE_OK;
            }
        }
    }
    if (ferror(file)) result = HS_CRACK_CACHE_IO_ERROR;
    fclose(file);
    return result;
}

static bool copy_file_lines_without_key(FILE *src, FILE *dst,
                                        const hs_crack_attempt_t *key)
{
    char original[HS_CACHE_LINE_MAX];
    while (fgets(original, sizeof(original), src)) {
        char parsed_line[HS_CACHE_LINE_MAX];
        copy_text(parsed_line, sizeof(parsed_line), original);
        hs_crack_attempt_t parsed = {0};
        if (parse_attempt_line(parsed_line, &parsed) &&
            attempt_key_equal(&parsed, key)) {
            continue;
        }
        if (fputs(original, dst) == EOF) return false;
    }
    return !ferror(src);
}

static hs_crack_cache_result_t replace_with_temp(const char *path,
                                                 const char *temp_path)
{
    char backup_path[HS_CRACK_CACHE_PATH_MAX + 8];
    int n = snprintf(backup_path, sizeof(backup_path), "%s.bak", path);
    if (n < 0 || (size_t)n >= sizeof(backup_path))
        return HS_CRACK_CACHE_INVALID;

    (void)remove(backup_path);
    bool had_original = rename(path, backup_path) == 0;
    if (!had_original && errno != ENOENT) {
        remove(temp_path);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    if (rename(temp_path, path) != 0) {
        if (had_original) (void)rename(backup_path, path);
        remove(temp_path);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    if (had_original) (void)remove(backup_path);
    return HS_CRACK_CACHE_OK;
}

hs_crack_cache_result_t hs_crack_cache_store_full_crc(
    const char *cache_path, const hs_crack_wordlist_id_t *wordlist,
    uint32_t crc32)
{
    if (!cache_path || !wordlist) return HS_CRACK_CACHE_INVALID;
    char temp_path[HS_CRACK_CACHE_PATH_MAX + 8];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", cache_path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) return HS_CRACK_CACHE_INVALID;

    FILE *dst = fopen(temp_path, "wb");
    if (!dst) return HS_CRACK_CACHE_IO_ERROR;
    FILE *src = fopen(cache_path, "rb");
    bool ok = true;
    if (src) {
        char original[HS_CACHE_LINE_MAX];
        while (fgets(original, sizeof(original), src)) {
            char parsed_line[HS_CACHE_LINE_MAX];
            copy_text(parsed_line, sizeof(parsed_line), original);
            hs_crack_wordlist_id_t parsed = {0};
            uint32_t ignored = 0;
            if (parse_full_crc_line(parsed_line, &parsed, &ignored) &&
                strcmp(parsed.path, wordlist->path) == 0) {
                continue;
            }
            if (fputs(original, dst) == EOF) {
                ok = false;
                break;
            }
        }
        if (ferror(src)) ok = false;
        fclose(src);
    } else if (errno != ENOENT) {
        ok = false;
    }

    if (ok &&
        (fprintf(dst, "%" PRIu32 ",", (uint32_t)HS_CRACK_CACHE_SCHEMA_VERSION) < 0 ||
         csv_put(dst, wordlist->path) ||
         fprintf(dst, ",%" PRIu64 ",%" PRId64 ",%" PRIu32 ",%" PRIu32
                      ",%" PRIu32 "\n",
                 wordlist->size, wordlist->mtime, wordlist->head_crc32,
                 wordlist->tail_crc32, crc32) < 0)) {
        ok = false;
    }
    if (fclose(dst) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    return replace_with_temp(cache_path, temp_path);
}

hs_crack_cache_result_t hs_crack_cache_upsert_attempt(
    const char *path, const hs_crack_attempt_t *attempt)
{
    if (!path || !attempt) return HS_CRACK_CACHE_INVALID;
    char temp_path[HS_CRACK_CACHE_PATH_MAX + 8];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) return HS_CRACK_CACHE_INVALID;

    FILE *dst = fopen(temp_path, "wb");
    if (!dst) return HS_CRACK_CACHE_IO_ERROR;
    FILE *src = fopen(path, "rb");
    bool ok = true;
    if (src) {
        ok = copy_file_lines_without_key(src, dst, attempt);
        fclose(src);
    } else if (errno != ENOENT) {
        ok = false;
    }
    if (ok) ok = write_attempt(dst, attempt) == 0;
    if (fclose(dst) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    return replace_with_temp(path, temp_path);
}

static int write_resume(FILE *file, const hs_crack_resume_t *r)
{
    if (fprintf(file, "%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",",
                (uint32_t)HS_CRACK_CACHE_SCHEMA_VERSION, r->capture.crc32,
                r->capture.size) < 0 ||
        csv_put(file, r->capture.ssid) || fputc(',', file) == EOF ||
        csv_put(file, r->capture.bssid) || fputc(',', file) == EOF ||
        csv_put(file, r->wordlist.path) ||
        fprintf(file, ",%" PRIu64 ",%" PRId64
                ",%" PRIu32 ",%" PRIu32 ",%" PRIu64
                ",%" PRIu64 ",%u,%" PRIu32 ",%" PRIu64 "\n",
                r->wordlist.size, r->wordlist.mtime,
                r->wordlist.head_crc32, r->wordlist.tail_crc32,
                r->safe_offset, r->tried, r->all_mode ? 1U : 0U,
                r->selected_list_index, r->updated_sequence) < 0) {
        return -1;
    }
    return 0;
}

static bool parse_resume_line(char *line, hs_crack_resume_t *out)
{
    char *f[HS_CACHE_FIELD_MAX];
    size_t n = csv_split(line, f, HS_CACHE_FIELD_MAX);
    uint32_t schema, all_mode;
    if (n != 15 || !parse_u32(f[0], &schema) ||
        schema != HS_CRACK_CACHE_SCHEMA_VERSION ||
        !parse_u32(f[1], &out->capture.crc32) ||
        !parse_u64(f[2], &out->capture.size) ||
        !parse_u64(f[6], &out->wordlist.size) ||
        !parse_i64(f[7], &out->wordlist.mtime) ||
        !parse_u32(f[8], &out->wordlist.head_crc32) ||
        !parse_u32(f[9], &out->wordlist.tail_crc32) ||
        !parse_u64(f[10], &out->safe_offset) ||
        !parse_u64(f[11], &out->tried) ||
        !parse_u32(f[12], &all_mode) || all_mode > 1 ||
        !parse_u32(f[13], &out->selected_list_index) ||
        !parse_u64(f[14], &out->updated_sequence)) {
        return false;
    }
    copy_text(out->capture.ssid, sizeof(out->capture.ssid), f[3]);
    copy_text(out->capture.bssid, sizeof(out->capture.bssid), f[4]);
    copy_text(out->wordlist.path, sizeof(out->wordlist.path), f[5]);
    out->all_mode = all_mode != 0;
    return true;
}

hs_crack_cache_result_t hs_crack_cache_load_resume(
    const char *path, const hs_crack_capture_id_t *capture,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_resume_t *out)
{
    if (!path || !capture || !wordlist || !out) return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? HS_CRACK_CACHE_NOT_FOUND
                                      : HS_CRACK_CACHE_IO_ERROR;
    char line[HS_CACHE_LINE_MAX];
    bool got = fgets(line, sizeof(line), file) != NULL;
    bool io_error = ferror(file);
    fclose(file);
    if (io_error) return HS_CRACK_CACHE_IO_ERROR;
    hs_crack_resume_t parsed = {0};
    if (!got || !parse_resume_line(line, &parsed)) return HS_CRACK_CACHE_INVALID;
    if (!hs_crack_cache_capture_equal(capture, &parsed.capture) ||
        !hs_crack_cache_wordlist_equal(wordlist, &parsed.wordlist)) {
        return HS_CRACK_CACHE_STALE;
    }
    *out = parsed;
    return HS_CRACK_CACHE_OK;
}

hs_crack_cache_result_t hs_crack_cache_save_resume(
    const char *path, const hs_crack_resume_t *resume)
{
    if (!path || !resume) return HS_CRACK_CACHE_INVALID;
    char temp_path[HS_CRACK_CACHE_PATH_MAX + 8];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(temp_path)) return HS_CRACK_CACHE_INVALID;
    FILE *file = fopen(temp_path, "wb");
    if (!file) return HS_CRACK_CACHE_IO_ERROR;
    bool ok = write_resume(file, resume) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return HS_CRACK_CACHE_IO_ERROR;
    }
    return replace_with_temp(path, temp_path);
}

hs_crack_cache_result_t hs_crack_cache_remove_resume(const char *path)
{
    if (!path) return HS_CRACK_CACHE_INVALID;
    if (remove(path) == 0 || errno == ENOENT) return HS_CRACK_CACHE_OK;
    return HS_CRACK_CACHE_IO_ERROR;
}

static int ascii_casecmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a++);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b++);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int wordlist_compare(const void *left, const void *right)
{
    const hs_crack_wordlist_entry_t *a = left;
    const hs_crack_wordlist_entry_t *b = right;
    int result = ascii_casecmp(a->name, b->name);
    return result ? result : strcmp(a->name, b->name);
}

void hs_crack_cache_sort_wordlists(
    hs_crack_wordlist_entry_t *items, size_t count)
{
    if (items && count > 1) qsort(items, count, sizeof(*items), wordlist_compare);
}

bool hs_crack_cache_wordlist_name_allowed(const char *name)
{
    if (!name || !*name || name[0] == '.') return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return ascii_casecmp(dot, ".txt") == 0 ||
           ascii_casecmp(dot, ".lst") == 0 ||
           ascii_casecmp(dot, ".dic") == 0;
}

bool hs_crack_cache_decode_source(unsigned dropdown_index,
                                  size_t wordlist_count,
                                  hs_crack_source_kind_t *kind,
                                  size_t *wordlist_index)
{
    if (!kind || !wordlist_index) return false;
    *wordlist_index = 0;
    if (dropdown_index == 0) {
        *kind = HS_CRACK_SOURCE_NONE;
        return true;
    }
    if (dropdown_index == 1) {
        *kind = HS_CRACK_SOURCE_INTERNAL;
        return true;
    }
    size_t file_index = (size_t)dropdown_index - 2U;
    if (file_index < wordlist_count) {
        *kind = HS_CRACK_SOURCE_WORDLIST;
        *wordlist_index = file_index;
        return true;
    }
    if (wordlist_count > 1U && file_index == wordlist_count) {
        *kind = HS_CRACK_SOURCE_ALL;
        return true;
    }
    return false;
}

unsigned hs_crack_recommended_worker_count(bool unicore)
{
    return unicore ? 1U : 2U;
}

unsigned hs_crack_cache_byte_percent(uint64_t offset, uint64_t size)
{
    if (size == 0) return 0;
    if (offset >= size) return 100;
    return (unsigned)((offset * 100U) / size);
}

uint64_t hs_crack_cache_eta_seconds(
    uint64_t offset, uint64_t size, uint64_t processed,
    uint64_t elapsed_us)
{
    if (offset >= size && size != 0) return 0;
    if (offset == 0 || processed == 0 || elapsed_us == 0)
        return UINT64_MAX;
    uint64_t remaining = size - offset;
    if (remaining > UINT64_MAX / elapsed_us) return UINT64_MAX;
    uint64_t remaining_us = (remaining * elapsed_us + offset - 1) / offset;
    return (remaining_us + 999999U) / 1000000U;
}

bool hs_crack_checkpoint_due(uint64_t completed_since_checkpoint,
                             int64_t elapsed_since_checkpoint_us)
{
    return completed_since_checkpoint >= 64U ||
           elapsed_since_checkpoint_us >= 300000000LL;
}
