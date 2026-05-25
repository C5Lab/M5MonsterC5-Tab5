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
typedef struct subghz_weather_sensor subghz_weather_sensor_t;
typedef struct scanner_freq_view scanner_freq_view_t;

/* Per-tab SubGHz state. Owned by the host (main.c) but accessed by screens.
 * One instance per tab_context_t (Grove/USB/MBus/INTERNAL). */
typedef struct subghz_tab_state {
    /* Pages (parented to the tab container) */
    lv_obj_t *page;            /* SubGHz menu page */
    lv_obj_t *listen_page;
    lv_obj_t *manage_page;
    lv_obj_t *jammer_page;
    lv_obj_t *tesla_page;
    lv_obj_t *hunter_page;
    lv_obj_t *scanner_page;
    lv_obj_t *weather_page;
    lv_obj_t *settings_page;
    lv_obj_t *listen_settings_page;
    lv_obj_t *hunter_settings_page;
    lv_obj_t *scanner_settings_page;

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
    lv_obj_t   *listen_action_popup;
    lv_obj_t   *listen_leave_popup;
    int         listen_pending_action_idx;
    lv_obj_t   *rollers[5];
    signal_row_view_t *row_pool;  /* heap-allocated array, see listen.c */
    lv_timer_t *ui_timer;
    /* Cross-task status text queued from background reader */
    volatile bool listen_status_pending;
    int        listen_status_color_argb;
    char       listen_status_text[96];
    lv_timer_t *listen_status_clear_timer;
    /* Pre-config used by show_subghz_listen_page_at() */
    bool        listen_pending_autostart;

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

    /* Manage SD-only state */
    volatile bool manage_running;
    TaskHandle_t  manage_task;
    int           manage_task_tab_id;
    lv_obj_t     *manage_action_popup;
    lv_obj_t     *manage_delete_popup;
    int           manage_pending_action_idx;
    char          manage_pending_action_name[64];
    /* Cross-task status text */
    volatile bool manage_status_pending;
    int           manage_status_color_argb;
    char          manage_status_text[96];
    lv_timer_t   *manage_status_clear_timer;
    /* Re-list trigger from background task after mutations */
    volatile bool manage_relist_pending;
    lv_timer_t   *manage_ui_timer;

    /* Jammer */
    bool       jamming;
    lv_obj_t  *jammer_status_lbl;
    lv_obj_t  *jammer_freq_lbl;
    lv_obj_t  *jammer_big_btn;
    lv_obj_t  *jammer_big_btn_lbl;
    lv_obj_t  *jammer_freq_popup;

    /* Tesla */
    lv_obj_t  *tesla_status_lbl;

    /* Hunter */
    volatile bool hunter_running;
    TaskHandle_t  hunter_task;
    int           hunter_task_tab_id;
    int           hunter_status_kind;     /* 0=scan 1=capture 2=ok 3=err 4=idle */
    char          hunter_status_text[96];
    volatile bool hunter_status_dirty;
    lv_obj_t     *hunter_spinner;
    lv_obj_t     *hunter_status_lbl;
    lv_obj_t     *hunter_btn_stop;
    lv_obj_t     *hunter_capt_count_lbl;
    lv_obj_t     *hunter_sig_list;
    lv_obj_t     *hunter_sig_spacer;
    lv_obj_t     *hunter_empty_lbl;
    lv_obj_t     *hunter_action_popup;
    lv_obj_t     *hunter_leave_popup;
    int           hunter_pending_action_idx;
    lv_timer_t   *hunter_ui_timer;
    signal_row_view_t *hunter_row_pool;

    /* Scanner */
    volatile bool scanner_running;
    TaskHandle_t  scanner_task;
    int           scanner_task_tab_id;
    float         scanner_freqs[6];
    int           scanner_freq_rssi[6];
    unsigned int  scanner_freq_edges[6];
    int           scanner_freq_count;
    volatile bool scanner_dirty;
    volatile bool scanner_pulse;
    lv_obj_t     *scanner_pulse_dot;
    lv_obj_t     *scanner_pass_lbl;
    scanner_freq_view_t *scanner_tiles;   /* heap array, count=6 */
    lv_timer_t   *scanner_ui_timer;

    /* Weather */
    volatile bool weather_running;
    TaskHandle_t  weather_task;
    int           weather_task_tab_id;
    subghz_weather_sensor_t *weather_sensors;   /* heap array of 8 */
    int           weather_count;
    volatile bool weather_dirty;
    volatile bool weather_pulse;
    lv_obj_t     *weather_status_lbl;
    lv_obj_t     *weather_pulse_dot;
    lv_obj_t     *weather_tiles_grid;
    lv_timer_t   *weather_ui_timer;
    float         weather_freq;

    /* Freq-correction Settings (subghz_get_freq_correction / set) */
    volatile bool settings_query_running;
    TaskHandle_t  settings_task;
    int           settings_task_tab_id;
    float         settings_pending_correction;
    volatile bool settings_correction_loaded;
    volatile bool settings_correction_changed;
    lv_obj_t     *settings_rollers[4]; /* sign, int, dec1, dec2 */
    lv_obj_t     *settings_status_lbl;
    lv_obj_t     *settings_set_btn;
    lv_timer_t   *settings_ui_timer;
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
void subghz_host_uart_flush_input(int tab_id);  /* drop any stale RX bytes */

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
void show_subghz_listen_page_at(float mhz, bool autostart);
void show_subghz_manage_page(void);
void show_subghz_jammer_page(void);
void show_subghz_tesla_page(void);
void show_subghz_hunter_page(void);
void show_subghz_hunter_page_resume(void);
void show_subghz_scanner_page(void);
void show_subghz_weather_page(void);
void show_subghz_settings_page(void);
void show_subghz_listen_settings_page(void);
void show_subghz_hunter_settings_page(void);
void show_subghz_scanner_settings_page(void);

/* Hide-all helper (called by host's hide_all_pages) */
void subghz_hide_all_pages(subghz_tab_state_t *st);

/* Cleanup on tab teardown (best-effort; called by host if needed) */
void subghz_listen_cleanup(subghz_tab_state_t *st);
void subghz_manage_cleanup(subghz_tab_state_t *st);
void subghz_hunter_cleanup(subghz_tab_state_t *st);
void subghz_scanner_cleanup(subghz_tab_state_t *st);
void subghz_weather_cleanup(subghz_tab_state_t *st);
void subghz_settings_cleanup(subghz_tab_state_t *st);
void subghz_listen_settings_cleanup(subghz_tab_state_t *st);
void subghz_hunter_settings_cleanup(subghz_tab_state_t *st);
void subghz_scanner_settings_cleanup(subghz_tab_state_t *st);

#ifdef __cplusplus
}
#endif
