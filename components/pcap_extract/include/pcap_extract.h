#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Offline HTTP object extraction.
 *
 * Rebuilds the server-to-client byte stream of plaintext HTTP/1.x conversations
 * found in a classic PCAP and writes every response body it can bound to the SD
 * card as a derived artifact. The source capture is never modified.
 *
 * This is not decryption. TLS, QUIC, SSH and VPN payloads stay encrypted and are
 * reported as metadata only. An object is only as complete as the bytes present
 * in the capture. */

#define PCAP_EXTRACT_MAX_OBJECTS      64U
#define PCAP_EXTRACT_MAX_SESSIONS     48U
#define PCAP_EXTRACT_MAX_SEGMENTS  16384U
#define PCAP_EXTRACT_PENDING_URLS      4U
#define PCAP_EXTRACT_NAME_MAX         72U
#define PCAP_EXTRACT_PATH_MAX        384U
#define PCAP_EXTRACT_URL_MAX          96U
#define PCAP_EXTRACT_HOST_MAX         80U
#define PCAP_EXTRACT_MIME_MAX         48U
#define PCAP_EXTRACT_SHA256_TEXT      65U

/* An object that would exceed the per-object ceiling is finished as TRUNCATED
 * instead of being silently completed. The run ceiling bounds how much a single
 * extraction may write to the SD card in total. */
#define PCAP_EXTRACT_OBJECT_BYTE_LIMIT   (4U * 1024U * 1024U)
#define PCAP_EXTRACT_RUN_BYTE_LIMIT      (32U * 1024U * 1024U)
#define PCAP_EXTRACT_MAX_GAP_FILL        (256U * 1024U)

typedef enum {
    PCAP_EXTRACT_OK = 0,
    PCAP_EXTRACT_INVALID_ARG,
    PCAP_EXTRACT_NO_MEMORY,
    PCAP_EXTRACT_IO_ERROR,
    PCAP_EXTRACT_READER_ERROR,
    PCAP_EXTRACT_CANCELLED,
    PCAP_EXTRACT_NOTHING_FOUND,
} pcap_extract_status_t;

typedef enum {
    PCAP_EXTRACT_TYPE_UNKNOWN = 0,
    PCAP_EXTRACT_TYPE_TEXT,
    PCAP_EXTRACT_TYPE_HTML,
    PCAP_EXTRACT_TYPE_JSON,
    PCAP_EXTRACT_TYPE_XML,
    PCAP_EXTRACT_TYPE_JPEG,
    PCAP_EXTRACT_TYPE_PNG,
    PCAP_EXTRACT_TYPE_GIF,
    PCAP_EXTRACT_TYPE_WEBP,
    PCAP_EXTRACT_TYPE_PDF,
    PCAP_EXTRACT_TYPE_ZIP,
    PCAP_EXTRACT_TYPE_GZIP,
    PCAP_EXTRACT_TYPE_COUNT,
} pcap_extract_type_t;

typedef enum {
    PCAP_EXTRACT_STATE_COMPLETE = 0, /* every declared body byte was present */
    PCAP_EXTRACT_STATE_PARTIAL,      /* stream ended before the body did */
    PCAP_EXTRACT_STATE_GAPS,         /* missing TCP bytes were zero-filled */
    PCAP_EXTRACT_STATE_TRUNCATED,    /* stopped at the per-object byte limit */
} pcap_extract_state_t;

typedef enum {
    PCAP_EXTRACT_CONFIDENCE_LOW = 0,  /* neither MIME nor signature is usable */
    PCAP_EXTRACT_CONFIDENCE_MEDIUM,   /* only one of the two identifies the type */
    PCAP_EXTRACT_CONFIDENCE_HIGH,     /* declared MIME and signature agree */
    PCAP_EXTRACT_CONFIDENCE_MISMATCH, /* declared MIME and signature disagree */
} pcap_extract_confidence_t;

typedef struct {
    char name[PCAP_EXTRACT_NAME_MAX];
    char url[PCAP_EXTRACT_URL_MAX];
    char host[PCAP_EXTRACT_HOST_MAX];
    char declared_mime[PCAP_EXTRACT_MIME_MAX];
    char content_encoding[24];
    char sha256[PCAP_EXTRACT_SHA256_TEXT];
    char client[64];
    char server[64];
    uint64_t bytes;
    uint64_t declared_bytes; /* 0 when the response had no Content-Length */
    uint64_t gap_bytes;
    uint64_t first_time_us;
    uint64_t last_time_us;
    uint32_t first_packet;
    uint32_t last_packet;
    uint32_t retransmissions;
    uint16_t client_port;
    uint16_t server_port;
    uint16_t http_status;
    uint8_t type;          /* pcap_extract_type_t after MIME and signature */
    uint8_t detected_type; /* pcap_extract_type_t from the signature alone */
    uint8_t state;         /* pcap_extract_state_t */
    uint8_t confidence;    /* pcap_extract_confidence_t */
    bool chunked;
    bool encoded; /* Content-Encoding present; bytes are stored compressed */
    bool saved;
} pcap_extract_object_t;

typedef struct {
    uint32_t object_count;
    uint32_t session_count;
    uint32_t http_responses;
    uint32_t encrypted_flows; /* TLS/QUIC conversations that cannot be carved */
    uint32_t scanned_packets;
    uint32_t payload_packets;
    uint64_t written_bytes;
    bool session_limited;
    bool object_limited;
    bool segment_limited;
    bool budget_reached;
    bool capture_truncated;
    char directory[PCAP_EXTRACT_PATH_MAX];
    char manifest_path[PCAP_EXTRACT_PATH_MAX];
    pcap_extract_object_t objects[PCAP_EXTRACT_MAX_OBJECTS];
} pcap_extract_result_t;

/* phase is a short constant label such as "scan" or "carve". */
typedef void (*pcap_extract_progress_cb_t)(const char *phase, uint32_t done,
                                           uint32_t total, void *user_ctx);

/* Runs discovery and carving over the whole capture. The result structure is
 * large (~70 KB) and should be allocated in PSRAM by the caller.
 *
 * Extracted files land in /sdcard/lab/espshark/objects/<capture>/ together with
 * a manifest.json. Any previous content of that directory is replaced. */
pcap_extract_status_t pcap_extract_run(const char *capture_path,
                                       pcap_extract_result_t *result,
                                       const volatile bool *cancel_requested,
                                       pcap_extract_progress_cb_t progress_cb,
                                       void *progress_ctx);

/* Removes one extracted copy and rewrites the manifest. The capture is never
 * touched. */
pcap_extract_status_t pcap_extract_delete_object(pcap_extract_result_t *result,
                                                 uint32_t object_index);

/* Reads up to buffer_size-1 bytes of an extracted object for a safe preview.
 * Bytes are returned verbatim; the caller is responsible for rendering them
 * without interpreting the content. */
pcap_extract_status_t pcap_extract_read_object(const pcap_extract_result_t *result,
                                               uint32_t object_index,
                                               uint8_t *buffer, size_t buffer_size,
                                               size_t *bytes_read_out);

const char *pcap_extract_status_name(pcap_extract_status_t status);
const char *pcap_extract_type_name(uint8_t type);
const char *pcap_extract_type_extension(uint8_t type);
const char *pcap_extract_state_name(uint8_t state);
const char *pcap_extract_confidence_name(uint8_t confidence);
bool pcap_extract_type_is_text(uint8_t type);

#ifdef __cplusplus
}
#endif
