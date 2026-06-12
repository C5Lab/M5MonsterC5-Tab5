#pragma once

#include "subghz_host.h"
#include "subghz_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common stored-signal record (shared between SD Signals manage / TX flows). */
typedef struct subghz_stored_sig {
    int   idx;
    char  type[32];
    float freq;
    char  serial[32];
    int   btn;
    int   cnt;
    char  mf[32];
    char  name[64];
} subghz_stored_sig_t;

#define SUBGHZ_LIST_HARD_CAP 2048
#define SUBGHZ_LIST_END_MARKER   "[SUBGHZ_LIST_END]"

/* Synchronously issue `subghz_list <source>` and collect lines until
 * `[SUBGHZ_LIST_END] ... source=<source>` is seen, or `timeout_ms` elapses.
 *
 * For each `[SUBGHZ_LIST]` line, parses & appends to st->sigs (PSRAM dynarr).
 * Resets st->sigs_count to 0 before starting. Skips duplicates if `dedup` is true.
 *
 * `source` must be either "mem" or "sd". The function flushes the UART input
 * for the current tab, sends the command and waits for the matching terminator.
 *
 * The timeout exists purely as a safety net so the LVGL task can recover if
 * the UART link misbehaves; the firmware normally emits the END marker even
 * for an empty list within a few ms. */
void subghz_collect_signal_list(subghz_tab_state_t *st, const char *source,
                                int timeout_ms, bool dedup);

/* Free the dynarr backing storage (e.g. on full teardown). */
void subghz_signal_list_free(subghz_tab_state_t *st);

#ifdef __cplusplus
}
#endif
