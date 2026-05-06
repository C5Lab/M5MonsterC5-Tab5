#pragma once

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward types defined privately in screens/.c files */
typedef struct subghz_signal_chunk subghz_signal_chunk_t;
typedef struct subghz_signal subghz_signal_t;
typedef struct subghz_stored_sig subghz_stored_sig_t;
typedef struct signal_row_view signal_row_view_t;

/* Per-tab SubGHz state. Owned by the host (main.c) but accessed by screens.
 * One instance per tab_context_t (Grove/USB/MBus/INTERNAL). */
typedef struct subghz_tab_state {
    /* Pages (parented to the tab container) */
    lv_obj_t *page;            /* SubGHz menu page */
    lv_obj_t *listen_page;
    lv_obj_t *transmit_page;
    lv_obj_t *manage_page;
    lv_obj_t *jammer_page;
    lv_obj_t *tesla_page;

    /* Persistent across screens within a tab */
    float freq_mhz;            /* default 433.92 */
    bool  raw_mode;            /* Listen RAW vs decoded */

    /* Listen state */
    volatile bool listen_running;
    TaskHandle_t  listen_task;
    int           listen_task_tab_id;   /* tab id captured at task start */
    subghz_signal_chunk_t *signal_head;
    subghz_signal_chunk_t *signal_tail;
    subghz_signal_t       *last_signal;
    size_t                 signal_count;
    volatile bool          history_dirty;
    volatile bool          activity_pending;
    volatile bool          psram_exhausted;
    volatile bool          follow_latest;

    /* Listen UI */
    lv_obj_t   *canvas;
    void       *canvas_buf;       /* PSRAM lv_color_t [W*H] */
    lv_obj_t   *sig_list;
    lv_obj_t   *sig_spacer;
    lv_obj_t   *empty_lbl;
    lv_obj_t   *count_lbl;
    lv_obj_t   *listen_freq_lbl;
    lv_obj_t   *btn_start_stop;
    lv_obj_t   *btn_raw;
    lv_obj_t   *listen_freq_popup;
    lv_obj_t   *rollers[5];
    signal_row_view_t *row_pool;  /* heap-allocated array, see listen.c */
    lv_timer_t *ui_timer;

    /* Transmit / Manage shared list */
    subghz_stored_sig_t *sigs;
    int                  sigs_cap;
    int                  sigs_count;
    lv_obj_t            *sig_list_obj;
    lv_obj_t            *status_lbl;
    lv_obj_t            *tx_popup;
    lv_obj_t            *clear_popup;
    int                  pending_tx_idx;
    lv_timer_t          *build_timer;
    int                  build_idx;

    /* Jammer */
    bool       jamming;
    lv_obj_t  *jammer_status_lbl;
    lv_obj_t  *jammer_freq_lbl;
    lv_obj_t  *jammer_big_btn;
    lv_obj_t  *jammer_big_btn_lbl;
    lv_obj_t  *jammer_freq_popup;

    /* Tesla */
    lv_obj_t  *tesla_status_lbl;
} subghz_tab_state_t;

/* ----------------------------------------------------------
 * Host interface (provided by main.c)
 * ---------------------------------------------------------- */

/* Per-tab state access */
subghz_tab_state_t *subghz_host_state(void);                  /* current tab */
subghz_tab_state_t *subghz_host_state_for_tab(int tab_id);

/* Tab/UART helpers */
int  subghz_host_current_tab(void);                /* returns tab_id_t int */
bool subghz_host_tab_is_internal(int tab_id);
lv_obj_t *subghz_host_current_container(void);
lv_obj_t *subghz_host_container_for_tab(int tab_id);

/* UART (per-tab) */
void subghz_host_uart_send(const char *cmd);
void subghz_host_uart_send_for_tab(int tab_id, const char *cmd);
int  subghz_host_uart_read_bytes(int tab_id, void *buf, size_t sz, uint32_t ticks);

/* Navigation */
void subghz_host_hide_all_pages(void);
void subghz_host_show_main_tiles(void);

/* Theme/colors */
lv_color_t subghz_host_color_red(void);
lv_color_t subghz_host_color_green(void);
lv_color_t subghz_host_color_blue(void);
lv_color_t subghz_host_color_cyan(void);
lv_color_t subghz_host_color_orange(void);
lv_color_t subghz_host_color_purple(void);
lv_color_t subghz_host_color_pink(void);
lv_color_t subghz_host_color_amber(void);

lv_color_t subghz_host_ui_bg(void);
lv_color_t subghz_host_ui_card(void);
lv_color_t subghz_host_ui_card_pressed(void);
lv_color_t subghz_host_ui_panel(void);
lv_color_t subghz_host_ui_text(void);
lv_color_t subghz_host_ui_muted(void);
lv_color_t subghz_host_ui_border(void);

/* Init/teardown for a tab's subghz state (called by host) */
subghz_tab_state_t *subghz_host_alloc_state(void);
void subghz_host_free_state(subghz_tab_state_t **pstate);

/* ----------------------------------------------------------
 * Public screen entry points (provided by screens/.c)
 * ---------------------------------------------------------- */
void show_subghz_page(void);
void show_subghz_listen_page(void);
void show_subghz_transmit_page(void);
void show_subghz_manage_page(void);
void show_subghz_jammer_page(void);
void show_subghz_tesla_page(void);

/* Hide-all helper (called by host's hide_all_pages) */
void subghz_hide_all_pages(subghz_tab_state_t *st);

/* Cleanup on tab teardown (best-effort; called by host if needed) */
void subghz_listen_cleanup(subghz_tab_state_t *st);
void subghz_manage_cleanup(subghz_tab_state_t *st);

#ifdef __cplusplus
}
#endif
