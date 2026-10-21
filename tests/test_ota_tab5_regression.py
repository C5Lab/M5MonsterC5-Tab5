"""Execute extracted Tab5 routing functions with transport/UI boundary stubs.

The production precheck is compiled unchanged with the real ota_rf.c, so these
checks cover bypasses in main.c as well as the pure RF decision module.
"""
import re
from pathlib import Path
from _ota_rf_harness import run_harness, assert_ok

ROOT = Path(__file__).resolve().parents[1]


def function(name):
    source = (ROOT / "main/main.c").read_text(encoding="utf-8")
    match = re.search(r"^static [^\n]*\b" + name + r"\([^)]*\)\s*\{.*?^\}", source, re.M | re.S)
    assert match, name
    return match.group(0)


def test_tab5_routing():
    harness = r'''
#include "ota_rf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LV_SYMBOL_WARNING ""
#define LV_SYMBOL_DOWNLOAD ""
#define LV_SYMBOL_WIFI ""
#define LV_SYMBOL_REFRESH ""
#define OTA_VIEW_STATUS 1
#define COLOR_MATERIAL_ORANGE 1
typedef struct {
    char janos_version[32], janos_app_project[32], janos_app_version[64], janos_app_build[80];
    bool janos_version_mismatch;
    bool has_subghz;
} tab_context_t;
typedef struct { bool is_running; char project[32], version[64], build[80]; } ota_slot_info_t;
static tab_context_t context;
static struct {
    bool await_info, precheck_for_update, use_rf, subghz_seen, force_update;
    bool list_sent_after_connect, pending_latest;
    bool install_started, list_after_connect;
    int target_tab, variant_choice;
    char pending_tag[48], post_ip_cmd[96], pending_wifi_cmd[256], channel[8];
    ota_info_report_t info_report;
    ota_rf_state_t rf_state;
} g_ota;
static unsigned blocks, connects, commands, lists;
static char sent[96];
static tab_context_t *get_ctx_for_tab(int tab) { (void)tab; return &context; }
static void ota_precheck_block(const char *phase, const char *detail)
{ (void)phase; (void)detail; ++blocks; }
static void ota_start_deferred_connect(void) { ++connects; }
static void ota_send_cmd(const char *command) { ++commands; snprintf(sent, sizeof(sent), "%s", command); }
static bool ota_prepare_wifi_command(char *out, size_t capacity, bool ota)
{ snprintf(out, capacity, "wifi_connect%s", ota ? " ota" : ""); return true; }
typedef void lv_event_t;
static void ota_read_variant_force(void) {}
static void ota_list_btn_cb(lv_event_t *event) { (void)event; ++lists; }
static bool ota_can_reuse_wifi_for_update(void) { return true; }
static void ota_close_monitor(void) {}
static void ota_open_monitor(int view, const char *title)
{
    (void)view; (void)title;
    g_ota.pending_tag[0] = g_ota.post_ip_cmd[0] = g_ota.pending_wifi_cmd[0] = 0;
    g_ota.subghz_seen = g_ota.use_rf = false;
    ota_info_report_reset(&g_ota.info_report);
}
static void ota_suspend_screen_timeout(void) {}
static void ota_monitor_lock_close_for_update(void) {}
static void ota_status_set_phase(const char *phase, const char *detail, int color, int step, int progress)
{ (void)phase; (void)detail; (void)color; (void)step; (void)progress; }
'''
    harness += "\n" + function("ota_info_sync_running_context")
    harness += "\n" + function("ota_running_version")
    harness += "\n" + function("ota_apply_selected_channel")
    harness += "\n" + function("ota_precheck_decide")
    harness += "\n" + function("ota_rf_start_tag_install")
    harness += "\n" + function("ota_check_btn_cb")
    harness += r'''
static void reset(void)
{
    memset(&g_ota, 0, sizeof(g_ota)); memset(&context, 0, sizeof(context));
    blocks = connects = commands = lists = 0; sent[0] = 0;
    g_ota.precheck_for_update = true; strcpy(g_ota.channel, "dev");
    strcpy(context.janos_version, "1.7.4");
    ota_info_report_feed_line(&g_ota.info_report, "OTA running: ota_0 state=2");
    ota_info_report_feed_line(&g_ota.info_report, "APP[0]: ota_0 state=2 ver=1.6.9");
}
int main(void)
{
    reset();
    ota_slot_info_t slot = {.is_running = true}; strcpy(slot.version, "1.6.9");
    ota_info_sync_running_context(&slot);
    assert(!strcmp(context.janos_version, "1.7.4"));
    assert(!strcmp(context.janos_app_version, "1.6.9"));
    /* A confirmed classic release must preserve v, not use stale APP 1.6.9. */
    strcpy(g_ota.pending_tag, "v1.7.4"); g_ota.variant_choice = 1;
    ota_precheck_decide();
    assert(!blocks && connects == 1 && !strcmp(g_ota.post_ip_cmd, "ota_check v1.7.4"));
    assert(!strcmp(sent, "ota_channel dev"));
    /* Every route, including the plain/no-rf command path, shares the gate. */
    reset(); g_ota.variant_choice = 2;
    g_ota.info_report.layout = OTA_RF_LAYOUT_INCOMPATIBLE;
    g_ota.info_report.have_offset = true; g_ota.info_report.table_offset = 0x8000;
    strcpy(g_ota.pending_tag, "1.7.5"); g_ota.force_update = true;
    ota_precheck_decide(); assert(blocks == 1 && connects == 0 && commands == 0);
    reset(); g_ota.variant_choice = 1; g_ota.subghz_seen = true;
    ota_precheck_decide(); assert(blocks == 1 && connects == 0 && commands == 0);
    reset(); g_ota.variant_choice = 2; /* unknown classic cannot impersonate RF */
    ota_precheck_decide(); assert(blocks == 1 && connects == 0);
    reset(); g_ota.subghz_seen = true; strcpy(g_ota.pending_tag, "1.7.5");
    ota_precheck_decide();
    assert(!blocks && connects == 1 && !strcmp(g_ota.post_ip_cmd, "ota_check 1.7.5"));
    assert(commands == 0); /* no classic channel command on RF */
    reset(); g_ota.force_update = true;
    ota_check_btn_cb(NULL);
    assert(lists == 1 && commands == 0 && connects == 0);
    /* Selected tags survive monitor recreation and require fresh ota_info. */
    reset(); strcpy(g_ota.pending_tag, "v1.7.4");
    ota_rf_start_tag_install(g_ota.pending_tag);
    assert(g_ota.await_info && !strcmp(g_ota.pending_tag, "v1.7.4"));
    assert(!strcmp(sent, "ota_info") && connects == 0);
    ota_info_report_feed_line(&g_ota.info_report, "OTA running: ota_0 state=2");
    ota_info_report_feed_line(&g_ota.info_report, "APP[0]: ota_0 state=2 ver=1.6.9");
    ota_precheck_decide();
    assert(!blocks && connects == 1 && !strcmp(g_ota.post_ip_cmd, "ota_check v1.7.4"));
    reset(); ota_info_report_reset(&g_ota.info_report);
    ota_precheck_decide(); assert(blocks == 1 && connects == 0 && commands == 0);
    reset(); g_ota.subghz_seen = true; g_ota.info_report.has_rf_source = true;
    g_ota.info_report.layout = OTA_RF_LAYOUT_COMPATIBLE;
    strcpy(g_ota.pending_tag, "v1.7.5");
    ota_precheck_decide();
    assert(!blocks && connects == 1 && !strcmp(g_ota.post_ip_cmd, "ota_check rf v1.7.5"));
    puts("Tab5 OTA regression: versions, exact tags, dev channel and variant gates passed");
}
'''
    result = run_harness(harness)
    assert_ok(result)
    print(result.stdout.strip())


if __name__ == "__main__":
    test_tab5_routing()
