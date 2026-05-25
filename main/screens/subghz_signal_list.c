#include "subghz_signal_list.h"
#include "psram_dynarr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    snprintf(dst->name,   sizeof(dst->name),   "%s", src->name);
}

/* Optional UI-side de-duplication: hide byte-for-byte duplicates when the
 * caller passes dedup=true (e.g. mem list from a long capture session). */
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

void subghz_collect_signal_list(subghz_tab_state_t *st, const char *source,
                                int timeout_ms, bool dedup)
{
    if (!st) return;
    if (!source || (strcmp(source, "mem") != 0 && strcmp(source, "sd") != 0)) {
        ESP_LOGE(TAG, "Invalid source '%s' (must be mem|sd)", source ? source : "(null)");
        return;
    }

    int tab_id = subghz_host_current_tab();

    /* Issue command and reset count */
    subghz_host_uart_flush_input(tab_id);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_list %s", source);
    subghz_host_uart_send(cmd);

    st->sigs_count = 0;

    static char rx_buf[UART_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    bool done = false;
    int lines_seen = 0;
    int bytes_seen = 0;
    int fw_count = -1;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    ESP_LOGI(TAG, "Collecting signal list (timeout %d ms, dedup=%d, waiting for %s)",
             timeout_ms, dedup, SUBGHZ_LIST_END_MARKER);

    while (!done && (xTaskGetTickCount() - start) < timeout) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));
        if (len <= 0) continue;
        bytes_seen += len;

        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    lines_seen++;
                    ESP_LOGI(TAG, "RX< %s", line_buf);

                    if (strstr(line_buf, SUBGHZ_LIST_END_MARKER) != NULL) {
                        /* Match end marker only if source matches what we asked for */
                        const char *src_field = strstr(line_buf, "source=");
                        if (src_field && strncmp(src_field + 7, source, strlen(source)) != 0) {
                            ESP_LOGW(TAG, "END marker source mismatch (want=%s got=%s), ignoring",
                                     source, src_field + 7);
                            line_pos = 0;
                            continue;
                        }
                        const char *cnt = strstr(line_buf, "count=");
                        if (cnt) fw_count = atoi(cnt + 6);
                        ESP_LOGI(TAG, "END marker received after %d lines (firmware count=%d, source=%s)",
                                 lines_seen, fw_count, source);
                        done = true;
                    } else {
                        subghz_signal_info_t parsed;
                        bool ok = subghz_parse_signal_line(line_buf, &parsed);
                        if (!ok) {
                            ESP_LOGI(TAG, "  -> parse failed (ignored)");
                        } else if (parsed.kind != SUBGHZ_SIGNAL_KIND_LIST) {
                            ESP_LOGI(TAG, "  -> wrong kind=%d (ignored)", (int)parsed.kind);
                        } else if (parsed.idx <= 0) {
                            ESP_LOGI(TAG, "  -> idx<=0 (ignored)");
                        } else {
                            bool skip = dedup && is_duplicate_of_existing(
                                (subghz_stored_sig_t *)st->sigs, st->sigs_count, &parsed);
                            if (skip) {
                                ESP_LOGI(TAG, "  -> dedup skip idx=%d", parsed.idx);
                            } else if (psram_dynarr_ensure((void **)&st->sigs, &st->sigs_cap,
                                                           st->sigs_count + 1,
                                                           sizeof(subghz_stored_sig_t),
                                                           SUBGHZ_LIST_HARD_CAP)) {
                                fill_signal(&((subghz_stored_sig_t *)st->sigs)[st->sigs_count],
                                            &parsed);
                                st->sigs_count++;
                                ESP_LOGI(TAG, "  -> accepted idx=%d (count=%d)",
                                         parsed.idx, st->sigs_count);
                            } else {
                                ESP_LOGW(TAG, "PSRAM cap reached at %d", st->sigs_count);
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

    if (!done) {
        line_buf[line_pos] = '\0';
        ESP_LOGW(TAG, "Timed out after %d ms without %s (saw %d lines, %d bytes total, partial=\"%s\")",
                 timeout_ms, SUBGHZ_LIST_END_MARKER, lines_seen, bytes_seen, line_buf);
    }

    if (fw_count >= 0 && fw_count != st->sigs_count) {
        ESP_LOGW(TAG, "Count mismatch: firmware reported %d, parser collected %d",
                 fw_count, st->sigs_count);
    }

    ESP_LOGI(TAG, "Collected %d signals (dedup=%d, lines_seen=%d, bytes_seen=%d, firmware count=%d, ended_via=%s)",
             st->sigs_count, dedup, lines_seen, bytes_seen, fw_count,
             done ? "marker" : "timeout");
}

void subghz_signal_list_free(subghz_tab_state_t *st)
{
    if (!st) return;
    psram_dynarr_free((void **)&st->sigs, &st->sigs_cap);
    st->sigs_count = 0;
}
