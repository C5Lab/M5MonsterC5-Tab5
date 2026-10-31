"""Host regression: a short display-lock miss must not drop an OTA UART line.

The unchanged ota_monitor_sink function is extracted from main/main.c and
compiled with transport/display boundary stubs. Run in WSL on Windows:
python3 tests/test_ota_monitor_sink.py
"""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/main.c"


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
#include <stdio.h>
#include <string.h>

typedef int tab_id_t;
typedef unsigned int TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
typedef struct { int unused; } ota_info_report_t;
static struct {
    bool await_info, monitoring;
    ota_info_report_t info_report;
} g_ota;

#define TAG "test"
#define ESP_LOGI(tag, format, transport, line) do { \
    (void)(tag); (void)(format); (void)(transport); (void)(line); \
} while (0)

static int lock_attempts, unlock_calls, handled_calls;
static char handled_line[128], handled_raw_line[128];

const char *tab_transport_name(tab_id_t tab)
{ (void)tab; return "Grove"; }
void ota_info_report_feed_line(ota_info_report_t *report, const char *line)
{ (void)report; (void)line; }
bool bsp_display_lock(int timeout_ms)
{
    if (timeout_ms != 50) return false;
    return ++lock_attempts > 1;
}
void bsp_display_unlock(void) { unlock_calls++; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void ota_handle_line(const char *line, const char *raw_line)
{
    handled_calls++;
    snprintf(handled_line, sizeof(handled_line), "%s", line);
    snprintf(handled_raw_line, sizeof(handled_raw_line), "%s", raw_line);
}
'''

CHECKS = r'''
int main(void)
{
    const char *terminal = "OTA: no update (current=1.7.7, latest=1.7.5)";
    const char *prompted = "> OTA: no update (current=1.7.7, latest=1.7.5)";
    tab_id_t tab = 0;
    g_ota.monitoring = true;
    ota_monitor_sink(&tab, prompted);
    if (lock_attempts < 2 || handled_calls != 1 || unlock_calls != 1 ||
        strcmp(handled_line, terminal) != 0 ||
        strcmp(handled_raw_line, prompted) != 0 || !g_ota.monitoring) {
        fprintf(stderr, "FAIL: lock attempts=%d, handled=%d, unlocks=%d, "
                "line='%s', raw='%s', monitoring=%d\n", lock_attempts,
                handled_calls, unlock_calls, handled_line, handled_raw_line,
                g_ota.monitoring);
        return 1;
    }
    puts("Tab5 OTA monitor: terminal line handled once after a display-lock miss");
    return 0;
}
'''


def test_ota_monitor_sink() -> None:
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    assert cc, "Host C compiler required; on Windows, run this test in WSL"
    source = SOURCE.read_text(encoding="utf-8")
    harness = PRELUDE + production_function(source, "ota_monitor_sink") + CHECKS
    with tempfile.TemporaryDirectory() as directory:
        c_file = Path(directory) / "ota_monitor_sink_harness.c"
        executable = Path(directory) / "ota_monitor_sink_harness"
        c_file.write_text(harness, encoding="utf-8")
        subprocess.run(
            [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-o", str(executable), str(c_file)],
            check=True,
        )
        result = subprocess.run([str(executable)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout.strip())


if __name__ == "__main__":
    test_ota_monitor_sink()
