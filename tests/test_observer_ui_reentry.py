"""Polls append AP tiles, defer collapsed clients, and resist LVGL reentry."""
import unittest
from test_observer_extended_receive import definition, execute_c

class ObserverUiReentry(unittest.TestCase):
    def test_pending_refresh_is_consumed_before_lvgl_callbacks(self):
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct { int unused; } lv_obj_t;
typedef lv_obj_t lv_indev_t;
typedef struct { int client_count; } observer_network_t;
typedef struct { lv_obj_t *tile, *info_label; int rendered_client_count; bool clients_expanded; } observer_network_ui_t;
typedef struct { lv_obj_t *observer_table; bool observer_ui_refresh_pending, observer_has_new_networks; int observer_network_count; observer_network_t *observer_networks; observer_network_ui_t *observer_network_ui; } tab_context_t;
typedef struct { tab_context_t *ctx; } lv_event_t;
#define MAX_CLIENTS_PER_NETWORK 20
#define LV_INDEV_STATE_PRESSED 1
static int rebuilds, appends, client_rows;
static bool touch_pressed;
static lv_obj_t object;
static void observer_table_scroll_end_cb(lv_event_t *e);
static void __attribute__((unused)) observer_add_network_tile(tab_context_t *ctx,int index,bool expanded) {
    (void)index;(void)expanded;++appends;
    assert(!ctx->observer_ui_refresh_pending && !ctx->observer_has_new_networks);
    lv_event_t event={ctx};observer_table_scroll_end_cb(&event);
}
static void __attribute__((unused)) update_observer_table(tab_context_t *ctx) {
    ++rebuilds;
    /* lv_obj_clean -> scroll_to(0,0,OFF) emits SCROLL_END synchronously. */
    lv_event_t event = {ctx}; observer_table_scroll_end_cb(&event);
}
static void *lv_event_get_user_data(lv_event_t *e) {return e->ctx;}
static bool lv_obj_is_valid(lv_obj_t *o) {return o != NULL;}
static lv_indev_t *bsp_display_get_input_dev(void) {return &object;}
static int lv_indev_get_state(lv_indev_t *o) {(void)o;return touch_pressed ? 1 : 0;}
static bool lv_obj_is_scrolling(lv_obj_t *o) {(void)o;return false;}
static void observer_update_ssid_label(tab_context_t *c,int i) {(void)c;(void)i;}
static void observer_format_fields(observer_network_t *n,char *s,size_t z) {(void)n;if(z)s[0]=0;}
static const char *lv_label_get_text(lv_obj_t *o) {(void)o;return "";}
static void lv_label_set_text(lv_obj_t *o,const char *s) {(void)o;(void)s;}
static void observer_update_summary_label(tab_context_t *c,int i) {(void)c;(void)i;}
static void observer_update_client_vendor_labels(tab_context_t *c,int i) {(void)c;(void)i;}
static void observer_add_client_row(tab_context_t *c,int i,int j) {(void)c;(void)i;(void)j;++client_rows;}
static void observer_update_client_toggle(tab_context_t *c,int i) {(void)c;(void)i;}
"""
        source = prefix + definition('observer_sync_changed_tiles','void') + definition('observer_flush_incremental_ui','void') + definition('observer_table_scroll_end_cb','void')
        source += r"""
int main(void) {
    observer_network_t net = {0}; observer_network_ui_t ui = {0};
    tab_context_t ctx = {.observer_table=&object, .observer_network_count=1, .observer_networks=&net, .observer_network_ui=&ui, .observer_ui_refresh_pending=true, .observer_has_new_networks=true};
    observer_sync_changed_tiles(&ctx);
    assert(rebuilds==0 && appends==1 && !ctx.observer_ui_refresh_pending && !ctx.observer_has_new_networks);
    rebuilds=appends=0;ctx.observer_ui_refresh_pending=true;ctx.observer_has_new_networks=true;
    observer_flush_incremental_ui(&ctx);
    assert(rebuilds==0 && appends==1 && !ctx.observer_ui_refresh_pending && !ctx.observer_has_new_networks);
    rebuilds=0;touch_pressed=true;ctx.observer_has_new_networks=true;
    observer_flush_incremental_ui(&ctx);
    assert(rebuilds==0 && ctx.observer_ui_refresh_pending && ctx.observer_has_new_networks);
    touch_pressed=false;ctx.observer_has_new_networks=false;
    ui.tile=&object;net.client_count=20;ui.clients_expanded=false;
    observer_sync_changed_tiles(&ctx);assert(client_rows==0 && ui.rendered_client_count==0);
    ui.clients_expanded=true;observer_sync_changed_tiles(&ctx);
    assert(client_rows==20 && ui.rendered_client_count==20);
    observer_sync_changed_tiles(&ctx);assert(client_rows==20);
    puts("Observer synchronous scroll reentry assertions passed");return 0;
}
"""
        execute_c(source)
