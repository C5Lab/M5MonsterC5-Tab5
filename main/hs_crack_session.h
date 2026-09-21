#ifndef HS_CRACK_SESSION_H
#define HS_CRACK_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_SESSION_SCHEMA_VERSION 1U
#define HS_SESSION_MAX_WIRE_BYTES 65536U
#define HS_SESSION_WIRE_HEADER_BYTES 48U
#define HS_SESSION_WIRE_TRAILER_BYTES 8U
#define HS_SESSION_WIRE_PAYLOAD_LENGTH_OFFSET 8U
#define HS_SESSION_WIRE_PAYLOAD_CRC_OFFSET 40U
#define HS_SESSION_WIRE_HEADER_CRC_OFFSET 44U

#define HS_SESSION_ID_BYTES 16U
#define HS_SESSION_SSID_MAX 32U
#define HS_SESSION_PATH_MAX 192U
#define HS_SESSION_JOB_ID_MAX 64U
#define HS_SESSION_PASSWORD_MAX 64U
#define HS_SESSION_MAX_WORDLISTS 8U
#define HS_SESSION_MAX_SHARDS 8U
#define HS_SESSION_MAX_WORKERS 3U

typedef enum {
    HS_SESSION_OK = 0,
    HS_SESSION_NOT_FOUND,
    HS_SESSION_IO_ERROR,
    HS_SESSION_NO_SPACE,
    HS_SESSION_RANGE,
    HS_SESSION_CORRUPT,
    HS_SESSION_UNSUPPORTED,
    HS_SESSION_MISMATCH,
} hs_session_result_t;

typedef enum {
    HS_SESSION_ACTIVE = 1,
    HS_SESSION_FINALIZING = 2,
    HS_SESSION_TOMBSTONE = 3,
} hs_session_state_t;

typedef enum {
    HS_SESSION_SOURCE_LOCAL = 0,
    HS_SESSION_SOURCE_GROVE = 1,
    HS_SESSION_SOURCE_USB = 2,
    HS_SESSION_SOURCE_MBUS = 3,
} hs_session_source_t;

typedef enum {
    HS_SESSION_PHASE_GENERIC = 0,
    HS_SESSION_PHASE_WORDLIST = 1,
    HS_SESSION_PHASE_FINALIZING = 2,
} hs_session_phase_kind_t;

typedef enum {
    HS_SESSION_SHARD_LOCAL = 0,
    HS_SESSION_SHARD_REMOTE = 1,
} hs_session_shard_kind_t;

typedef enum {
    HS_SESSION_SHARD_PENDING = 0,
    HS_SESSION_SHARD_STARTING = 1,
    HS_SESSION_SHARD_LEASED = 2,
    HS_SESSION_SHARD_RECOVERING = 3,
    HS_SESSION_SHARD_COMPLETE = 4,
} hs_session_shard_state_t;

typedef enum {
    HS_SESSION_WORKER_IDLE = 0,
    HS_SESSION_WORKER_STARTING = 1,
    HS_SESSION_WORKER_ACTIVE = 2,
    HS_SESSION_WORKER_RECOVERING = 3,
    HS_SESSION_WORKER_LOST = 4,
} hs_session_worker_state_t;

typedef enum {
    HS_SESSION_OUTCOME_NONE = 0,
    HS_SESSION_OUTCOME_FOUND = 1,
    HS_SESSION_OUTCOME_NOT_FOUND = 2,
    HS_SESSION_OUTCOME_INTERRUPTED = 3,
    HS_SESSION_OUTCOME_STALE = 4,
    HS_SESSION_OUTCOME_ERROR = 5,
} hs_session_outcome_t;

typedef enum {
    HS_SESSION_SLOT_NONE = 0,
    HS_SESSION_SLOT_A = 1,
    HS_SESSION_SLOT_B = 2,
} hs_session_slot_t;

typedef struct {
    uint64_t size;
    uint32_t crc32;
    uint32_t validator_version;
    uint32_t validation_state;
    uint32_t validation_reason;
    uint32_t record_count;
    uint32_t packet_count;
    uint32_t eapol_count;
    uint32_t ap_nonce_count;
    uint32_t sta_response_count;
    uint8_t ssid_length;
    uint8_t ssid[HS_SESSION_SSID_MAX];
    uint8_t bssid[6];
} hs_session_capture_t;

typedef struct {
    hs_session_source_t source;
    char local_path[HS_SESSION_PATH_MAX];
    char remote_path[HS_SESSION_PATH_MAX];
} hs_session_origin_t;

typedef struct {
    uint8_t method;
    uint8_t selected_source;
    bool sync_capture;
    bool sync_wordlists;
    bool force_rerun;
    uint8_t requested_workers;
} hs_session_config_t;

typedef struct {
    char path[HS_SESSION_PATH_MAX];
    uint64_t size;
    uint64_t mtime;
    uint32_t head_crc32;
    uint32_t tail_crc32;
    uint32_t full_crc32;
    bool full_crc_valid;
} hs_session_wordlist_t;

typedef struct {
    hs_session_phase_kind_t kind;
    uint16_t wordlist_index;
    uint32_t generic_cursor;
} hs_session_phase_t;

typedef struct {
    uint8_t shard_id;
    hs_session_shard_kind_t kind;
    hs_session_shard_state_t state;
    int8_t owner;
    int8_t previous_owner;
    uint64_t range_start;
    uint64_t range_end;
    uint64_t safe_offset;
    uint64_t accounted_checked;
    uint32_t generation;
    char job_id[HS_SESSION_JOB_ID_MAX];
    char retired_job_id[HS_SESSION_JOB_ID_MAX];
} hs_session_shard_t;

typedef struct {
    hs_session_source_t transport;
    hs_session_worker_state_t state;
    uint8_t miss_count;
    uint8_t recovery_attempt;
    uint32_t recovery_delay_ms;
    uint64_t safe_offset;
    uint64_t checked;
    uint64_t rate_milli_per_second;
    uint64_t capture_size;
    uint32_t capture_crc32;
    uint64_t wordlist_size;
    uint32_t wordlist_crc32;
    char job_id[HS_SESSION_JOB_ID_MAX];
    char retired_job_id[HS_SESSION_JOB_ID_MAX];
} hs_session_worker_t;

typedef struct {
    uint64_t total_tried;
    uint64_t active_time_ms;
    uint64_t last_rate_milli_per_second;
    uint64_t last_eta_seconds;
    uint64_t last_checkpoint_sequence;
    uint32_t resume_count;
} hs_session_metrics_t;

typedef struct {
    hs_session_outcome_t outcome;
    uint32_t verified_record_index;
    int32_t reason;
    uint8_t password_length;
    uint8_t password[HS_SESSION_PASSWORD_MAX];
} hs_session_terminal_t;

typedef struct {
    uint16_t schema_version;
    uint64_t sequence;
    uint8_t session_id[HS_SESSION_ID_BYTES];
    hs_session_state_t state;
    hs_session_capture_t capture;
    hs_session_origin_t origin;
    hs_session_config_t config;
    hs_session_wordlist_t wordlists[HS_SESSION_MAX_WORDLISTS];
    size_t wordlist_count;
    hs_session_phase_t phase;
    hs_session_shard_t shards[HS_SESSION_MAX_SHARDS];
    size_t shard_count;
    hs_session_worker_t workers[HS_SESSION_MAX_WORKERS];
    size_t worker_count;
    hs_session_metrics_t metrics;
    bool has_result;
    hs_session_terminal_t result;
} hs_session_t;

hs_session_result_t hs_session_encode(const hs_session_t *session, uint8_t *output,
                                      size_t capacity, size_t *length_out);
hs_session_result_t hs_session_decode(const uint8_t *wire, size_t length,
                                      hs_session_t *session_out);
hs_session_result_t hs_session_load_latest(const char *slot_a, const char *slot_b,
                                           hs_session_t *session_out,
                                           hs_session_slot_t *slot_out);
hs_session_result_t hs_session_save_next(const char *slot_a, const char *slot_b,
                                         hs_session_t *session);
hs_session_result_t hs_session_tombstone(const char *slot_a, const char *slot_b,
                                         const uint8_t session_id[HS_SESSION_ID_BYTES]);

#endif
