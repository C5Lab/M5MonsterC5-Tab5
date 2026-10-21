#ifndef HS_SESSION_CATALOG_H
#define HS_SESSION_CATALOG_H

#include "hs_crack_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_SESSION_CATALOG_PATH_MAX 512U
#define HS_SESSION_CATALOG_UI_LIMIT 8U

typedef struct {
    hs_session_t session;
    int64_t updated_at;
    bool legacy;
} hs_session_catalog_entry_t;

hs_session_result_t hs_session_catalog_save(const char *root,
                                            hs_session_t *session);
hs_session_result_t hs_session_catalog_list(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    hs_session_catalog_entry_t *out, size_t capacity, size_t *count_out);
hs_session_result_t hs_session_catalog_find_id(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    const uint8_t session_id[HS_SESSION_ID_BYTES], hs_session_t *session_out);
hs_session_result_t hs_session_catalog_find_newest_wordlist(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    uint64_t capture_size, uint32_t capture_crc32,
    const hs_session_wordlist_t *wordlists, size_t wordlist_count,
    hs_session_t *session_out, int *wordlist_index_out,
    bool *matching_capture_out);
hs_session_result_t hs_session_catalog_tombstone(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    const uint8_t session_id[HS_SESSION_ID_BYTES]);

#endif
