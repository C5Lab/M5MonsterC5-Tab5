#include "hs_artifact_inventory.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ARTIFACT_PREFIX "[ARTIFACT/1] "
#define ARTIFACT_MAX_FIELDS 16U

typedef struct {
    char *key;
    char *value;
} artifact_field_t;

static bool copy_text(char *destination, size_t capacity, const char *source)
{
    size_t length = source ? strlen(source) : 0U;
    if (!destination || capacity == 0U || !source || length >= capacity)
        return false;
    memcpy(destination, source, length + 1U);
    return true;
}

static bool parse_u64(const char *text, uint64_t *value)
{
    if (!text || !*text || !value || !isdigit((unsigned char)*text))
        return false;
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || !end || *end != '\0')
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_u32(const char *text, uint32_t *value)
{
    uint64_t parsed;
    if (!parse_u64(text, &parsed) || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool valid_request_id(const char *text, bool allow_zero)
{
    size_t length = text ? strlen(text) : 0U;
    if (allow_zero && length == 1U && text[0] == '0')
        return true;
    if (length == 0U || length > HS_ARTIFACT_REQUEST_ID_MAX)
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!isalnum(c) && c != '_' && c != '-')
            return false;
    }
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool decode_name(const char *hex, hs_artifact_message_t *message)
{
    size_t length = hex ? strlen(hex) : 0U;
    if (length == 0U || (length & 1U) != 0U || length / 2U > HS_ARTIFACT_NAME_MAX)
        return false;
    message->name_length = length / 2U;
    for (size_t i = 0; i < message->name_length; ++i) {
        int high = hex_nibble(hex[i * 2U]);
        int low = hex_nibble(hex[i * 2U + 1U]);
        if (high < 0 || low < 0)
            return false;
        message->name[i] = (uint8_t)((high << 4) | low);
        if (message->name[i] == 0U)
            return false;
    }
    message->name[message->name_length] = 0U;
    return true;
}

static const char *field_value(const artifact_field_t *fields, size_t count,
                               const char *key)
{
    for (size_t i = 0; i < count; ++i)
        if (strcmp(fields[i].key, key) == 0)
            return fields[i].value;
    return NULL;
}

static bool only_fields(const artifact_field_t *fields, size_t count,
                        const char *const *allowed, size_t allowed_count)
{
    for (size_t i = 0; i < count; ++i) {
        bool found = false;
        for (size_t j = 0; j < allowed_count; ++j)
            if (strcmp(fields[i].key, allowed[j]) == 0) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return true;
}

static bool parse_common(const artifact_field_t *fields, size_t count,
                         hs_artifact_message_t *message, bool allow_zero)
{
    const char *request_id = field_value(fields, count, "req");
    const char *snapshot = field_value(fields, count, "snapshot");
    return request_id && snapshot && valid_request_id(request_id, allow_zero) &&
           copy_text(message->request_id, sizeof(message->request_id), request_id) &&
           parse_u32(snapshot, &message->snapshot);
}

static bool parse_format(const char *text, hs_artifact_format_t *format)
{
    if (!strcmp(text, "hccapx"))
        *format = HS_ARTIFACT_FORMAT_HCCAPX;
    else if (!strcmp(text, "pcap"))
        *format = HS_ARTIFACT_FORMAT_PCAP;
    else if (!strcmp(text, "unknown"))
        *format = HS_ARTIFACT_FORMAT_UNKNOWN;
    else
        return false;
    return true;
}

static bool parse_validation(const char *text, hs_artifact_validation_t *validation)
{
    if (!strcmp(text, "valid"))
        *validation = HS_ARTIFACT_VALIDATION_VALID;
    else if (!strcmp(text, "invalid"))
        *validation = HS_ARTIFACT_VALIDATION_INVALID;
    else if (!strcmp(text, "unknown"))
        *validation = HS_ARTIFACT_VALIDATION_UNKNOWN;
    else
        return false;
    return true;
}

static bool parse_crc32(const char *text, bool *known, uint32_t *crc32)
{
    if (!strcmp(text, "none")) {
        *known = false;
        *crc32 = 0U;
        return true;
    }
    if (strlen(text) != 8U)
        return false;
    uint32_t value = 0U;
    for (size_t i = 0; i < 8U; ++i) {
        int nibble = hex_nibble(text[i]);
        if (nibble < 0)
            return false;
        value = (value << 4U) | (uint32_t)nibble;
    }
    *known = true;
    *crc32 = value;
    return true;
}

static bool parse_capabilities(const artifact_field_t *fields, size_t count,
                               hs_artifact_message_t *message)
{
    static const char *const allowed[] = {
        "req", "snapshot", "artifact_inventory", "scopes", "page_max",
        "entries_max", "name_max", "line_max", "inspect_max", "validator",
    };
    const char *feature = field_value(fields, count, "artifact_inventory");
    const char *scopes = field_value(fields, count, "scopes");
    const char *validator = field_value(fields, count, "validator");
    return count == sizeof(allowed) / sizeof(allowed[0]) &&
           only_fields(fields, count, allowed, sizeof(allowed) / sizeof(allowed[0])) &&
           parse_common(fields, count, message, true) &&
           !strcmp(message->request_id, "0") && message->snapshot == 0U &&
           feature && !strcmp(feature, "1") && scopes && !strcmp(scopes, "handshakes,pcaps") &&
           validator && !strcmp(validator, "hccapx_v1") &&
           parse_u32(field_value(fields, count, "page_max"), &message->page_max) &&
           parse_u32(field_value(fields, count, "entries_max"), &message->entries_max) &&
           parse_u32(field_value(fields, count, "name_max"), &message->name_max) &&
           parse_u32(field_value(fields, count, "line_max"), &message->line_max) &&
           parse_u64(field_value(fields, count, "inspect_max"), &message->inspect_max) &&
           message->page_max > 0U && message->page_max <= HS_ARTIFACT_PAGE_MAX &&
           message->entries_max > 0U && message->entries_max <= HS_ARTIFACT_ENTRIES_MAX &&
           message->name_max <= HS_ARTIFACT_NAME_MAX && message->line_max <= HS_ARTIFACT_LINE_MAX;
}

static bool parse_begin(const artifact_field_t *fields, size_t count,
                        hs_artifact_message_t *message)
{
    static const char *const allowed[] = {"req", "snapshot", "scope", "cursor", "limit"};
    const char *scope = field_value(fields, count, "scope");
    if (count != 5U || !only_fields(fields, count, allowed, 5U) ||
        !parse_common(fields, count, message, false) || message->snapshot == 0U || !scope)
        return false;
    if (!strcmp(scope, "handshakes"))
        message->scope = HS_ARTIFACT_SCOPE_HANDSHAKES;
    else if (!strcmp(scope, "pcaps"))
        message->scope = HS_ARTIFACT_SCOPE_PCAPS;
    else
        return false;
    return
           parse_u32(field_value(fields, count, "cursor"), &message->cursor) &&
           parse_u32(field_value(fields, count, "limit"), &message->limit) &&
           message->cursor <= HS_ARTIFACT_ENTRIES_MAX && message->limit > 0U &&
           message->limit <= HS_ARTIFACT_PAGE_MAX;
}

static bool parse_item(const artifact_field_t *fields, size_t count,
                       hs_artifact_message_t *message)
{
    static const char *const allowed[] = {
        "req", "snapshot", "entry", "name_hex", "size", "mtime",
        "format", "validation", "reason",
    };
    const char *reason = field_value(fields, count, "reason");
    return count == 9U && only_fields(fields, count, allowed, 9U) &&
           parse_common(fields, count, message, false) && message->snapshot != 0U &&
           parse_u32(field_value(fields, count, "entry"), &message->entry) && message->entry > 0U &&
           message->entry <= HS_ARTIFACT_ENTRIES_MAX &&
           decode_name(field_value(fields, count, "name_hex"), message) &&
           parse_u64(field_value(fields, count, "size"), &message->size) &&
           parse_u64(field_value(fields, count, "mtime"), &message->mtime) &&
           parse_format(field_value(fields, count, "format"), &message->format) &&
           parse_validation(field_value(fields, count, "validation"), &message->validation) &&
           copy_text(message->reason, sizeof(message->reason), reason);
}

static bool parse_accepted(const artifact_field_t *fields, size_t count,
                           hs_artifact_message_t *message)
{
    static const char *const allowed[] = {"req", "snapshot", "entry"};
    return count == 3U && only_fields(fields, count, allowed, 3U) &&
           parse_common(fields, count, message, false) && message->snapshot != 0U &&
           parse_u32(field_value(fields, count, "entry"), &message->entry) && message->entry > 0U &&
           message->entry <= HS_ARTIFACT_ENTRIES_MAX;
}

static bool parse_progress(const artifact_field_t *fields, size_t count,
                           hs_artifact_message_t *message)
{
    static const char *const allowed[] = {"req", "snapshot", "entry", "bytes", "total"};
    return count == 5U && only_fields(fields, count, allowed, 5U) &&
           parse_common(fields, count, message, false) && message->snapshot != 0U &&
           parse_u32(field_value(fields, count, "entry"), &message->entry) && message->entry > 0U &&
           message->entry <= HS_ARTIFACT_ENTRIES_MAX &&
           parse_u64(field_value(fields, count, "bytes"), &message->progress_bytes) &&
           parse_u64(field_value(fields, count, "total"), &message->progress_total) &&
           message->progress_bytes <= message->progress_total;
}

static bool parse_result(const artifact_field_t *fields, size_t count,
                         hs_artifact_message_t *message)
{
    static const char *const allowed[] = {
        "req", "snapshot", "entry", "validation", "reason", "crc32", "bytes",
    };
    const char *reason = field_value(fields, count, "reason");
    return count == 7U && only_fields(fields, count, allowed, 7U) &&
           parse_common(fields, count, message, false) && message->snapshot != 0U &&
           parse_u32(field_value(fields, count, "entry"), &message->entry) && message->entry > 0U &&
           message->entry <= HS_ARTIFACT_ENTRIES_MAX &&
           parse_validation(field_value(fields, count, "validation"), &message->validation) &&
           copy_text(message->reason, sizeof(message->reason), reason) &&
           parse_crc32(field_value(fields, count, "crc32"), &message->crc32_known,
                       &message->crc32) &&
           parse_u64(field_value(fields, count, "bytes"), &message->inspected_bytes);
}

static bool parse_end(const artifact_field_t *fields, size_t count,
                      hs_artifact_message_t *message)
{
    static const char *const allowed[] = {
        "req", "snapshot", "status", "reason", "next", "more", "count",
    };
    const char *status = field_value(fields, count, "status");
    const char *reason = field_value(fields, count, "reason");
    const char *more = field_value(fields, count, "more");
    if (count != 7U || !only_fields(fields, count, allowed, 7U) ||
        !parse_common(fields, count, message, true) ||
        !status || !reason || !more ||
        !copy_text(message->reason, sizeof(message->reason), reason) ||
        !parse_u32(field_value(fields, count, "next"), &message->next_cursor) ||
        !parse_u32(field_value(fields, count, "count"), &message->count))
        return false;
    if (!strcmp(status, "ok"))
        message->end_status = HS_ARTIFACT_END_OK;
    else if (!strcmp(status, "error"))
        message->end_status = HS_ARTIFACT_END_ERROR;
    else if (!strcmp(status, "cancelled"))
        message->end_status = HS_ARTIFACT_END_CANCELLED;
    else
        return false;
    if ((!strcmp(message->request_id, "0") && message->snapshot != 0U) ||
        (strcmp(message->request_id, "0") && message->snapshot == 0U &&
         message->end_status != HS_ARTIFACT_END_ERROR))
        return false;
    if (!strcmp(more, "0"))
        message->more = false;
    else if (!strcmp(more, "1"))
        message->more = true;
    else
        return false;
    return message->count <= HS_ARTIFACT_PAGE_MAX &&
           message->next_cursor <= HS_ARTIFACT_ENTRIES_MAX &&
           (!message->more || message->next_cursor > 0U);
}

hs_artifact_parse_result_t hs_artifact_parse_line(const char *line,
                                                   hs_artifact_message_t *message)
{
    if (!line || !message)
        return HS_ARTIFACT_PARSE_MALFORMED;
    memset(message, 0, sizeof(*message));
    size_t prefix_length = strlen(ARTIFACT_PREFIX);
    if (strncmp(line, ARTIFACT_PREFIX, prefix_length) != 0)
        return HS_ARTIFACT_PARSE_IGNORED;
    size_t line_length = strlen(line);
    if (line_length <= prefix_length || line_length > HS_ARTIFACT_LINE_MAX)
        return HS_ARTIFACT_PARSE_MALFORMED;

    char buffer[HS_ARTIFACT_LINE_MAX + 1U];
    memcpy(buffer, line + prefix_length, line_length - prefix_length + 1U);
    char *cursor = buffer;
    char *kind = cursor;
    char *space = strchr(cursor, ' ');
    if (space) {
        *space = '\0';
        cursor = space + 1;
    } else {
        cursor += strlen(cursor);
    }
    if (!kind || !*kind)
        return HS_ARTIFACT_PARSE_MALFORMED;

    artifact_field_t fields[ARTIFACT_MAX_FIELDS];
    size_t count = 0U;
    while (*cursor) {
        if (*cursor == ' ')
            return HS_ARTIFACT_PARSE_MALFORMED;
        char *token = cursor;
        space = strchr(cursor, ' ');
        if (space) {
            *space = '\0';
            cursor = space + 1;
        } else {
            cursor += strlen(cursor);
        }
        if (count == ARTIFACT_MAX_FIELDS)
            return HS_ARTIFACT_PARSE_MALFORMED;
        char *equals = strchr(token, '=');
        if (!equals || equals == token || equals[1] == '\0' || strchr(equals + 1, '='))
            return HS_ARTIFACT_PARSE_MALFORMED;
        *equals = '\0';
        for (size_t i = 0; i < count; ++i)
            if (!strcmp(fields[i].key, token))
                return HS_ARTIFACT_PARSE_MALFORMED;
        fields[count].key = token;
        fields[count].value = equals + 1;
        ++count;
    }

    bool valid = false;
    if (!strcmp(kind, "CAPABILITIES")) {
        message->type = HS_ARTIFACT_CAPABILITIES;
        valid = parse_capabilities(fields, count, message);
    } else if (!strcmp(kind, "BEGIN")) {
        message->type = HS_ARTIFACT_BEGIN;
        valid = parse_begin(fields, count, message);
    } else if (!strcmp(kind, "ITEM")) {
        message->type = HS_ARTIFACT_ITEM;
        valid = parse_item(fields, count, message);
    } else if (!strcmp(kind, "ACCEPTED")) {
        message->type = HS_ARTIFACT_ACCEPTED;
        valid = parse_accepted(fields, count, message);
    } else if (!strcmp(kind, "PROGRESS")) {
        message->type = HS_ARTIFACT_PROGRESS;
        valid = parse_progress(fields, count, message);
    } else if (!strcmp(kind, "RESULT")) {
        message->type = HS_ARTIFACT_RESULT;
        valid = parse_result(fields, count, message);
    } else if (!strcmp(kind, "END")) {
        message->type = HS_ARTIFACT_END;
        valid = parse_end(fields, count, message);
    }
    if (!valid) {
        memset(message, 0, sizeof(*message));
        return HS_ARTIFACT_PARSE_MALFORMED;
    }
    return HS_ARTIFACT_PARSE_OK;
}

void hs_artifact_stream_init(hs_artifact_stream_t *parser)
{
    if (parser)
        memset(parser, 0, sizeof(*parser));
}

void hs_artifact_stream_feed(hs_artifact_stream_t *parser, const uint8_t *data,
                             size_t length, hs_artifact_stream_callback_t callback,
                             void *context)
{
    if (!parser || (!data && length != 0U) || !callback)
        return;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (parser->dropping) {
            if (c == '\n') {
                hs_artifact_message_t empty = {0};
                callback(HS_ARTIFACT_PARSE_MALFORMED, &empty, context);
                parser->dropping = false;
                parser->used = 0U;
            }
            continue;
        }
        if (c == '\n') {
            if (parser->used > 0U && parser->line[parser->used - 1U] == '\r')
                --parser->used;
            parser->line[parser->used] = '\0';
            hs_artifact_message_t message;
            hs_artifact_parse_result_t result = hs_artifact_parse_line(parser->line, &message);
            if (result != HS_ARTIFACT_PARSE_IGNORED)
                callback(result, &message, context);
            parser->used = 0U;
        } else if (parser->used == HS_ARTIFACT_LINE_MAX) {
            parser->dropping = true;
            parser->used = 0U;
        } else {
            parser->line[parser->used++] = (char)c;
        }
    }
}

void hs_artifact_request_init(hs_artifact_request_t *request, const char *request_id)
{
    if (!request)
        return;
    memset(request, 0, sizeof(*request));
    if (valid_request_id(request_id, false))
        (void)copy_text(request->request_id, sizeof(request->request_id), request_id);
}

hs_artifact_request_result_t hs_artifact_request_accept(hs_artifact_request_t *request,
                                                        const hs_artifact_message_t *message)
{
    if (!request || !message || request->request_id[0] == '\0' ||
        strcmp(request->request_id, message->request_id) != 0 || request->terminal)
        return HS_ARTIFACT_REQUEST_STALE;
    if (message->type == HS_ARTIFACT_BEGIN || message->type == HS_ARTIFACT_ACCEPTED) {
        if (request->begun)
            return HS_ARTIFACT_REQUEST_STALE;
        request->snapshot = message->snapshot;
        request->begun = true;
        request->listing = message->type == HS_ARTIFACT_BEGIN;
        if (request->listing) {
            request->scope = message->scope;
            request->page_cursor = message->cursor;
            request->page_limit = message->limit;
        }
        return HS_ARTIFACT_REQUEST_ACCEPTED;
    }
    if (message->type == HS_ARTIFACT_END &&
        ((!request->begun && message->snapshot == 0U) ||
         (request->begun && request->snapshot == message->snapshot))) {
        if (request->begun && request->listing &&
            (message->count != request->items_seen ||
             message->next_cursor != request->page_cursor + request->items_seen))
            return HS_ARTIFACT_REQUEST_STALE;
        if (request->begun && !request->listing &&
            (message->count != 0U || message->next_cursor != 0U || message->more))
            return HS_ARTIFACT_REQUEST_STALE;
        request->terminal = true;
        request->more = message->more;
        request->next_cursor = message->next_cursor;
        return HS_ARTIFACT_REQUEST_DONE;
    }
    if (!request->begun || request->snapshot != message->snapshot)
        return HS_ARTIFACT_REQUEST_STALE;
    if (message->type == HS_ARTIFACT_ITEM) {
        if (!request->listing || request->items_seen >= request->page_limit ||
            message->entry != request->page_cursor + request->items_seen + 1U)
            return HS_ARTIFACT_REQUEST_STALE;
        ++request->items_seen;
        return HS_ARTIFACT_REQUEST_ACCEPTED;
    }
    if (!request->listing &&
        (message->type == HS_ARTIFACT_PROGRESS || message->type == HS_ARTIFACT_RESULT))
        return HS_ARTIFACT_REQUEST_ACCEPTED;
    return HS_ARTIFACT_REQUEST_STALE;
}

hs_artifact_request_result_t hs_artifact_request_finish(const hs_artifact_request_t *request)
{
    return request && request->terminal ? HS_ARTIFACT_REQUEST_DONE
                                        : HS_ARTIFACT_REQUEST_INCOMPLETE;
}

bool hs_artifact_parse_legacy_sized_row(const char *line, uint64_t *size_out,
                                        char *name_out, size_t name_capacity)
{
    if (!line || !size_out || !name_out || name_capacity == 0U)
        return false;
    const char *cursor = line;
    uint64_t index;
    char *end = NULL;
    if (!isdigit((unsigned char)*cursor))
        return false;
    errno = 0;
    index = strtoull(cursor, &end, 10);
    if (errno == ERANGE || !end || index == 0U || *end != ' ')
        return false;
    cursor = end + 1;
    uint64_t size;
    if (!isdigit((unsigned char)*cursor))
        return false;
    errno = 0;
    size = strtoull(cursor, &end, 10);
    if (errno == ERANGE || !end || *end != ' ' || end[1] == '\0')
        return false;
    cursor = end + 1;
    size_t name_length = strlen(cursor);
    if (name_length == 0U || name_length > HS_ARTIFACT_NAME_MAX || name_length >= name_capacity)
        return false;
    memcpy(name_out, cursor, name_length + 1U);
    *size_out = size;
    return true;
}

static bool valid_location(const hs_artifact_location_t *location)
{
    if (!location || (int)location->source < (int)HS_ARTIFACT_SOURCE_LOCAL ||
        location->source > HS_ARTIFACT_SOURCE_MBUS ||
        location->scope < HS_ARTIFACT_SCOPE_HANDSHAKES ||
        location->scope > HS_ARTIFACT_SCOPE_PCAPS ||
        location->format > HS_ARTIFACT_FORMAT_PCAP ||
        location->validation > HS_ARTIFACT_VALIDATION_INVALID ||
        memchr(location->reason, '\0', sizeof(location->reason)) == NULL)
        return false;
    return memchr(location->name, '\0', sizeof(location->name)) != NULL &&
           location->name[0] != '\0';
}

hs_artifact_catalog_result_t hs_artifact_catalog_add(hs_artifact_catalog_t *catalog,
                                                     const hs_artifact_location_t *location,
                                                     size_t *asset_index_out)
{
    if (!catalog || !valid_location(location) || catalog->asset_count > HS_ARTIFACT_CATALOG_MAX_ASSETS)
        return HS_ARTIFACT_CATALOG_INVALID;

    size_t existing_asset = SIZE_MAX;
    size_t existing_location = SIZE_MAX;
    for (size_t i = 0; i < catalog->asset_count; ++i) {
        hs_artifact_asset_t *asset = &catalog->assets[i];
        if (asset->location_count > HS_ARTIFACT_CATALOG_MAX_LOCATIONS)
            return HS_ARTIFACT_CATALOG_INVALID;
        for (size_t j = 0; j < asset->location_count; ++j) {
            hs_artifact_location_t *existing = &asset->locations[j];
            bool stable_id = (location->snapshot != 0U || location->entry != 0U) &&
                             existing->scope == location->scope &&
                             existing->snapshot == location->snapshot &&
                             existing->entry == location->entry;
            bool stable_name = existing->scope == location->scope &&
                               strcmp(existing->name, location->name) == 0;
            if (existing->source == location->source && (stable_id || stable_name)) {
                existing_asset = i;
                existing_location = j;
                break;
            }
        }
        if (existing_asset != SIZE_MAX) break;
    }

    if (existing_asset != SIZE_MAX) {
        size_t target = SIZE_MAX;
        if (location->crc32_known) {
            for (size_t i = 0; i < catalog->asset_count && target == SIZE_MAX; ++i) {
                for (size_t j = 0; j < catalog->assets[i].location_count; ++j) {
                    if (i == existing_asset && j == existing_location) continue;
                    const hs_artifact_location_t *candidate = &catalog->assets[i].locations[j];
                    if (candidate->crc32_known && candidate->size == location->size &&
                        candidate->crc32 == location->crc32) {
                        target = i;
                        break;
                    }
                }
            }
        }
        hs_artifact_asset_t *old_asset = &catalog->assets[existing_asset];
        if (target == existing_asset || (target == SIZE_MAX && old_asset->location_count == 1U)) {
            old_asset->locations[existing_location] = *location;
            if (asset_index_out) *asset_index_out = existing_asset;
            return HS_ARTIFACT_CATALOG_UPDATED;
        }
        if (target != SIZE_MAX) {
            if (catalog->assets[target].location_count == HS_ARTIFACT_CATALOG_MAX_LOCATIONS)
                return HS_ARTIFACT_CATALOG_FULL;
            memmove(&old_asset->locations[existing_location],
                    &old_asset->locations[existing_location + 1U],
                    (old_asset->location_count - existing_location - 1U) *
                        sizeof(old_asset->locations[0]));
            --old_asset->location_count;
            if (old_asset->location_count == 0U) {
                if (existing_asset < target) --target;
                memmove(&catalog->assets[existing_asset],
                        &catalog->assets[existing_asset + 1U],
                        (catalog->asset_count - existing_asset - 1U) *
                            sizeof(catalog->assets[0]));
                --catalog->asset_count;
            }
            catalog->assets[target].locations[catalog->assets[target].location_count++] = *location;
            if (asset_index_out) *asset_index_out = target;
            return HS_ARTIFACT_CATALOG_UPDATED;
        }
        if (catalog->asset_count == HS_ARTIFACT_CATALOG_MAX_ASSETS)
            return HS_ARTIFACT_CATALOG_FULL;
        memmove(&old_asset->locations[existing_location],
                &old_asset->locations[existing_location + 1U],
                (old_asset->location_count - existing_location - 1U) *
                    sizeof(old_asset->locations[0]));
        --old_asset->location_count;
        size_t new_index = catalog->asset_count++;
        memset(&catalog->assets[new_index], 0, sizeof(catalog->assets[new_index]));
        catalog->assets[new_index].locations[0] = *location;
        catalog->assets[new_index].location_count = 1U;
        if (asset_index_out) *asset_index_out = new_index;
        return HS_ARTIFACT_CATALOG_UPDATED;
    }

    if (location->crc32_known) {
        for (size_t i = 0; i < catalog->asset_count; ++i) {
            hs_artifact_asset_t *asset = &catalog->assets[i];
            bool exact = false;
            for (size_t j = 0; j < asset->location_count; ++j) {
                const hs_artifact_location_t *existing = &asset->locations[j];
                if (existing->crc32_known && existing->size == location->size &&
                    existing->crc32 == location->crc32) {
                    exact = true;
                    break;
                }
            }
            if (exact) {
                if (asset->location_count == HS_ARTIFACT_CATALOG_MAX_LOCATIONS)
                    return HS_ARTIFACT_CATALOG_FULL;
                asset->locations[asset->location_count++] = *location;
                if (asset_index_out)
                    *asset_index_out = i;
                return HS_ARTIFACT_CATALOG_MERGED;
            }
        }
    }

    if (catalog->asset_count == HS_ARTIFACT_CATALOG_MAX_ASSETS)
        return HS_ARTIFACT_CATALOG_FULL;
    size_t index = catalog->asset_count++;
    memset(&catalog->assets[index], 0, sizeof(catalog->assets[index]));
    catalog->assets[index].locations[0] = *location;
    catalog->assets[index].location_count = 1U;
    if (asset_index_out)
        *asset_index_out = index;
    return HS_ARTIFACT_CATALOG_ADDED;
}

bool hs_artifact_locations_same_content(const hs_artifact_location_t *left,
                                        const hs_artifact_location_t *right)
{
    if (!left || !right || left->size != right->size ||
        left->format != right->format)
        return false;
    if (left->crc32_known && right->crc32_known)
        return left->crc32 == right->crc32;
    return strcmp(left->name, right->name) == 0;
}
