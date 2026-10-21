#ifndef HS_AUDIT_HISTORY_H
#define HS_AUDIT_HISTORY_H

#include "hs_crack_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_AUDIT_HISTORY_SCHEMA_VERSION 1U
#define HS_AUDIT_HISTORY_PATH_MAX 512U
#define HS_AUDIT_HISTORY_MAX_SELECTED_LISTS HS_SESSION_MAX_WORDLISTS

typedef enum {
    HS_AUDIT_HISTORY_OK = 0,
    HS_AUDIT_HISTORY_NOT_FOUND,
    HS_AUDIT_HISTORY_IO_ERROR,
    HS_AUDIT_HISTORY_INVALID,
    HS_AUDIT_HISTORY_RANGE,
} hs_audit_history_result_t;

/* This deliberately contains no recovered password or other secret. */
typedef struct {
    uint16_t schema_version;
    uint8_t session_id[HS_SESSION_ID_BYTES];
    uint64_t episode_sequence;
    bool started_at_available;
    int64_t started_at_unix_seconds;
    bool finished_at_available;
    int64_t finished_at_unix_seconds;
    hs_session_source_t origin_source;
    char origin_local_path[HS_SESSION_PATH_MAX];
    char origin_remote_path[HS_SESSION_PATH_MAX];
    size_t selected_list_count;
    char selected_lists[HS_AUDIT_HISTORY_MAX_SELECTED_LISTS][HS_SESSION_PATH_MAX];
    uint8_t requested_workers;
    uint8_t effective_workers;
    uint64_t elapsed_time_ms;
    uint64_t rate_milli_per_second;
    uint64_t last_eta_seconds;
    uint64_t tried_count;
    uint32_t resume_count;
    hs_session_outcome_t outcome;
    int32_t reason;
} hs_audit_history_record_t;

hs_audit_history_result_t hs_audit_history_from_session(
    const hs_session_t *session, uint64_t episode_sequence,
    bool started_at_available, int64_t started_at_unix_seconds,
    bool finished_at_available, int64_t finished_at_unix_seconds,
    hs_audit_history_record_t *out);

/* The directory must already exist. Calls are serialized by the owner; the
 * same session/episode is idempotent only when every persisted field matches. */
hs_audit_history_result_t hs_audit_history_append(
    const char *directory, const hs_audit_history_record_t *record);

/* Keeps at most capacity newest records in memory while scanning. Corrupt and
 * temporary candidates remain untouched and are omitted from the result. */
hs_audit_history_result_t hs_audit_history_list(
    const char *directory, hs_audit_history_record_t *out, size_t capacity,
    size_t *count_out);

#endif
