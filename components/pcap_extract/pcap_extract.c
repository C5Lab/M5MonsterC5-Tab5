#include "pcap_extract.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"

static const char *TAG = "pcap_extract";

#define EXTRACT_ROOT          "/sdcard/lab/espshark/objects"
#define EXTRACT_HEADER_MAX    4096U
#define EXTRACT_SNIFF_BYTES     32U
#define EXTRACT_ZERO_BLOCK     512U
#define EXTRACT_MIN_FRAME     2048U
#define EXTRACT_MAX_FRAME    16384U
#define EXTRACT_SCAN_BYTES    1024U

/* Longest extension pcap_extract_type_extension() can return, plus the room a
 * "_NN" collision suffix and the terminator need. Keeping both as compile-time
 * constants lets the compiler prove that the composed name fits. */
#define EXTRACT_EXTENSION_MAX    8U
#define EXTRACT_STEM_MAX (PCAP_EXTRACT_NAME_MAX - EXTRACT_EXTENSION_MAX - 4U)

/* ---------------------------------------------------------------- internals */

typedef enum {
    HTTP_STATE_RESYNC = 0,
    HTTP_STATE_HEADER,
    HTTP_STATE_BODY_LENGTH,
    HTTP_STATE_CHUNK_SIZE,
    HTTP_STATE_CHUNK_DATA,
    HTTP_STATE_CHUNK_CRLF,
    HTTP_STATE_TRAILER,
    HTTP_STATE_BODY_TO_CLOSE,
    HTTP_STATE_DONE,
} http_state_t;

typedef struct {
    char url[PCAP_EXTRACT_URL_MAX];
    bool head_request;
    bool connect_request;
} pcap_extract_request_t;

typedef struct {
    uint64_t key_forward; /* client -> server */
    uint64_t key_reverse; /* server -> client */
    char client[64];
    char server[64];
    char host[PCAP_EXTRACT_HOST_MAX];
    uint16_t client_port;
    uint16_t server_port;
    uint32_t base_sequence;
    uint32_t first_packet;
    uint32_t segment_count;
    pcap_extract_request_t pending[PCAP_EXTRACT_PENDING_URLS];
    uint8_t pending_count;
    uint8_t pending_taken;
    bool pending_overflow;
    bool base_valid;
    bool response_seen;
} pcap_extract_session_t;

typedef struct {
    uint64_t data_offset;
    uint64_t time_us;
    uint32_t packet_number;
    uint32_t sequence; /* relative to the session base */
    uint16_t payload_offset;
    uint16_t payload_length;
    uint16_t captured_length;
    uint16_t session;
} pcap_extract_segment_t;

typedef struct {
    uint8_t state;
    uint8_t resync_match;
    char header[EXTRACT_HEADER_MAX];
    uint32_t header_length;

    bool body_open;
    bool discard;    /* framing is tracked but nothing is written */
    bool limit_hit;  /* per-object byte ceiling reached */
    bool io_failed;
    FILE *file;
    char part_path[PCAP_EXTRACT_PATH_MAX];
    mbedtls_sha256_context sha;
    uint32_t object_index;
    uint64_t body_written;
    uint64_t body_remaining;
    uint64_t chunk_remaining;
    char chunk_line[24];
    uint8_t chunk_line_length;
    uint8_t sniff[EXTRACT_SNIFF_BYTES];
    uint8_t sniff_length;
    /* Name proposed by Content-Disposition. Attacker-controlled, sanitized on use. */
    char suggested_name[PCAP_EXTRACT_NAME_MAX];
} pcap_extract_http_t;

typedef struct {
    pcap_reader_t *reader;
    pcap_capture_info_t info;
    pcap_extract_result_t *result;
    pcap_extract_session_t *sessions;
    pcap_extract_segment_t *segments;
    uint32_t segment_count;
    uint8_t *frame;
    size_t frame_capacity;
    uint64_t total_bytes; /* budget for the whole run, never reset per session */
    const volatile bool *cancel;
    pcap_extract_progress_cb_t progress;
    void *progress_ctx;
} pcap_extract_ctx_t;

/* ------------------------------------------------------------- small helpers */

static bool cancelled(const pcap_extract_ctx_t *ctx)
{
    return ctx->cancel && *ctx->cancel;
}

static void copy_bounded(char *destination, size_t capacity, const char *source,
                         size_t length)
{
    if (!destination || capacity == 0) return;
    size_t copied = 0;
    for (size_t i = 0; source && i < length && copied + 1U < capacity; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c < 32U || c == 127U) continue;
        destination[copied++] = (char)c;
    }
    destination[copied] = '\0';
}

/* strcasestr is a GNU extension, so the component carries its own. */
static const char *find_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return NULL;
    size_t needle_length = strlen(needle);
    for (const char *c = haystack; *c; c++) {
        if (strncasecmp(c, needle, needle_length) == 0) return c;
    }
    return NULL;
}

static uint64_t hash_tuple(const char *source, uint16_t source_port,
                           const char *destination, uint16_t destination_port)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const char *c = source; c && *c; c++) {
        hash = (hash ^ (uint8_t)*c) * 1099511628211ULL;
    }
    hash = (hash ^ (source_port & 0xFFU)) * 1099511628211ULL;
    hash = (hash ^ (source_port >> 8)) * 1099511628211ULL;
    for (const char *c = destination; c && *c; c++) {
        hash = (hash ^ (uint8_t)*c) * 1099511628211ULL;
    }
    hash = (hash ^ (destination_port & 0xFFU)) * 1099511628211ULL;
    hash = (hash ^ (destination_port >> 8)) * 1099511628211ULL;
    return hash;
}

static bool make_directory(const char *path)
{
    if (mkdir(path, 0775) == 0) return true;
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool prepare_directory(const char *path)
{
    return make_directory("/sdcard/lab") && make_directory("/sdcard/lab/espshark") &&
           make_directory(EXTRACT_ROOT) && make_directory(path);
}

/* Removes the previously extracted copies for this capture. Only the derived
 * artifact directory is touched; the capture itself is never modified. */
static void clear_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char victim[PCAP_EXTRACT_PATH_MAX];
        if (snprintf(victim, sizeof(victim), "%s/%s", path, entry->d_name) >=
            (int)sizeof(victim)) {
            continue;
        }
        unlink(victim);
    }
    closedir(dir);
}

static void sanitize_component(const char *source, char *output, size_t output_size)
{
    if (!output || output_size == 0) return;
    size_t written = 0;
    bool last_was_separator = false;
    for (size_t i = 0; source && source[i] && written + 1U < output_size; i++) {
        unsigned char c = (unsigned char)source[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!keep) {
            if (last_was_separator || written == 0) continue;
            output[written++] = '_';
            last_was_separator = true;
            continue;
        }
        /* A leading dot would hide the artifact and ".." would escape upwards. */
        if (c == '.' && written == 0) continue;
        output[written++] = (char)c;
        last_was_separator = false;
    }
    while (written > 0 && (output[written - 1U] == '_' || output[written - 1U] == '.')) {
        written--;
    }
    output[written] = '\0';
}

static void capture_stem(const char *capture_path, char *output, size_t output_size)
{
    const char *slash = strrchr(capture_path, '/');
    const char *base = slash ? slash + 1 : capture_path;
    char trimmed[80];
    copy_bounded(trimmed, sizeof(trimmed), base, strlen(base));
    char *dot = strrchr(trimmed, '.');
    if (dot && dot != trimmed) *dot = '\0';
    sanitize_component(trimmed, output, output_size);
    if (output[0] == '\0') {
        snprintf(output, output_size, "capture");
    }
}

/* -------------------------------------------------------------- type helpers */

static uint8_t type_from_mime(const char *mime)
{
    if (!mime || !mime[0]) return PCAP_EXTRACT_TYPE_UNKNOWN;
    if (strncasecmp(mime, "text/html", 9) == 0 ||
        strncasecmp(mime, "application/xhtml", 17) == 0) return PCAP_EXTRACT_TYPE_HTML;
    if (strncasecmp(mime, "application/json", 16) == 0 ||
        strncasecmp(mime, "text/json", 9) == 0) return PCAP_EXTRACT_TYPE_JSON;
    if (strncasecmp(mime, "text/xml", 8) == 0 ||
        strncasecmp(mime, "application/xml", 15) == 0) return PCAP_EXTRACT_TYPE_XML;
    if (strncasecmp(mime, "image/jpeg", 10) == 0 ||
        strncasecmp(mime, "image/jpg", 9) == 0) return PCAP_EXTRACT_TYPE_JPEG;
    if (strncasecmp(mime, "image/png", 9) == 0) return PCAP_EXTRACT_TYPE_PNG;
    if (strncasecmp(mime, "image/gif", 9) == 0) return PCAP_EXTRACT_TYPE_GIF;
    if (strncasecmp(mime, "image/webp", 10) == 0) return PCAP_EXTRACT_TYPE_WEBP;
    if (strncasecmp(mime, "application/pdf", 15) == 0) return PCAP_EXTRACT_TYPE_PDF;
    if (strncasecmp(mime, "application/zip", 15) == 0 ||
        strncasecmp(mime, "application/x-zip", 17) == 0) return PCAP_EXTRACT_TYPE_ZIP;
    if (strncasecmp(mime, "application/gzip", 16) == 0 ||
        strncasecmp(mime, "application/x-gzip", 18) == 0) return PCAP_EXTRACT_TYPE_GZIP;
    if (strncasecmp(mime, "text/", 5) == 0) return PCAP_EXTRACT_TYPE_TEXT;
    return PCAP_EXTRACT_TYPE_UNKNOWN;
}

static uint8_t type_from_signature(const uint8_t *data, size_t length)
{
    if (!data || length == 0) return PCAP_EXTRACT_TYPE_UNKNOWN;
    if (length >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return PCAP_EXTRACT_TYPE_JPEG;
    }
    if (length >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return PCAP_EXTRACT_TYPE_PNG;
    }
    if (length >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
        return PCAP_EXTRACT_TYPE_GIF;
    }
    if (length >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        return PCAP_EXTRACT_TYPE_WEBP;
    }
    if (length >= 5 && memcmp(data, "%PDF-", 5) == 0) return PCAP_EXTRACT_TYPE_PDF;
    if (length >= 4 && data[0] == 'P' && data[1] == 'K' &&
        ((data[2] == 3 && data[3] == 4) || (data[2] == 5 && data[3] == 6) ||
         (data[2] == 7 && data[3] == 8))) {
        return PCAP_EXTRACT_TYPE_ZIP;
    }
    if (length >= 2 && data[0] == 0x1F && data[1] == 0x8B) return PCAP_EXTRACT_TYPE_GZIP;

    size_t start = 0;
    while (start < length && isspace((int)data[start])) start++;
    if (start < length) {
        size_t left = length - start;
        const char *text = (const char *)data + start;
        if (left >= 5 && strncasecmp(text, "<?xml", 5) == 0) return PCAP_EXTRACT_TYPE_XML;
        if (left >= 9 && strncasecmp(text, "<!doctype", 9) == 0) return PCAP_EXTRACT_TYPE_HTML;
        if (left >= 5 && strncasecmp(text, "<html", 5) == 0) return PCAP_EXTRACT_TYPE_HTML;
        if (text[0] == '{' || text[0] == '[') return PCAP_EXTRACT_TYPE_JSON;
    }
    size_t printable = 0;
    for (size_t i = 0; i < length; i++) {
        if (data[i] == '\r' || data[i] == '\n' || data[i] == '\t' ||
            (data[i] >= 32 && data[i] < 127)) {
            printable++;
        }
    }
    if (printable * 10U >= length * 9U) return PCAP_EXTRACT_TYPE_TEXT;
    return PCAP_EXTRACT_TYPE_UNKNOWN;
}

const char *pcap_extract_type_name(uint8_t type)
{
    switch (type) {
        case PCAP_EXTRACT_TYPE_TEXT: return "TEXT";
        case PCAP_EXTRACT_TYPE_HTML: return "HTML";
        case PCAP_EXTRACT_TYPE_JSON: return "JSON";
        case PCAP_EXTRACT_TYPE_XML: return "XML";
        case PCAP_EXTRACT_TYPE_JPEG: return "JPEG";
        case PCAP_EXTRACT_TYPE_PNG: return "PNG";
        case PCAP_EXTRACT_TYPE_GIF: return "GIF";
        case PCAP_EXTRACT_TYPE_WEBP: return "WEBP";
        case PCAP_EXTRACT_TYPE_PDF: return "PDF";
        case PCAP_EXTRACT_TYPE_ZIP: return "ZIP";
        case PCAP_EXTRACT_TYPE_GZIP: return "GZIP";
        default: return "UNKNOWN";
    }
}

const char *pcap_extract_type_extension(uint8_t type)
{
    switch (type) {
        case PCAP_EXTRACT_TYPE_TEXT: return ".txt";
        case PCAP_EXTRACT_TYPE_HTML: return ".html";
        case PCAP_EXTRACT_TYPE_JSON: return ".json";
        case PCAP_EXTRACT_TYPE_XML: return ".xml";
        case PCAP_EXTRACT_TYPE_JPEG: return ".jpg";
        case PCAP_EXTRACT_TYPE_PNG: return ".png";
        case PCAP_EXTRACT_TYPE_GIF: return ".gif";
        case PCAP_EXTRACT_TYPE_WEBP: return ".webp";
        case PCAP_EXTRACT_TYPE_PDF: return ".pdf";
        case PCAP_EXTRACT_TYPE_ZIP: return ".zip";
        case PCAP_EXTRACT_TYPE_GZIP: return ".gz";
        default: return ".bin";
    }
}

bool pcap_extract_type_is_text(uint8_t type)
{
    return type == PCAP_EXTRACT_TYPE_TEXT || type == PCAP_EXTRACT_TYPE_HTML ||
           type == PCAP_EXTRACT_TYPE_JSON || type == PCAP_EXTRACT_TYPE_XML;
}

const char *pcap_extract_state_name(uint8_t state)
{
    switch (state) {
        case PCAP_EXTRACT_STATE_COMPLETE: return "COMPLETE";
        case PCAP_EXTRACT_STATE_PARTIAL: return "PARTIAL";
        case PCAP_EXTRACT_STATE_GAPS: return "GAPS";
        case PCAP_EXTRACT_STATE_TRUNCATED: return "TRUNCATED";
        default: return "UNKNOWN";
    }
}

const char *pcap_extract_confidence_name(uint8_t confidence)
{
    switch (confidence) {
        case PCAP_EXTRACT_CONFIDENCE_LOW: return "LOW";
        case PCAP_EXTRACT_CONFIDENCE_MEDIUM: return "MEDIUM";
        case PCAP_EXTRACT_CONFIDENCE_HIGH: return "HIGH";
        case PCAP_EXTRACT_CONFIDENCE_MISMATCH: return "MISMATCH";
        default: return "LOW";
    }
}

const char *pcap_extract_status_name(pcap_extract_status_t status)
{
    switch (status) {
        case PCAP_EXTRACT_OK: return "OK";
        case PCAP_EXTRACT_INVALID_ARG: return "Invalid argument";
        case PCAP_EXTRACT_NO_MEMORY: return "Out of memory";
        case PCAP_EXTRACT_IO_ERROR: return "SD card I/O error";
        case PCAP_EXTRACT_READER_ERROR: return "PCAP could not be read";
        case PCAP_EXTRACT_CANCELLED: return "Cancelled";
        case PCAP_EXTRACT_NOTHING_FOUND: return "No plaintext HTTP objects found";
        default: return "Unknown error";
    }
}

/* -------------------------------------------------------------- HTTP parsing */

static const char *header_value(const char *header, const char *name)
{
    size_t name_length = strlen(name);
    const char *cursor = header;
    while (cursor && *cursor) {
        const char *line_end = strchr(cursor, '\n');
        if (cursor != header && strncasecmp(cursor, name, name_length) == 0 &&
            cursor[name_length] == ':') {
            const char *value = cursor + name_length + 1U;
            while (*value == ' ' || *value == '\t') value++;
            return value;
        }
        if (!line_end) break;
        cursor = line_end + 1U;
    }
    return NULL;
}

static void header_copy_value(const char *header, const char *name,
                              char *output, size_t output_size)
{
    output[0] = '\0';
    const char *value = header_value(header, name);
    if (!value) return;
    size_t length = 0;
    while (value[length] && value[length] != '\r' && value[length] != '\n') length++;
    copy_bounded(output, output_size, value, length);
}

static void finish_object(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                          uint8_t state);

static void object_set_time(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                            uint32_t packet_number, uint64_t time_us)
{
    if (!http->body_open || http->discard) return;
    pcap_extract_object_t *object = &ctx->result->objects[http->object_index];
    if (object->first_packet == 0U) {
        object->first_packet = packet_number + 1U;
        object->first_time_us = time_us;
    }
    object->last_packet = packet_number + 1U;
    object->last_time_us = time_us;
}

static void body_write(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                       const uint8_t *data, size_t length)
{
    if (!http->body_open || http->discard || http->io_failed || length == 0) return;
    if (http->limit_hit) return;
    size_t writable = length;
    if (http->body_written + writable > PCAP_EXTRACT_OBJECT_BYTE_LIMIT) {
        writable = (size_t)(PCAP_EXTRACT_OBJECT_BYTE_LIMIT - http->body_written);
        http->limit_hit = true;
    }
    if (writable == 0) return;
    size_t sniffed = 0;
    while (http->sniff_length < EXTRACT_SNIFF_BYTES && sniffed < writable) {
        http->sniff[http->sniff_length++] = data[sniffed++];
    }
    if (fwrite(data, 1, writable, http->file) != writable) {
        http->io_failed = true;
        return;
    }
    mbedtls_sha256_update(&http->sha, data, writable);
    http->body_written += writable;
    ctx->total_bytes += writable;
    ctx->result->written_bytes += writable;
}

/* Missing TCP bytes are zero-filled so that the surrounding structure keeps its
 * offsets. The manifest always reports how many bytes were fabricated. */
static bool body_fill_gap(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                          uint64_t gap)
{
    if (gap > PCAP_EXTRACT_MAX_GAP_FILL) return false;
    uint8_t zeros[EXTRACT_ZERO_BLOCK];
    memset(zeros, 0, sizeof(zeros));
    uint64_t remaining = gap;
    while (remaining > 0 && !http->io_failed && !http->limit_hit) {
        size_t block = remaining > sizeof(zeros) ? sizeof(zeros) : (size_t)remaining;
        body_write(ctx, http, zeros, block);
        remaining -= block;
    }
    if (http->body_open && !http->discard) {
        ctx->result->objects[http->object_index].gap_bytes += gap;
    }
    return true;
}

static void choose_object_name(pcap_extract_ctx_t *ctx, pcap_extract_object_t *object,
                               uint32_t object_index, const char *suggested_name)
{
    char candidate[PCAP_EXTRACT_NAME_MAX] = {0};
    if (suggested_name && suggested_name[0]) {
        sanitize_component(suggested_name, candidate, sizeof(candidate));
    }
    const char *url = object->url;
    if (candidate[0] == '\0' && url[0]) {
        const char *cut = url;
        for (const char *c = url; *c; c++) {
            if (*c == '?' || *c == '#') break;
            if (*c == '/') cut = c + 1;
        }
        char trimmed[PCAP_EXTRACT_NAME_MAX];
        size_t length = 0;
        while (cut[length] && cut[length] != '?' && cut[length] != '#' &&
               length + 1U < sizeof(trimmed)) {
            length++;
        }
        copy_bounded(trimmed, sizeof(trimmed), cut, length);
        sanitize_component(trimmed, candidate, sizeof(candidate));
    }
    if (candidate[0] == '\0') {
        snprintf(candidate, sizeof(candidate), "object_%03u", (unsigned)object_index + 1U);
    }

    /* The extension always follows what the bytes actually are, never what the
     * server claimed. If the candidate already ends with it, keep only the stem
     * so that it is not appended twice. */
    const char *extension = pcap_extract_type_extension(object->type);
    size_t extension_length = strlen(extension);
    if (extension_length > EXTRACT_EXTENSION_MAX) extension_length = EXTRACT_EXTENSION_MAX;
    char *dot = strrchr(candidate, '.');
    if (dot && dot != candidate && strcasecmp(dot, extension) == 0) {
        *dot = '\0';
    }

    /* Reserve room for the extension and a "_NN" collision suffix, then compose
     * the name by hand. Both bounds are compile-time constants so the result
     * provably fits and no truncation analysis is needed. */
    size_t stem_length = strlen(candidate);
    if (stem_length > EXTRACT_STEM_MAX) stem_length = EXTRACT_STEM_MAX;

    for (uint32_t attempt = 0; attempt < 100U; attempt++) {
        size_t position = stem_length;
        memcpy(object->name, candidate, stem_length);
        if (attempt > 0U) {
            object->name[position++] = '_';
            object->name[position++] = (char)('0' + ((attempt / 10U) % 10U));
            object->name[position++] = (char)('0' + (attempt % 10U));
        }
        memcpy(object->name + position, extension, extension_length);
        object->name[position + extension_length] = '\0';

        bool clash = false;
        for (uint32_t i = 0; i < ctx->result->object_count; i++) {
            if (i == object_index) continue;
            if (strcasecmp(ctx->result->objects[i].name, object->name) == 0) {
                clash = true;
                break;
            }
        }
        if (!clash) return;
    }
}

static void finish_object(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                          uint8_t state)
{
    if (!http->body_open) return;
    http->body_open = false;

    if (http->discard) {
        if (http->file) fclose(http->file);
        http->file = NULL;
        return;
    }

    pcap_extract_object_t *object = &ctx->result->objects[http->object_index];
    static const char hex_digits[] = "0123456789abcdef";
    uint8_t digest[32];
    mbedtls_sha256_finish(&http->sha, digest);
    mbedtls_sha256_free(&http->sha);
    for (size_t i = 0; i < sizeof(digest); i++) {
        object->sha256[i * 2U] = hex_digits[digest[i] >> 4];
        object->sha256[(i * 2U) + 1U] = hex_digits[digest[i] & 0x0FU];
    }
    object->sha256[sizeof(digest) * 2U] = '\0';

    bool io_ok = !http->io_failed;
    if (http->file) {
        if (fflush(http->file) != 0) io_ok = false;
        int descriptor = fileno(http->file);
        if (descriptor >= 0) fsync(descriptor);
        if (fclose(http->file) != 0) io_ok = false;
        http->file = NULL;
    }

    object->bytes = http->body_written;
    if (http->limit_hit) {
        object->state = PCAP_EXTRACT_STATE_TRUNCATED;
    } else if (state == PCAP_EXTRACT_STATE_COMPLETE && object->gap_bytes > 0) {
        object->state = PCAP_EXTRACT_STATE_GAPS;
    } else {
        object->state = state;
    }

    object->detected_type = type_from_signature(http->sniff, http->sniff_length);
    uint8_t mime_type = type_from_mime(object->declared_mime);
    if (object->encoded) {
        /* The declared MIME describes the decoded content, so it cannot be
         * compared against the signature of the compressed bytes. */
        object->type = object->detected_type != PCAP_EXTRACT_TYPE_UNKNOWN
                           ? object->detected_type : PCAP_EXTRACT_TYPE_GZIP;
        object->confidence = PCAP_EXTRACT_CONFIDENCE_MEDIUM;
    } else if (mime_type != PCAP_EXTRACT_TYPE_UNKNOWN &&
               object->detected_type != PCAP_EXTRACT_TYPE_UNKNOWN) {
        bool agree = mime_type == object->detected_type ||
                     (pcap_extract_type_is_text(mime_type) &&
                      object->detected_type == PCAP_EXTRACT_TYPE_TEXT);
        object->type = agree ? mime_type : object->detected_type;
        object->confidence = agree ? PCAP_EXTRACT_CONFIDENCE_HIGH
                                   : PCAP_EXTRACT_CONFIDENCE_MISMATCH;
    } else if (object->detected_type != PCAP_EXTRACT_TYPE_UNKNOWN) {
        object->type = object->detected_type;
        object->confidence = PCAP_EXTRACT_CONFIDENCE_MEDIUM;
    } else if (mime_type != PCAP_EXTRACT_TYPE_UNKNOWN) {
        object->type = mime_type;
        object->confidence = PCAP_EXTRACT_CONFIDENCE_MEDIUM;
    } else {
        object->type = PCAP_EXTRACT_TYPE_UNKNOWN;
        object->confidence = PCAP_EXTRACT_CONFIDENCE_LOW;
    }

    if (object->bytes == 0 || !io_ok) {
        unlink(http->part_path);
        object->saved = false;
        ctx->result->object_count--;
        return;
    }

    choose_object_name(ctx, object, http->object_index, http->suggested_name);
    char final_path[PCAP_EXTRACT_PATH_MAX];
    if (snprintf(final_path, sizeof(final_path), "%s/%s", ctx->result->directory,
                 object->name) >= (int)sizeof(final_path)) {
        object->saved = false;
        return;
    }
    unlink(final_path);
    object->saved = rename(http->part_path, final_path) == 0;
    if (!object->saved) {
        ESP_LOGW(TAG, "Could not finalize %s", final_path);
    }
}

static void start_object(pcap_extract_ctx_t *ctx, pcap_extract_session_t *session,
                         pcap_extract_http_t *http, uint16_t status_code,
                         const pcap_extract_request_t *request)
{
    http->body_open = true;
    http->discard = false;
    http->limit_hit = false;
    http->body_written = 0;
    http->sniff_length = 0;
    http->file = NULL;

    if (ctx->result->object_count >= PCAP_EXTRACT_MAX_OBJECTS ||
        ctx->total_bytes >= PCAP_EXTRACT_RUN_BYTE_LIMIT) {
        if (ctx->result->object_count >= PCAP_EXTRACT_MAX_OBJECTS) {
            ctx->result->object_limited = true;
        } else {
            ctx->result->budget_reached = true;
        }
        http->discard = true;
        return;
    }

    http->object_index = ctx->result->object_count++;
    pcap_extract_object_t *object = &ctx->result->objects[http->object_index];
    memset(object, 0, sizeof(*object));
    object->http_status = status_code;
    snprintf(object->client, sizeof(object->client), "%s", session->client);
    snprintf(object->server, sizeof(object->server), "%s", session->server);
    object->client_port = session->client_port;
    object->server_port = session->server_port;
    snprintf(object->host, sizeof(object->host), "%s", session->host);
    if (request) {
        snprintf(object->url, sizeof(object->url), "%s", request->url);
    }
    header_copy_value(http->header, "Content-Type", object->declared_mime,
                      sizeof(object->declared_mime));
    char *semicolon = strchr(object->declared_mime, ';');
    if (semicolon) *semicolon = '\0';
    header_copy_value(http->header, "Content-Encoding", object->content_encoding,
                      sizeof(object->content_encoding));
    object->encoded = object->content_encoding[0] != '\0' &&
                      strcasecmp(object->content_encoding, "identity") != 0;

    http->suggested_name[0] = '\0';
    char disposition[128];
    header_copy_value(http->header, "Content-Disposition", disposition,
                      sizeof(disposition));
    const char *filename = find_ci(disposition, "filename=");
    if (filename) {
        filename += 9;
        if (*filename == '"') filename++;
        size_t length = 0;
        while (filename[length] && filename[length] != '"' && filename[length] != ';') {
            length++;
        }
        /* Kept verbatim here; sanitize_component() strips paths before it is used. */
        copy_bounded(http->suggested_name, sizeof(http->suggested_name),
                     filename, length);
    }

    if (snprintf(http->part_path, sizeof(http->part_path), "%s/object_%03u.part",
                 ctx->result->directory, (unsigned)http->object_index + 1U) >=
        (int)sizeof(http->part_path)) {
        http->discard = true;
        ctx->result->object_count--;
        return;
    }
    http->file = fopen(http->part_path, "wb");
    if (!http->file) {
        ESP_LOGW(TAG, "Could not open %s", http->part_path);
        http->discard = true;
        ctx->result->object_count--;
        return;
    }
    mbedtls_sha256_init(&http->sha);
    mbedtls_sha256_starts(&http->sha, 0);
}

static void parse_response_header(pcap_extract_ctx_t *ctx,
                                  pcap_extract_session_t *session,
                                  pcap_extract_http_t *http)
{
    ctx->result->http_responses++;
    unsigned status_code = 0;
    const char *space = strchr(http->header, ' ');
    if (space) status_code = (unsigned)strtoul(space + 1, NULL, 10);

    const pcap_extract_request_t *request = NULL;
    if (session->pending_taken < session->pending_count) {
        request = &session->pending[session->pending_taken++];
    }

    if (request && request->connect_request && status_code >= 200U && status_code < 300U) {
        /* Everything after a successful CONNECT is a tunnel we cannot read. */
        ctx->result->encrypted_flows++;
        http->state = HTTP_STATE_DONE;
        return;
    }

    bool bodyless = status_code < 200U || status_code == 204U || status_code == 304U ||
                    (request && request->head_request);
    if (bodyless) {
        http->state = HTTP_STATE_RESYNC;
        http->resync_match = 0;
        return;
    }

    char transfer_encoding[48];
    header_copy_value(http->header, "Transfer-Encoding", transfer_encoding,
                      sizeof(transfer_encoding));
    bool chunked = find_ci(transfer_encoding, "chunked") != NULL;

    char content_length[24];
    header_copy_value(http->header, "Content-Length", content_length,
                      sizeof(content_length));

    if (chunked) {
        start_object(ctx, session, http, (uint16_t)status_code, request);
        if (!http->discard) ctx->result->objects[http->object_index].chunked = true;
        http->chunk_line_length = 0;
        http->state = HTTP_STATE_CHUNK_SIZE;
        return;
    }
    if (content_length[0]) {
        uint64_t declared = strtoull(content_length, NULL, 10);
        if (declared == 0) {
            http->state = HTTP_STATE_RESYNC;
            http->resync_match = 0;
            return;
        }
        start_object(ctx, session, http, (uint16_t)status_code, request);
        if (!http->discard) {
            ctx->result->objects[http->object_index].declared_bytes = declared;
        }
        http->body_remaining = declared;
        http->state = HTTP_STATE_BODY_LENGTH;
        return;
    }
    start_object(ctx, session, http, (uint16_t)status_code, request);
    http->state = HTTP_STATE_BODY_TO_CLOSE;
}

static void http_feed(pcap_extract_ctx_t *ctx, pcap_extract_session_t *session,
                      pcap_extract_http_t *http, const uint8_t *data, size_t length,
                      uint32_t packet_number, uint64_t time_us)
{
    static const char marker[] = "HTTP/1.";
    size_t position = 0;
    while (position < length && http->state != HTTP_STATE_DONE && !http->io_failed) {
        size_t available = length - position;
        const uint8_t *cursor = data + position;
        size_t consumed = 0;

        switch (http->state) {
            case HTTP_STATE_RESYNC: {
                while (consumed < available) {
                    char c = (char)cursor[consumed++];
                    if (c == marker[http->resync_match]) {
                        http->resync_match++;
                        if (http->resync_match == 7U) {
                            memcpy(http->header, marker, 7U);
                            http->header_length = 7U;
                            http->header[7] = '\0';
                            http->resync_match = 0;
                            http->state = HTTP_STATE_HEADER;
                            break;
                        }
                    } else {
                        http->resync_match = (c == 'H') ? 1U : 0U;
                    }
                }
                break;
            }
            case HTTP_STATE_HEADER: {
                bool terminated = false;
                while (consumed < available) {
                    if (http->header_length + 2U >= EXTRACT_HEADER_MAX) {
                        http->state = HTTP_STATE_RESYNC;
                        http->resync_match = 0;
                        http->header_length = 0;
                        break;
                    }
                    http->header[http->header_length++] = (char)cursor[consumed++];
                    http->header[http->header_length] = '\0';
                    uint32_t used = http->header_length;
                    if (used >= 4U && memcmp(&http->header[used - 4U], "\r\n\r\n", 4) == 0) {
                        terminated = true;
                        break;
                    }
                    if (used >= 2U && http->header[used - 1U] == '\n' &&
                        http->header[used - 2U] == '\n') {
                        terminated = true;
                        break;
                    }
                }
                if (terminated) parse_response_header(ctx, session, http);
                break;
            }
            case HTTP_STATE_BODY_LENGTH: {
                size_t take = available;
                if ((uint64_t)take > http->body_remaining) {
                    take = (size_t)http->body_remaining;
                }
                object_set_time(ctx, http, packet_number, time_us);
                body_write(ctx, http, cursor, take);
                http->body_remaining -= take;
                consumed = take;
                if (http->body_remaining == 0) {
                    finish_object(ctx, http, PCAP_EXTRACT_STATE_COMPLETE);
                    http->state = HTTP_STATE_RESYNC;
                    http->resync_match = 0;
                }
                break;
            }
            case HTTP_STATE_CHUNK_SIZE: {
                while (consumed < available) {
                    char c = (char)cursor[consumed++];
                    if (c == '\n') {
                        http->chunk_line[http->chunk_line_length] = '\0';
                        char *end = strchr(http->chunk_line, ';');
                        if (end) *end = '\0';
                        http->chunk_remaining = strtoull(http->chunk_line, NULL, 16);
                        http->chunk_line_length = 0;
                        http->state = http->chunk_remaining == 0
                                          ? HTTP_STATE_TRAILER : HTTP_STATE_CHUNK_DATA;
                        break;
                    }
                    if (c != '\r' &&
                        http->chunk_line_length + 1U < sizeof(http->chunk_line)) {
                        http->chunk_line[http->chunk_line_length++] = c;
                    }
                }
                break;
            }
            case HTTP_STATE_CHUNK_DATA: {
                size_t take = available;
                if ((uint64_t)take > http->chunk_remaining) {
                    take = (size_t)http->chunk_remaining;
                }
                object_set_time(ctx, http, packet_number, time_us);
                body_write(ctx, http, cursor, take);
                http->chunk_remaining -= take;
                consumed = take;
                if (http->chunk_remaining == 0) http->state = HTTP_STATE_CHUNK_CRLF;
                break;
            }
            case HTTP_STATE_CHUNK_CRLF: {
                while (consumed < available) {
                    char c = (char)cursor[consumed++];
                    if (c == '\n') {
                        http->chunk_line_length = 0;
                        http->state = HTTP_STATE_CHUNK_SIZE;
                        break;
                    }
                }
                break;
            }
            case HTTP_STATE_TRAILER: {
                while (consumed < available) {
                    char c = (char)cursor[consumed++];
                    if (c == '\n') {
                        if (http->chunk_line_length == 0) {
                            finish_object(ctx, http, PCAP_EXTRACT_STATE_COMPLETE);
                            http->state = HTTP_STATE_RESYNC;
                            http->resync_match = 0;
                            break;
                        }
                        http->chunk_line_length = 0;
                    } else if (c != '\r') {
                        http->chunk_line_length = 1U;
                    }
                }
                break;
            }
            case HTTP_STATE_BODY_TO_CLOSE: {
                object_set_time(ctx, http, packet_number, time_us);
                body_write(ctx, http, cursor, available);
                consumed = available;
                break;
            }
            default:
                consumed = available;
                break;
        }

        if (consumed == 0) {
            /* No state may stall: without progress the loop would never end. */
            consumed = 1;
        }
        position += consumed;
    }
}

static void http_feed_gap(pcap_extract_ctx_t *ctx, pcap_extract_http_t *http,
                          uint64_t gap)
{
    if (gap == 0 || http->state == HTTP_STATE_DONE) return;
    switch (http->state) {
        case HTTP_STATE_BODY_LENGTH:
            if (gap <= http->body_remaining && body_fill_gap(ctx, http, gap)) {
                http->body_remaining -= gap;
                if (http->body_remaining == 0) {
                    finish_object(ctx, http, PCAP_EXTRACT_STATE_GAPS);
                    http->state = HTTP_STATE_RESYNC;
                    http->resync_match = 0;
                }
                return;
            }
            break;
        case HTTP_STATE_BODY_TO_CLOSE:
            if (body_fill_gap(ctx, http, gap)) return;
            break;
        default:
            /* Header and chunk framing cannot survive missing bytes. */
            break;
    }
    finish_object(ctx, http, PCAP_EXTRACT_STATE_PARTIAL);
    http->state = HTTP_STATE_RESYNC;
    http->resync_match = 0;
    http->header_length = 0;
    http->chunk_line_length = 0;
}

/* --------------------------------------------------------------- discovery */

static bool looks_like_request(const uint8_t *payload, size_t length,
                               bool *head_out, bool *connect_out)
{
    static const char *methods[] = {
        "GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ", "PATCH ", "CONNECT "
    };
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        size_t method_length = strlen(methods[i]);
        if (length >= method_length &&
            memcmp(payload, methods[i], method_length) == 0) {
            if (head_out) *head_out = i == 3U;
            if (connect_out) *connect_out = i == 7U;
            return true;
        }
    }
    return false;
}

static bool looks_like_response(const uint8_t *payload, size_t length)
{
    return length >= 7U && memcmp(payload, "HTTP/1.", 7U) == 0;
}

static void record_request(pcap_extract_session_t *session, const uint8_t *payload,
                           size_t length, bool head_request, bool connect_request)
{
    if (session->pending_count >= PCAP_EXTRACT_PENDING_URLS) {
        session->pending_overflow = true;
        return;
    }
    const uint8_t *space = memchr(payload, ' ', length);
    if (!space) return;
    const uint8_t *url = space + 1;
    size_t url_length = 0;
    while ((size_t)(url - payload) + url_length < length &&
           url[url_length] != ' ' && url[url_length] != '\r' && url[url_length] != '\n') {
        url_length++;
    }
    pcap_extract_request_t *entry = &session->pending[session->pending_count++];
    memset(entry, 0, sizeof(*entry));
    entry->head_request = head_request;
    entry->connect_request = connect_request;
    copy_bounded(entry->url, sizeof(entry->url), (const char *)url, url_length);

    if (session->host[0]) return;
    for (size_t i = 0; i + 6U < length; i++) {
        if ((i == 0U || payload[i - 1U] == '\n') &&
            strncasecmp((const char *)payload + i, "Host:", 5U) == 0) {
            size_t start = i + 5U;
            while (start < length && (payload[start] == ' ' || payload[start] == '\t')) {
                start++;
            }
            size_t end = start;
            while (end < length && payload[end] != '\r' && payload[end] != '\n') end++;
            copy_bounded(session->host, sizeof(session->host),
                         (const char *)payload + start, end - start);
            return;
        }
    }
}

static int segment_compare(const void *left, const void *right)
{
    const pcap_extract_segment_t *a = left;
    const pcap_extract_segment_t *b = right;
    if (a->session != b->session) return a->session < b->session ? -1 : 1;
    if (a->sequence != b->sequence) return a->sequence < b->sequence ? -1 : 1;
    if (a->packet_number != b->packet_number) {
        return a->packet_number < b->packet_number ? -1 : 1;
    }
    return 0;
}

static uint64_t packet_time_us(const pcap_capture_info_t *info,
                               const pcap_packet_index_t *packet)
{
    uint64_t fraction = info->timestamp_resolution == PCAP_TIMESTAMP_NANOSECONDS
                            ? packet->timestamp_fraction / 1000ULL
                            : packet->timestamp_fraction;
    return (uint64_t)packet->timestamp_seconds * 1000000ULL + fraction;
}

static pcap_extract_status_t discovery_pass(pcap_extract_ctx_t *ctx)
{
    pcap_reader_status_t status = pcap_reader_iterate_begin(ctx->reader);
    if (status != PCAP_READER_OK) return PCAP_EXTRACT_READER_ERROR;

    uint8_t *scan = ctx->frame;
    uint32_t packet_number = 0;
    while (true) {
        if (cancelled(ctx)) return PCAP_EXTRACT_CANCELLED;
        pcap_packet_index_t packet;
        size_t bytes_read = 0;
        bool have_packet = false;
        status = pcap_reader_iterate_next(ctx->reader, &packet, scan,
                                          ctx->frame_capacity < EXTRACT_SCAN_BYTES
                                              ? ctx->frame_capacity : EXTRACT_SCAN_BYTES,
                                          &bytes_read, &have_packet);
        if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) {
            ctx->result->capture_truncated = true;
            break;
        }
        if (!have_packet) break;

        uint32_t index = packet_number++;
        ctx->result->scanned_packets++;
        if ((index % 512U) == 0U && ctx->progress) {
            ctx->progress("scan", (uint32_t)(packet.data_offset / 1024U),
                          (uint32_t)(ctx->info.file_size / 1024U), ctx->progress_ctx);
        }

        pcap_packet_details_t details;
        pcap_reader_describe_bytes(ctx->info.link_type, scan, bytes_read,
                                   packet.captured_length < packet.original_length,
                                   &details);
        if ((details.flags & PCAP_PACKET_FLAG_TCP) == 0U || details.payload_offset == 0U ||
            details.payload_offset >= packet.captured_length) {
            continue;
        }
        uint32_t payload_length = packet.captured_length - details.payload_offset;
        if (payload_length == 0U) continue;
        if (details.payload_offset >= bytes_read) continue;
        const uint8_t *payload = scan + details.payload_offset;
        size_t visible = bytes_read - details.payload_offset;
        ctx->result->payload_packets++;

        if (visible >= 6U && payload[0] == 0x16U && payload[1] == 0x03U &&
            payload[5] == 0x01U) {
            ctx->result->encrypted_flows++;
        }

        uint64_t key = hash_tuple(details.source, details.source_port,
                                  details.destination, details.destination_port);
        pcap_extract_session_t *session = NULL;
        bool server_to_client = false;
        for (uint32_t i = 0; i < ctx->result->session_count; i++) {
            if (ctx->sessions[i].key_forward == key) {
                session = &ctx->sessions[i];
                server_to_client = false;
                break;
            }
            if (ctx->sessions[i].key_reverse == key) {
                session = &ctx->sessions[i];
                server_to_client = true;
                break;
            }
        }

        bool head_request = false;
        bool connect_request = false;
        bool is_request = looks_like_request(payload, visible, &head_request,
                                             &connect_request);
        bool is_response = looks_like_response(payload, visible);

        if (!session) {
            if (!is_request && !is_response) continue;
            if (ctx->result->session_count >= PCAP_EXTRACT_MAX_SESSIONS) {
                ctx->result->session_limited = true;
                continue;
            }
            session = &ctx->sessions[ctx->result->session_count++];
            memset(session, 0, sizeof(*session));
            session->first_packet = index + 1U;
            if (is_response) {
                server_to_client = true;
                snprintf(session->server, sizeof(session->server), "%s", details.source);
                session->server_port = details.source_port;
                snprintf(session->client, sizeof(session->client), "%s",
                         details.destination);
                session->client_port = details.destination_port;
            } else {
                server_to_client = false;
                snprintf(session->client, sizeof(session->client), "%s", details.source);
                session->client_port = details.source_port;
                snprintf(session->server, sizeof(session->server), "%s",
                         details.destination);
                session->server_port = details.destination_port;
            }
            session->key_forward = hash_tuple(session->client, session->client_port,
                                              session->server, session->server_port);
            session->key_reverse = hash_tuple(session->server, session->server_port,
                                              session->client, session->client_port);
        }

        if (!server_to_client) {
            if (is_request) {
                record_request(session, payload, visible, head_request, connect_request);
            }
            continue;
        }

        if (is_response) session->response_seen = true;
        if (!session->base_valid) {
            session->base_sequence = details.tcp_sequence;
            session->base_valid = true;
        }
        if (ctx->segment_count >= PCAP_EXTRACT_MAX_SEGMENTS) {
            ctx->result->segment_limited = true;
            continue;
        }
        if (payload_length > UINT16_MAX || packet.captured_length > UINT16_MAX ||
            details.payload_offset > UINT16_MAX) {
            continue;
        }
        pcap_extract_segment_t *segment = &ctx->segments[ctx->segment_count++];
        segment->data_offset = packet.data_offset;
        segment->time_us = packet_time_us(&ctx->info, &packet);
        segment->packet_number = index;
        segment->sequence = details.tcp_sequence - session->base_sequence;
        segment->payload_offset = (uint16_t)details.payload_offset;
        segment->payload_length = (uint16_t)payload_length;
        segment->captured_length = (uint16_t)packet.captured_length;
        segment->session = (uint16_t)(session - ctx->sessions);
        session->segment_count++;
    }
    if (ctx->progress) {
        ctx->progress("scan", (uint32_t)(ctx->info.file_size / 1024U),
                      (uint32_t)(ctx->info.file_size / 1024U), ctx->progress_ctx);
    }
    return PCAP_EXTRACT_OK;
}

/* ----------------------------------------------------------------- carving */

static pcap_extract_status_t carve_pass(pcap_extract_ctx_t *ctx)
{
    pcap_extract_http_t *http = heap_caps_calloc(1, sizeof(*http),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!http) http = calloc(1, sizeof(*http));
    if (!http) return PCAP_EXTRACT_NO_MEMORY;

    pcap_extract_status_t result = PCAP_EXTRACT_OK;
    uint32_t index = 0;
    while (index < ctx->segment_count) {
        uint16_t session_index = ctx->segments[index].session;
        pcap_extract_session_t *session = &ctx->sessions[session_index];
        uint32_t end = index;
        while (end < ctx->segment_count && ctx->segments[end].session == session_index) {
            end++;
        }
        if (!session->response_seen) {
            index = end;
            continue;
        }

        memset(http, 0, sizeof(*http));
        http->state = HTTP_STATE_RESYNC;
        bool have_expected = false;
        uint32_t expected = 0;

        for (uint32_t i = index; i < end; i++) {
            if (cancelled(ctx)) {
                result = PCAP_EXTRACT_CANCELLED;
                break;
            }
            if (ctx->progress && (i % 64U) == 0U) {
                ctx->progress("carve", i, ctx->segment_count, ctx->progress_ctx);
            }
            pcap_extract_segment_t *segment = &ctx->segments[i];
            uint32_t skip = 0;
            if (have_expected) {
                if ((int32_t)(segment->sequence - expected) < 0) {
                    uint32_t overlap = expected - segment->sequence;
                    if (http->body_open && !http->discard) {
                        ctx->result->objects[http->object_index].retransmissions++;
                    }
                    if (overlap >= segment->payload_length) continue;
                    skip = overlap;
                } else if (segment->sequence != expected) {
                    http_feed_gap(ctx, http, segment->sequence - expected);
                }
            }

            pcap_packet_index_t packet = {
                .data_offset = segment->data_offset,
                .captured_length = segment->captured_length,
                .original_length = segment->captured_length,
            };
            size_t bytes_read = 0;
            pcap_reader_status_t status = pcap_reader_read_packet(
                ctx->reader, &packet, ctx->frame, ctx->frame_capacity, &bytes_read);
            if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) {
                expected = segment->sequence + segment->payload_length;
                have_expected = true;
                continue;
            }
            size_t offset = (size_t)segment->payload_offset + skip;
            if (offset < bytes_read) {
                size_t available = bytes_read - offset;
                size_t wanted = (size_t)segment->payload_length - skip;
                if (available > wanted) available = wanted;
                http_feed(ctx, session, http, ctx->frame + offset, available,
                          segment->packet_number, segment->time_us);
            }
            expected = segment->sequence + segment->payload_length;
            have_expected = true;
            if (http->io_failed) break;
        }

        /* A body delimited by connection close is complete once the stream ends. */
        finish_object(ctx, http,
                      http->state == HTTP_STATE_BODY_TO_CLOSE
                          ? PCAP_EXTRACT_STATE_COMPLETE : PCAP_EXTRACT_STATE_PARTIAL);
        if (result == PCAP_EXTRACT_CANCELLED) break;
        index = end;
    }

    free(http);
    return result;
}

/* ---------------------------------------------------------------- manifest */

static void json_escape(const char *source, char *output, size_t output_size)
{
    size_t written = 0;
    for (size_t i = 0; source && source[i] && written + 2U < output_size; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c == '"' || c == '\\') {
            output[written++] = '\\';
            output[written++] = (char)c;
        } else if (c >= 32U && c < 127U) {
            output[written++] = (char)c;
        } else {
            output[written++] = '.';
        }
    }
    output[written] = '\0';
}

static bool write_manifest(pcap_extract_result_t *result, const char *capture_path)
{
    char temp_path[PCAP_EXTRACT_PATH_MAX];
    if (snprintf(result->manifest_path, sizeof(result->manifest_path),
                 "%s/manifest.json", result->directory) >=
            (int)sizeof(result->manifest_path) ||
        snprintf(temp_path, sizeof(temp_path), "%s.tmp", result->manifest_path) >=
            (int)sizeof(temp_path)) {
        return false;
    }
    FILE *file = fopen(temp_path, "w");
    if (!file) return false;

    char escaped[PCAP_EXTRACT_PATH_MAX];
    json_escape(capture_path, escaped, sizeof(escaped));
    fprintf(file,
            "{\n  \"schema\":\"espshark-objects\",\n  \"schema_version\":1,\n"
            "  \"capture\":\"%s\",\n"
            "  \"scanned_packets\":%lu,\n  \"payload_packets\":%lu,\n"
            "  \"http_responses\":%lu,\n  \"encrypted_flows\":%lu,\n"
            "  \"sessions\":%lu,\n  \"written_bytes\":%llu,\n"
            "  \"gap_bytes_are_zero_filled\":true,\n"
            "  \"limits\":{\"objects\":%s,\"sessions\":%s,\"segments\":%s,"
            "\"budget\":%s,\"capture_truncated\":%s},\n"
            "  \"objects\":[\n",
            escaped,
            (unsigned long)result->scanned_packets,
            (unsigned long)result->payload_packets,
            (unsigned long)result->http_responses,
            (unsigned long)result->encrypted_flows,
            (unsigned long)result->session_count,
            (unsigned long long)result->written_bytes,
            result->object_limited ? "true" : "false",
            result->session_limited ? "true" : "false",
            result->segment_limited ? "true" : "false",
            result->budget_reached ? "true" : "false",
            result->capture_truncated ? "true" : "false");

    for (uint32_t i = 0; i < result->object_count; i++) {
        const pcap_extract_object_t *object = &result->objects[i];
        char name[PCAP_EXTRACT_NAME_MAX * 2U];
        char url[PCAP_EXTRACT_URL_MAX * 2U];
        char host[PCAP_EXTRACT_HOST_MAX * 2U];
        char mime[PCAP_EXTRACT_MIME_MAX * 2U];
        char encoding[sizeof(object->content_encoding) * 2U];
        json_escape(object->name, name, sizeof(name));
        json_escape(object->url, url, sizeof(url));
        json_escape(object->host, host, sizeof(host));
        json_escape(object->declared_mime, mime, sizeof(mime));
        json_escape(object->content_encoding, encoding, sizeof(encoding));
        fprintf(file,
                "    {\"index\":%lu,\"name\":\"%s\",\"saved\":%s,\"type\":\"%s\","
                "\"detected\":\"%s\",\"declared_mime\":\"%s\",\"encoding\":\"%s\","
                "\"status\":%u,\"host\":\"%s\",\"url\":\"%s\","
                "\"client\":\"%s:%u\",\"server\":\"%s:%u\","
                "\"bytes\":%llu,\"declared_bytes\":%llu,\"gap_bytes\":%llu,"
                "\"retransmissions\":%lu,\"chunked\":%s,"
                "\"first_packet\":%lu,\"last_packet\":%lu,"
                "\"state\":\"%s\",\"confidence\":\"%s\",\"sha256\":\"%s\"}%s\n",
                (unsigned long)i + 1UL, name, object->saved ? "true" : "false",
                pcap_extract_type_name(object->type),
                pcap_extract_type_name(object->detected_type), mime,
                encoding, object->http_status, host, url,
                object->client, object->client_port, object->server, object->server_port,
                (unsigned long long)object->bytes,
                (unsigned long long)object->declared_bytes,
                (unsigned long long)object->gap_bytes,
                (unsigned long)object->retransmissions,
                object->chunked ? "true" : "false",
                (unsigned long)object->first_packet, (unsigned long)object->last_packet,
                pcap_extract_state_name(object->state),
                pcap_extract_confidence_name(object->confidence), object->sha256,
                (i + 1U < result->object_count) ? "," : "");
    }
    fprintf(file, "  ]\n}\n");

    bool ok = fflush(file) == 0;
    int descriptor = fileno(file);
    if (descriptor >= 0) fsync(descriptor);
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        unlink(temp_path);
        return false;
    }
    unlink(result->manifest_path);
    return rename(temp_path, result->manifest_path) == 0;
}

/* -------------------------------------------------------------- public API */

pcap_extract_status_t pcap_extract_run(const char *capture_path,
                                       pcap_extract_result_t *result,
                                       const volatile bool *cancel_requested,
                                       pcap_extract_progress_cb_t progress_cb,
                                       void *progress_ctx)
{
    if (!capture_path || !result) return PCAP_EXTRACT_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    char stem[64];
    capture_stem(capture_path, stem, sizeof(stem));
    if (snprintf(result->directory, sizeof(result->directory), "%s/%s",
                 EXTRACT_ROOT, stem) >= (int)sizeof(result->directory)) {
        return PCAP_EXTRACT_INVALID_ARG;
    }
    if (!prepare_directory(result->directory)) {
        ESP_LOGE(TAG, "Could not create %s", result->directory);
        return PCAP_EXTRACT_IO_ERROR;
    }
    clear_directory(result->directory);

    pcap_extract_ctx_t ctx = {
        .result = result,
        .cancel = cancel_requested,
        .progress = progress_cb,
        .progress_ctx = progress_ctx,
    };
    pcap_reader_status_t reader_status = pcap_reader_open(capture_path, &ctx.reader,
                                                          &ctx.info);
    if (reader_status != PCAP_READER_OK) {
        ESP_LOGE(TAG, "Reader rejected %s: %s", capture_path,
                 pcap_reader_status_name(reader_status));
        return PCAP_EXTRACT_READER_ERROR;
    }

    size_t frame_capacity = ctx.info.snaplen;
    if (frame_capacity < EXTRACT_MIN_FRAME) frame_capacity = EXTRACT_MIN_FRAME;
    if (frame_capacity > EXTRACT_MAX_FRAME) frame_capacity = EXTRACT_MAX_FRAME;
    ctx.frame_capacity = frame_capacity;

    ctx.sessions = heap_caps_calloc(PCAP_EXTRACT_MAX_SESSIONS, sizeof(*ctx.sessions),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ctx.segments = heap_caps_calloc(PCAP_EXTRACT_MAX_SEGMENTS, sizeof(*ctx.segments),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ctx.frame = heap_caps_malloc(frame_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx.sessions || !ctx.segments || !ctx.frame) {
        heap_caps_free(ctx.sessions);
        heap_caps_free(ctx.segments);
        heap_caps_free(ctx.frame);
        pcap_reader_close(ctx.reader);
        return PCAP_EXTRACT_NO_MEMORY;
    }

    pcap_extract_status_t status = discovery_pass(&ctx);
    if (status == PCAP_EXTRACT_OK && ctx.segment_count > 0) {
        qsort(ctx.segments, ctx.segment_count, sizeof(*ctx.segments), segment_compare);
        status = carve_pass(&ctx);
    }

    if (status == PCAP_EXTRACT_OK || status == PCAP_EXTRACT_CANCELLED) {
        if (!write_manifest(result, capture_path)) {
            ESP_LOGW(TAG, "Manifest could not be written");
            result->manifest_path[0] = '\0';
        }
    }

    ESP_LOGI(TAG,
             "[ESPShark] Objects: packets=%lu payload=%lu sessions=%lu responses=%lu "
             "objects=%lu bytes=%llu encrypted=%lu%s%s%s",
             (unsigned long)result->scanned_packets,
             (unsigned long)result->payload_packets,
             (unsigned long)result->session_count,
             (unsigned long)result->http_responses,
             (unsigned long)result->object_count,
             (unsigned long long)result->written_bytes,
             (unsigned long)result->encrypted_flows,
             result->object_limited ? " OBJECT-LIMITED" : "",
             result->session_limited ? " SESSION-LIMITED" : "",
             result->segment_limited ? " SEGMENT-LIMITED" : "");

    heap_caps_free(ctx.sessions);
    heap_caps_free(ctx.segments);
    heap_caps_free(ctx.frame);
    pcap_reader_close(ctx.reader);

    if (status == PCAP_EXTRACT_OK && result->object_count == 0) {
        return PCAP_EXTRACT_NOTHING_FOUND;
    }
    return status;
}

pcap_extract_status_t pcap_extract_delete_object(pcap_extract_result_t *result,
                                                 uint32_t object_index)
{
    if (!result || object_index >= result->object_count) return PCAP_EXTRACT_INVALID_ARG;
    pcap_extract_object_t *object = &result->objects[object_index];
    if (object->saved) {
        char path[PCAP_EXTRACT_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", result->directory, object->name) <
            (int)sizeof(path)) {
            unlink(path);
        }
        object->saved = false;
    }
    return PCAP_EXTRACT_OK;
}

pcap_extract_status_t pcap_extract_read_object(const pcap_extract_result_t *result,
                                               uint32_t object_index,
                                               uint8_t *buffer, size_t buffer_size,
                                               size_t *bytes_read_out)
{
    if (!result || !buffer || buffer_size == 0 || !bytes_read_out ||
        object_index >= result->object_count) {
        return PCAP_EXTRACT_INVALID_ARG;
    }
    *bytes_read_out = 0;
    const pcap_extract_object_t *object = &result->objects[object_index];
    if (!object->saved) return PCAP_EXTRACT_IO_ERROR;
    char path[PCAP_EXTRACT_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", result->directory, object->name) >=
        (int)sizeof(path)) {
        return PCAP_EXTRACT_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (!file) return PCAP_EXTRACT_IO_ERROR;
    *bytes_read_out = fread(buffer, 1, buffer_size, file);
    fclose(file);
    return PCAP_EXTRACT_OK;
}
