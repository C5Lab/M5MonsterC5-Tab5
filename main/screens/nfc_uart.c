#include "nfc_host.h"
#include "subghz_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "nfc_uart";

static void nfc_set_live(nfc_tab_state_t *st, const char *status, const char *detail)
{
    if (!st || !st->lock) return;
    if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    snprintf(st->live_status, sizeof(st->live_status), "%s", status ? status : "");
    snprintf(st->live_detail, sizeof(st->live_detail), "%s", detail ? detail : "");
    st->live_dirty = true;
    xSemaphoreGive(st->lock);
}

static void push_line(nfc_tab_state_t *st, const char *line)
{
    if (!st || !line || !line[0]) return;
    nfc_parse_card_line(line, &st->result_card);
    if (st->line_count < NFC_LINE_CAP) {
        snprintf(st->lines[st->line_count], NFC_LINE_LEN, "%s", line);
        st->line_count++;
    }
    if (st->op == NFC_OP_READ) {
        if (strstr(line, "[NFC] present a card"))
            nfc_set_live(st, "Present a card...", "");
        else if (strncmp(line, "[NFC] type: ", 12) == 0)
            nfc_set_live(st, st->result_card.type, "Reading...");
    }
}

static void nfc_reader_task(void *arg)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)arg;
    char rx_buf[256];
    char line_buf[NFC_LINE_LEN];
    int line_pos = 0;
    bool got_end = false;
    int64_t deadline = esp_timer_get_time() + (int64_t)st->timeout_ms * 1000;

    subghz_host_uart_flush_input(st->tab_id);
    subghz_host_uart_send_for_tab(st->tab_id, st->cmd);

    while (!st->cancel && esp_timer_get_time() < deadline) {
        int len = subghz_host_uart_read_bytes(st->tab_id, rx_buf, sizeof(rx_buf) - 1,
                                              pdMS_TO_TICKS(100));
        if (len <= 0) continue;
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    if (nfc_line_is_end(line_buf)) {
                        got_end = true;
                        line_pos = 0;
                        break;
                    }
                    push_line(st, line_buf);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
        if (got_end) break;
    }

    if (!st->cancel) {
        st->result_op = st->op;
        st->result_timeout = !got_end;
        st->result_ready = true;
    }
    ESP_LOGI(TAG, "op %d done end=%d cancel=%d lines=%d",
             (int)st->op, (int)got_end, (int)st->cancel, st->line_count);
    st->task = NULL;
    vTaskDelete(NULL);
}

static void nfc_ui_timer_cb(lv_timer_t *t)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;

    if (st->live_dirty && st->lock &&
        xSemaphoreTake(st->lock, 0) == pdTRUE) {
        char status[80];
        char detail[160];
        snprintf(status, sizeof(status), "%s", st->live_status);
        snprintf(detail, sizeof(detail), "%s", st->live_detail);
        st->live_dirty = false;
        xSemaphoreGive(st->lock);
        if (st->op == NFC_OP_READ || st->result_op == NFC_OP_READ)
            nfc_read_on_live(st, status, detail);
    }

    if (!st->result_ready) return;
    st->result_ready = false;
    st->busy = false;
    nfc_op_t op = st->result_op;
    switch (op) {
    case NFC_OP_INIT:    nfc_hub_on_init(st); break;
    case NFC_OP_READ:    nfc_read_on_result(st); break;
    case NFC_OP_SAVE:    nfc_read_on_save(st); break;
    case NFC_OP_LIST:    nfc_list_on_result(st); break;
    case NFC_OP_LOAD:    nfc_detail_on_load(st); break;
    case NFC_OP_RENAME:  nfc_detail_on_rename(st); break;
    case NFC_OP_DELETE:  nfc_detail_on_delete(st); break;
    case NFC_OP_EMULATE: nfc_emulate_on_result(st); break;
    default: break;
    }
}

static void nfc_ensure_timer(nfc_tab_state_t *st)
{
    if (!st || st->ui_timer) return;
    st->ui_timer = lv_timer_create(nfc_ui_timer_cb, 100, st);
}

nfc_tab_state_t *nfc_host_alloc_state(void)
{
    nfc_tab_state_t *st = heap_caps_calloc(1, sizeof(*st),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!st) {
        ESP_LOGE(TAG, "Failed to allocate nfc_tab_state");
        return NULL;
    }
    st->lock = xSemaphoreCreateMutex();
    st->detail_idx = -1;
    return st;
}

void nfc_uart_cancel(nfc_tab_state_t *st)
{
    if (!st) return;
    if (st->task) {
        st->cancel = true;
        for (int i = 0; i < 40 && st->task; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
        st->cancel = false;
    }
    st->result_ready = false;
    st->live_dirty = false;
    st->busy = false;
    st->op = NFC_OP_NONE;
}

void nfc_uart_start(nfc_tab_state_t *st, nfc_op_t op, const char *cmd, uint32_t timeout_ms)
{
    if (!st || !cmd) return;
    nfc_uart_cancel(st);
    nfc_ensure_timer(st);

    st->op = op;
    st->timeout_ms = timeout_ms ? timeout_ms : 5000;
    st->busy = true;
    st->result_ready = false;
    st->result_timeout = false;
    st->result_op = NFC_OP_NONE;
    st->line_count = 0;
    st->cancel = false;
    nfc_card_reset(&st->result_card);
    snprintf(st->cmd, sizeof(st->cmd), "%s", cmd);

    if (xTaskCreate(nfc_reader_task, "nfc_rx", 5120, st, 5, &st->task) != pdPASS) {
        st->task = NULL;
        st->busy = false;
        st->op = NFC_OP_NONE;
        ESP_LOGW(TAG, "Failed to start NFC reader");
    }
}

bool nfc_lines_contain(const nfc_tab_state_t *st, const char *needle)
{
    if (!st || !needle) return false;
    for (int i = 0; i < st->line_count; i++) {
        if (strstr(st->lines[i], needle)) return true;
    }
    return false;
}
