#ifndef HS_ARTIFACT_INVENTORY_H
#define HS_ARTIFACT_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_ARTIFACT_LINE_MAX 1024U
#define HS_ARTIFACT_NAME_MAX 255U
#define HS_ARTIFACT_REQUEST_ID_MAX 32U
#define HS_ARTIFACT_REASON_MAX 31U
#define HS_ARTIFACT_PAGE_MAX 32U
#define HS_ARTIFACT_ENTRIES_MAX 256U
#define HS_ARTIFACT_CATALOG_MAX_ASSETS 64U
#define HS_ARTIFACT_CATALOG_MAX_LOCATIONS 4U

typedef enum {
    HS_ARTIFACT_MESSAGE_NONE = 0,
    HS_ARTIFACT_CAPABILITIES,
    HS_ARTIFACT_BEGIN,
    HS_ARTIFACT_ITEM,
    HS_ARTIFACT_ACCEPTED,
    HS_ARTIFACT_PROGRESS,
    HS_ARTIFACT_RESULT,
    HS_ARTIFACT_END,
} hs_artifact_message_type_t;

typedef enum {
    HS_ARTIFACT_PARSE_OK = 0,
    HS_ARTIFACT_PARSE_IGNORED,
    HS_ARTIFACT_PARSE_MALFORMED,
} hs_artifact_parse_result_t;

typedef enum {
    HS_ARTIFACT_FORMAT_UNKNOWN = 0,
    HS_ARTIFACT_FORMAT_HCCAPX,
    HS_ARTIFACT_FORMAT_PCAP,
} hs_artifact_format_t;

typedef enum {
    HS_ARTIFACT_SCOPE_UNKNOWN = 0,
    HS_ARTIFACT_SCOPE_HANDSHAKES,
    HS_ARTIFACT_SCOPE_PCAPS,
} hs_artifact_scope_t;

typedef enum {
    HS_ARTIFACT_VALIDATION_UNKNOWN = 0,
    HS_ARTIFACT_VALIDATION_VALID,
    HS_ARTIFACT_VALIDATION_INVALID,
} hs_artifact_validation_t;

typedef enum {
    HS_ARTIFACT_END_OK = 0,
    HS_ARTIFACT_END_ERROR,
    HS_ARTIFACT_END_CANCELLED,
} hs_artifact_end_status_t;

typedef struct {
    hs_artifact_message_type_t type;
    char request_id[HS_ARTIFACT_REQUEST_ID_MAX + 1U];
    uint32_t snapshot;
    uint32_t page_max;
    uint32_t entries_max;
    uint32_t name_max;
    uint32_t line_max;
    uint64_t inspect_max;
    uint32_t cursor;
    uint32_t limit;
    hs_artifact_scope_t scope;
    uint32_t entry;
    uint64_t size;
    uint64_t mtime;
    uint8_t name[HS_ARTIFACT_NAME_MAX + 1U];
    size_t name_length;
    hs_artifact_format_t format;
    hs_artifact_validation_t validation;
    char reason[HS_ARTIFACT_REASON_MAX + 1U];
    bool crc32_known;
    uint32_t crc32;
    uint64_t inspected_bytes;
    uint64_t progress_bytes;
    uint64_t progress_total;
    hs_artifact_end_status_t end_status;
    uint32_t next_cursor;
    uint32_t count;
    bool more;
} hs_artifact_message_t;

typedef void (*hs_artifact_stream_callback_t)(hs_artifact_parse_result_t result,
                                               const hs_artifact_message_t *message,
                                               void *context);

typedef struct {
    char line[HS_ARTIFACT_LINE_MAX + 1U];
    size_t used;
    bool dropping;
} hs_artifact_stream_t;

typedef enum {
    HS_ARTIFACT_REQUEST_ACCEPTED = 0,
    HS_ARTIFACT_REQUEST_STALE,
    HS_ARTIFACT_REQUEST_INCOMPLETE,
    HS_ARTIFACT_REQUEST_DONE,
} hs_artifact_request_result_t;

typedef struct {
    char request_id[HS_ARTIFACT_REQUEST_ID_MAX + 1U];
    uint32_t snapshot;
    bool begun;
    bool terminal;
    bool listing;
    bool more;
    hs_artifact_scope_t scope;
    uint32_t page_cursor;
    uint32_t page_limit;
    uint32_t items_seen;
    uint32_t next_cursor;
} hs_artifact_request_t;

typedef enum {
    HS_ARTIFACT_SOURCE_LOCAL = 0,
    HS_ARTIFACT_SOURCE_GROVE,
    HS_ARTIFACT_SOURCE_USB,
    HS_ARTIFACT_SOURCE_MBUS,
} hs_artifact_source_t;

typedef struct {
    hs_artifact_source_t source;
    hs_artifact_scope_t scope;
    uint32_t snapshot;
    uint32_t entry;
    uint64_t size;
    uint64_t mtime;
    bool crc32_known;
    uint32_t crc32;
    hs_artifact_format_t format;
    hs_artifact_validation_t validation;
    char reason[HS_ARTIFACT_REASON_MAX + 1U];
    char name[HS_ARTIFACT_NAME_MAX + 1U];
} hs_artifact_location_t;

typedef struct {
    hs_artifact_location_t locations[HS_ARTIFACT_CATALOG_MAX_LOCATIONS];
    size_t location_count;
} hs_artifact_asset_t;

typedef struct {
    hs_artifact_asset_t assets[HS_ARTIFACT_CATALOG_MAX_ASSETS];
    size_t asset_count;
} hs_artifact_catalog_t;

typedef enum {
    HS_ARTIFACT_CATALOG_ADDED = 0,
    HS_ARTIFACT_CATALOG_MERGED,
    HS_ARTIFACT_CATALOG_UPDATED,
    HS_ARTIFACT_CATALOG_FULL,
    HS_ARTIFACT_CATALOG_INVALID,
} hs_artifact_catalog_result_t;

hs_artifact_parse_result_t hs_artifact_parse_line(const char *line,
                                                   hs_artifact_message_t *message);
void hs_artifact_stream_init(hs_artifact_stream_t *parser);
void hs_artifact_stream_feed(hs_artifact_stream_t *parser, const uint8_t *data,
                             size_t length, hs_artifact_stream_callback_t callback,
                             void *context);
void hs_artifact_request_init(hs_artifact_request_t *request, const char *request_id);
hs_artifact_request_result_t hs_artifact_request_accept(hs_artifact_request_t *request,
                                                        const hs_artifact_message_t *message);
hs_artifact_request_result_t hs_artifact_request_finish(const hs_artifact_request_t *request);
bool hs_artifact_parse_legacy_sized_row(const char *line, uint64_t *size_out,
                                        char *name_out, size_t name_capacity);
hs_artifact_catalog_result_t hs_artifact_catalog_add(hs_artifact_catalog_t *catalog,
                                                     const hs_artifact_location_t *location,
                                                     size_t *asset_index_out);

#endif
