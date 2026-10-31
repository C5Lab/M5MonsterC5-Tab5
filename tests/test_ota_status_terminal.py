"""Host regression for Tab5 OTA status lines from main/main.c.

Extract the production functions unchanged and compile them with small LVGL
boundary stubs. Run with: python3 tests/test_ota_status_terminal.py
"""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def production_function(source: str, name: str) -> str:
    match = re.search(
        rf"^static [^\n]*\b{re.escape(name)}\([^)]*\)\s*\{{.*?^\}}",
        source,
        re.MULTILINE | re.DOTALL,
    )
    assert match, f"production function missing: {name}"
    return match.group(0)


PRELUDE = r'''
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LV_SYMBOL_OK ""
#define LV_SYMBOL_RIGHT ""
#define LV_SYMBOL_REFRESH ""
#define LV_SYMBOL_DOWNLOAD ""
#define LV_SYMBOL_SD_CARD ""
#define LV_SYMBOL_WARNING ""
#define LV_SYMBOL_WIFI ""
#define LV_SYMBOL_LIST ""
#define LV_ANIM_OFF 0
#define LV_STATE_DISABLED 1
#define LV_STATE_PRESSED 2
#define LV_OBJ_FLAG_HIDDEN 4
#define COLOR_MATERIAL_GREEN 1
#define COLOR_MATERIAL_ORANGE 2
#define COLOR_MATERIAL_RED 3
#define OTA_VIEW_STATUS 1
#define OTA_VIEW_LIST 2
#define OTA_VIEW_INFO 3

typedef int lv_color_t;
typedef int tab_id_t;
typedef struct { int unused; } ota_info_report_t;
typedef struct {
    char text[512];
    bool hidden, disabled;
    int bar_value;
} lv_obj_t;

static struct {
    lv_obj_t *mon_phase, *mon_detail, *mon_steps[4], *mon_progress;
    lv_obj_t *mon_close_btn, *mon_close_label, *mon_wifi, *mon_ip, *mon_ota;
    bool list_after_connect, flash_progress_seen, byte_progress_seen;
    bool reboot_wait_shown, terminal_status_seen, release_list_waiting;
    bool install_started, list_sent_after_connect, await_info, monitoring;
    int view, mon_release_count;
    char post_ip_cmd[96];
    ota_info_report_t info_report;
} g_ota;
static int restore_calls;
static int log_calls;
static char logged_line[512];

#define TAG "test"
#define ESP_LOGI(tag, format, transport, line) do { \
    (void)(tag); (void)(format); (void)(transport); (void)(line); \
} while (0)

void lv_label_set_text(lv_obj_t *obj, const char *text)
{ snprintf(obj->text, sizeof(obj->text), "%s", text); }
void lv_label_set_text_fmt(lv_obj_t *obj, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(obj->text, sizeof(obj->text), fmt, args);
    va_end(args);
}
void lv_bar_set_value(lv_obj_t *obj, int value, int anim)
{ (void)anim; obj->bar_value = value; }
void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t color, int selector)
{ (void)obj; (void)color; (void)selector; }
void lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t color, int selector)
{ (void)obj; (void)color; (void)selector; }
void lv_obj_add_state(lv_obj_t *obj, int state)
{ if (state == LV_STATE_DISABLED) obj->disabled = true; }
void lv_obj_clear_state(lv_obj_t *obj, int state)
{ if (state == LV_STATE_DISABLED) obj->disabled = false; }
void lv_obj_add_flag(lv_obj_t *obj, int flag)
{ if (flag == LV_OBJ_FLAG_HIDDEN) obj->hidden = true; }
void lv_obj_clear_flag(lv_obj_t *obj, int flag)
{ if (flag == LV_OBJ_FLAG_HIDDEN) obj->hidden = false; }
void lv_obj_center(lv_obj_t *obj) { (void)obj; }
lv_color_t lv_color_hex(int rgb) { return rgb; }
lv_color_t lv_color_darken(lv_color_t color, int amount)
{ (void)amount; return color; }
lv_color_t ui_muted_color(void) { return 4; }
lv_color_t ui_text_color(void) { return 5; }
void ota_release_list_finish(bool ok) { (void)ok; g_ota.release_list_waiting = false; }
void ota_note_wifi_disconnected(void) {}
void ota_note_wifi_connected(void) {}
void ota_restore_screen_timeout(void) { restore_calls++; }
void ota_send_cmd(const char *command) { (void)command; }
void ota_release_list_add_line(const char *line) { (void)line; }
void ota_append_log(const char *line)
{
    log_calls++;
    snprintf(logged_line, sizeof(logged_line), "%s", line);
}
void ota_info_parse_line(const char *line) { (void)line; }
void ota_info_pretty(const char *line, char *out, size_t size)
{ snprintf(out, size, "%s", line); }
void ota_info_report_feed_line(ota_info_report_t *report, const char *line)
{ (void)report; (void)line; }
const char *tab_transport_name(tab_id_t tab) { (void)tab; return "Grove"; }
bool bsp_display_lock(int timeout_ms) { (void)timeout_ms; return true; }
void bsp_display_unlock(void) {}
void vTaskDelay(int ticks) { (void)ticks; }
'''

CHECKS = r'''
static int failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static lv_obj_t phase, detail, steps[4], progress, close_btn, close_label, summary_label;

static void begin_install(void)
{
    memset(&g_ota, 0, sizeof(g_ota));
    restore_calls = 0;
    log_calls = 0;
    logged_line[0] = '\0';
    memset(&phase, 0, sizeof(phase));
    memset(&detail, 0, sizeof(detail));
    memset(steps, 0, sizeof(steps));
    memset(&progress, 0, sizeof(progress));
    memset(&close_btn, 0, sizeof(close_btn));
    memset(&close_label, 0, sizeof(close_label));
    memset(&summary_label, 0, sizeof(summary_label));
    g_ota.mon_phase = &phase;
    g_ota.mon_detail = &detail;
    for (int i = 0; i < 4; ++i) g_ota.mon_steps[i] = &steps[i];
    g_ota.mon_progress = &progress;
    g_ota.mon_close_btn = &close_btn;
    g_ota.mon_close_label = &close_label;
    g_ota.mon_ota = &summary_label;
    g_ota.install_started = true;
    g_ota.monitoring = true;
    g_ota.view = OTA_VIEW_STATUS;
    ota_monitor_lock_close_for_update();
    ota_status_set_phase("Checking release", "Waiting for C5", 2, 1, 35);
}

int main(void)
{
    const char *no_update = "OTA: no update (current=1.7.7, latest=1.7.5)";
    begin_install();
    ota_status_from_ota_line(no_update);
    const char *summary = ota_status_pretty(no_update);
    CHECK(ota_ci_contains(phase.text, "no newer") ||
          ota_ci_contains(phase.text, "up to date"));
    CHECK(ota_ci_contains(detail.text, "no newer") ||
          ota_ci_contains(detail.text, "up to date"));
    CHECK(strstr(detail.text, "current=1.7.7, latest=1.7.5") != NULL);
    CHECK(summary && (ota_ci_contains(summary, "no newer") ||
                      ota_ci_contains(summary, "up to date")));
    CHECK(!g_ota.flash_progress_seen && !g_ota.byte_progress_seen);
    CHECK(progress.hidden);
    CHECK(steps[2].hidden && steps[3].hidden);
    CHECK(g_ota.terminal_status_seen);
    CHECK(!close_btn.disabled);
    CHECK(strcmp(close_label.text, "Done - Close") == 0);
    CHECK(restore_calls == 1);

    /* The real UART line can include the shell prompt before the OTA marker. */
    const char *prompted = "> OTA: no update (current=1.7.7, latest=1.7.5)";
    tab_id_t tab = 0;
    begin_install();
    ota_monitor_sink(&tab, prompted);
    CHECK(ota_ci_contains(phase.text, "no newer") ||
          ota_ci_contains(phase.text, "up to date"));
    CHECK(strstr(detail.text, "current=1.7.7, latest=1.7.5") != NULL);
    CHECK(ota_ci_contains(summary_label.text, "no newer") ||
          ota_ci_contains(summary_label.text, "up to date"));
    CHECK(!g_ota.flash_progress_seen && !g_ota.byte_progress_seen);
    CHECK(progress.hidden && steps[2].hidden && steps[3].hidden);
    CHECK(g_ota.terminal_status_seen && !close_btn.disabled);
    CHECK(strcmp(close_label.text, "Done - Close") == 0);
    CHECK(restore_calls == 1);
    CHECK(log_calls == 1 && strcmp(logged_line, prompted) == 0);

    /* An ordinary version pair remains the nonterminal target-selection path. */
    const char *target = "OTA: current=1.7.7, target=1.7.8";
    begin_install();
    ota_status_from_ota_line(target);
    summary = ota_status_pretty(target);
    CHECK(ota_ci_contains(phase.text, "selecting target"));
    CHECK(strcmp(detail.text, target) == 0);
    CHECK(summary && ota_ci_contains(summary, "preparing selected release"));
    CHECK(g_ota.flash_progress_seen && !g_ota.byte_progress_seen);
    CHECK(!progress.hidden && !steps[2].hidden && !steps[3].hidden);
    CHECK(!g_ota.terminal_status_seen && close_btn.disabled);
    CHECK(restore_calls == 0);

    if (failures) return 1;
    puts("Tab5 OTA status: terminal no-update and target selection passed");
    return 0;
}
'''


def test_ota_status_terminal() -> None:
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert cc, "Host C compiler required; on Windows, run this test in WSL"
    source = (ROOT / "main/main.c").read_text(encoding="utf-8")
    names = (
        "ota_ci_contains",
        "ota_status_pretty",
        "ota_status_set_step",
        "ota_status_set_phase",
        "ota_monitor_set_close_state",
        "ota_monitor_lock_close_for_update",
        "ota_monitor_finish_close_ok",
        "ota_monitor_finish_close_error",
        "ota_status_parse_download_pct",
        "ota_status_overall_from_download_pct",
        "ota_status_from_ota_line",
        "ota_handle_line",
        "ota_monitor_sink",
    )
    harness = PRELUDE + "\n".join(production_function(source, name) for name in names) + CHECKS
    with tempfile.TemporaryDirectory() as directory:
        c_file = Path(directory) / "ota_status_harness.c"
        executable = Path(directory) / "ota_status_harness"
        c_file.write_text(harness, encoding="utf-8")
        subprocess.run(
            [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", str(executable), str(c_file)],
            check=True,
        )
        result = subprocess.run([str(executable)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout.strip())


if __name__ == "__main__":
    test_ota_status_terminal()
