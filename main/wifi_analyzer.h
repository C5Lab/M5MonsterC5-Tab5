#ifndef TAB5_WIFI_ANALYZER_H
#define TAB5_WIFI_ANALYZER_H

#include "wifi_analyzer_model.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct wa_session wa_session_t;

/* Host transport hooks execute only in the worker, except connected(). Claim
 * takes the console mutex and shared exclusive owner; release undoes both. */
typedef struct {
    bool (*available)(int tab);     /* read-only UI admission preflight */
    bool (*claim)(int tab);
    void (*release)(int tab);
    bool (*connected)(int tab);
    int (*read)(int tab, void *bytes, size_t length, uint32_t timeout_ms);
    int (*write)(int tab, const void *bytes, size_t length);
    void (*flush)(int tab);
    uint32_t (*baud)(int tab);
} wa_host_hooks_t;

typedef struct {
    wa_band_t band;
    unsigned profile;              /* 0 quick, 1 detailed, 2 passive */
    uint16_t limit;                /* 1..128; default 64 */
    char channels[160];            /* empty: driver's candidate plan */
    uint32_t repeat_ms;            /* zero: one shot; otherwise >=1000 */
} wa_scan_request_t;

typedef struct {
    bool running;
    bool capable;
    bool stale;
    bool needs_recovery;
    uint32_t revision;
    uint64_t age_ms;
    char message[160];
} wa_status_t;

void wa_init(const wa_host_hooks_t *hooks);
wa_session_t *wa_session_get(int tab); /* UI-thread lazy creation; PSRAM only */
bool wa_start(wa_session_t *session, const wa_scan_request_t *request);
void wa_stop(wa_session_t *session);
void wa_leave(int tab);            /* cooperative stop; never deletes worker */
bool wa_busy_tab(int tab);
bool wa_copy_view(wa_session_t *session, wa_snapshot_t *snapshot, wa_status_t *status);

#endif
