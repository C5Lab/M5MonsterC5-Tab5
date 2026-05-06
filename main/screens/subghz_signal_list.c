#include "subghz_signal_list.h"
#include "psram_dynarr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "subghz_list";

#define UART_BUF_LEN 256
#define LINE_BUF_LEN 256

static void fill_signal(subghz_stored_sig_t *dst, const subghz_signal_info_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->idx  = src->idx;
    dst->freq = src->freq;
    dst->btn  = src->btn;
    dst->cnt  = src->cnt;
    snprintf(dst->type,   sizeof(dst->type),   "%s", src->type[0]   ? src->type   : "--");
    snprintf(dst->serial, sizeof(dst->serial), "%s", src->serial[0] ? src->serial : "--");
    snprintf(dst->mf,     sizeof(dst->mf),     "%s", src->mf[0]     ? src->mf     : "--");
}

/* Re-running subghz_import re-adds every .sub file, so the firmware-side list
 * grows each time. Hide byte-for-byte duplicates from the UI (storage on JanOS
 * is left intact until the user taps Clear All). */
static bool is_duplicate_of_existing(const subghz_stored_sig_t *sigs, int count,
                                     const subghz_signal_info_t *p)
{
    int freq_x100 = (int)(p->freq * 100.0f + 0.5f);
    const char *p_type   = p->type[0]   ? p->type   : "--";
    const char *p_serial = p->serial[0] ? p->serial : "--";
    const char *p_mf     = p->mf[0]     ? p->mf     : "--";

    for (int j = 0; j < count; j++) {
        const subghz_stored_sig_t *e = &sigs[j];
        int e_freq_x100 = (int)(e->freq * 100.0f + 0.5f);
        if (e_freq_x100 != freq_x100) continue;
        if (e->btn != p->btn) continue;
        if (strcmp(e->type, p_type) != 0) continue;
        if (strcmp(e->serial, p_serial) != 0) continue;
        if (strcmp(e->mf, p_mf) != 0) continue;
        return true;
    }
    return false;
}

void subghz_collect_signal_list(subghz_tab_state_t *st, int timeout_ms, bool dedup)
{
    if (!st) return;

    int tab_id = subghz_host_current_tab();
    st->sigs_count = 0;

    static char rx_buf[UART_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    bool done = false;

    while (!done && (xTaskGetTickCount() - start) < timeout) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';

                    if (strstr(line_buf, SUBGHZ_LIST_END_MARKER) != NULL) {
                        done = true;
                    } else {
                        subghz_signal_info_t parsed;
                        if (subghz_parse_signal_line(line_buf, &parsed) &&
                            parsed.kind == SUBGHZ_SIGNAL_KIND_LIST &&
                            parsed.idx > 0) {
                            bool skip = dedup && is_duplicate_of_existing(
                                (subghz_stored_sig_t *)st->sigs, st->sigs_count, &parsed);
                            if (!skip) {
                                if (psram_dynarr_ensure((void **)&st->sigs, &st->sigs_cap,
                                                        st->sigs_count + 1,
                                                        sizeof(subghz_stored_sig_t),
                                                        SUBGHZ_LIST_HARD_CAP)) {
                                    fill_signal(&((subghz_stored_sig_t *)st->sigs)[st->sigs_count],
                                                &parsed);
                                    st->sigs_count++;
                                } else {
                                    ESP_LOGW(TAG, "PSRAM cap reached at %d", st->sigs_count);
                                }
                            }
                        }
                    }

                    line_pos = 0;
                    if (done) break;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }

    ESP_LOGI(TAG, "Collected %d signals (timeout %d ms, dedup=%d)",
             st->sigs_count, timeout_ms, dedup);
}

int subghz_wait_for_marker(int tab_id, const char *marker,
                           const char *count_prefix, int timeout_ms)
{
    static char rx_buf[UART_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;
    int counted = 0;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    bool done = false;

    while (!done && (xTaskGetTickCount() - start) < timeout) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;

        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    if (strstr(line_buf, marker) != NULL) {
                        done = true;
                    } else if (count_prefix && strstr(line_buf, count_prefix) != NULL) {
                        counted++;
                    }
                    line_pos = 0;
                    if (done) break;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }

    return counted;
}

void subghz_signal_list_free(subghz_tab_state_t *st)
{
    if (!st) return;
    psram_dynarr_free((void **)&st->sigs, &st->sigs_cap);
    st->sigs_count = 0;
}
