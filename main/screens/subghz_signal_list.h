#pragma once

#include "subghz_host.h"
#include "subghz_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common stored-signal record (shared between Transmit and Manage). */
typedef struct subghz_stored_sig {
    int   idx;
    char  type[32];
    float freq;
    char  serial[32];
    int   btn;
    int   cnt;
    char  mf[32];
} subghz_stored_sig_t;

#define SUBGHZ_LIST_HARD_CAP 2048
#define SUBGHZ_LIST_END_MARKER   "[SUBGHZ_LIST_END]"
#define SUBGHZ_IMPORT_END_MARKER "[SUBGHZ_IMPORT_END]"

/* Synchronously collect lines until `marker` is seen or `timeout_ms` elapses.
 * For each `[SUBGHZ_LIST]` line, parses & appends to st->sigs (PSRAM dynarr).
 * Resets st->sigs_count to 0 before starting. Skips duplicates if `dedup` is true. */
void subghz_collect_signal_list(subghz_tab_state_t *st, int timeout_ms, bool dedup);

/* Synchronously wait for `marker` line, counting `[SUBGHZ_IMPORT]` lines.
 * Returns number of imports detected. */
int subghz_wait_for_marker(int tab_id, const char *marker,
                           const char *count_prefix, int timeout_ms);

/* Free the dynarr backing storage (e.g. on full teardown). */
void subghz_signal_list_free(subghz_tab_state_t *st);

#ifdef __cplusplus
}
#endif
