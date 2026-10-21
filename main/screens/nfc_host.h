#pragma once

#include "nfc_parser.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_LINE_CAP 160
#define NFC_LINE_LEN 96
#define NFC_LIST_CAP 128

typedef enum {
    NFC_OP_NONE = 0,
    NFC_OP_INIT,
    NFC_OP_READ,
    NFC_OP_SAVE,
    NFC_OP_LIST,
    NFC_OP_LOAD,
    NFC_OP_RENAME,
    NFC_OP_DELETE,
    NFC_OP_EMULATE,
} nfc_op_t;

typedef struct nfc_tab_state {
    int tab_id;

    lv_obj_t *hub_page;
    lv_obj_t *read_page;
    lv_obj_t *list_page;
    lv_obj_t *detail_page;
    lv_obj_t *emulate_page;

    lv_obj_t *hub_status;
    lv_obj_t *hub_read_tile;
    lv_obj_t *hub_list_tile;

    lv_obj_t *read_status;
    lv_obj_t *read_detail;
    lv_obj_t *read_btn;
    lv_obj_t *save_btn;
    bool have_card;
    bool text_input_open;
    nfc_ui_card_t card;

    lv_obj_t *list_status;
    lv_obj_t *list_obj;
    nfc_list_entry_t rows[NFC_LIST_CAP];
    int row_count;

    int detail_idx;
    char detail_name[64];
    bool detail_loaded;
    lv_obj_t *detail_status;
    lv_obj_t *detail_lbl;
    lv_obj_t *detail_emu_btn;
    lv_obj_t *confirm_popup;
    bool hint_have_data;
    char hint_type[32];

    bool emulate_running;
    lv_obj_t *emu_status;
    lv_obj_t *emu_detail;

    lv_timer_t *ui_timer;
    bool busy;

    TaskHandle_t task;
    volatile bool cancel;
    char cmd[128];
    nfc_op_t op;
    uint32_t timeout_ms;
    SemaphoreHandle_t lock;

    volatile bool result_ready;
    bool result_timeout;
    nfc_op_t result_op;
    nfc_ui_card_t result_card;
    char lines[NFC_LINE_CAP][NFC_LINE_LEN];
    int line_count;

    volatile bool live_dirty;
    char live_status[80];
    char live_detail[160];
} nfc_tab_state_t;

nfc_tab_state_t *nfc_host_alloc_state(void);
nfc_tab_state_t *nfc_host_state(void);
nfc_tab_state_t *nfc_host_state_for_tab(int tab_id);

void nfc_hide_all_pages(nfc_tab_state_t *st);
void nfc_uart_start(nfc_tab_state_t *st, nfc_op_t op, const char *cmd, uint32_t timeout_ms);
void nfc_uart_cancel(nfc_tab_state_t *st);

lv_obj_t *nfc_make_page(lv_obj_t *container);
void nfc_set_btn_enabled(lv_obj_t *btn, bool en);
bool nfc_lines_contain(const nfc_tab_state_t *st, const char *needle);

void show_nfc_page(void);
void show_nfc_read_page(void);
void show_nfc_list_page(void);
void show_nfc_detail_page(int idx);
void show_nfc_emulate_page(void);

void nfc_hub_on_init(nfc_tab_state_t *st);
void nfc_read_on_live(nfc_tab_state_t *st, const char *status, const char *detail);
void nfc_read_on_result(nfc_tab_state_t *st);
void nfc_read_on_save(nfc_tab_state_t *st);
void nfc_list_on_result(nfc_tab_state_t *st);
void nfc_detail_on_load(nfc_tab_state_t *st);
void nfc_detail_on_rename(nfc_tab_state_t *st);
void nfc_detail_on_delete(nfc_tab_state_t *st);
void nfc_emulate_on_result(nfc_tab_state_t *st);

#ifdef __cplusplus
}
#endif
