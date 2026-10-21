"""Host-execute Tab5's production USB probe retry and command framing path."""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(
        r"static [^\n]+ " + re.escape(name) + r"\([^;]+?\n\{.*?\n\}",
        SOURCE,
        re.S,
    )
    if not match:
        raise RuntimeError(f"production function missing: {name}")
    return match.group(0)


HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "hs_crack_remote_core.h"

typedef int tab_id_t;
typedef int uart_port_t;
typedef unsigned TickType_t;
#define TAB_USB 1
#define TAB_MBUS 2
#define TAG "host"
#define pdMS_TO_TICKS(ms) (ms)
#define HS_CRACK_REMOTE_COMMAND_BYTES 192

typedef struct { int tab, port; } hs_crack_remote_worker_t;
static struct { bool cancel_requested; } hs_crack_ui;
static unsigned writes, flushes, reads, diag_calls, delay_calls;
static unsigned cdc_probe_timeouts;
static uint32_t observed_timeout[3];
static char last_write[256];
static size_t last_write_size;
static bool diag_result = true;
static bool fail_next_read;

static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGI log_ignore
#define ESP_LOGW log_ignore
static const char *tab_transport_name(int tab)
{ return tab == TAB_USB ? "USB" : "MBus"; }
static void vTaskDelay(unsigned ticks)
{ (void)ticks; delay_calls++; }
static void usb_log_cdc_state(const char *where)
{ if (strcmp(where, "probe_timeout") == 0) cdc_probe_timeouts++; }
static void compromised_transport_flush(int tab, int port)
{ (void)tab; (void)port; flushes++; }
static int transport_write_bytes_tab(int tab, int port,
                                     const char *data, size_t size)
{
    (void)tab; (void)port;
    writes++;
    if (size >= sizeof(last_write)) return -1;
    memcpy(last_write, data, size);
    last_write[size] = '\0';
    last_write_size = size;
    return (int)size;
}
static bool hs_crack_remote_read_message(int tab, int port,
                                         hs_remote_message_t *message,
                                         uint32_t timeout_ms)
{
    (void)tab; (void)port;
    observed_timeout[reads++] = timeout_ms;
    if (fail_next_read) { fail_next_read = false; return false; }
    memset(message, 0, sizeof(*message));
    message->type = HS_REMOTE_FILE;
    strcpy(message->kind, "wordlist");
    message->size = 15645263U;
    message->crc32 = 0x30F79DB6U;
    message->file_present = false;
    return true;
}
static bool hs_crack_remote_query_diag(hs_crack_remote_worker_t *worker)
{ (void)worker; diag_calls++; return diag_result; }
'''

for function_name in (
    "hs_crack_remote_write_all",
    "hs_crack_remote_send_line",
    "hs_crack_remote_probe",
):
    HARNESS += production_function(function_name) + "\n"

HARNESS += r'''
static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

int main(void)
{
    CHECK(hs_crack_remote_send_line(
        TAB_USB, 0, "crack_worker probe wordlist 15645263 30F79DB6"));
    CHECK(writes == 1U);
    CHECK(last_write_size == strlen(last_write));
    CHECK(strcmp(last_write,
        "crack_worker probe wordlist 15645263 30F79DB6\r\n") == 0);

    writes = flushes = reads = diag_calls = delay_calls = 0;
    cdc_probe_timeouts = 0;
    fail_next_read = true;
    hs_crack_remote_worker_t usb = {.tab = TAB_USB, .port = 0};
    bool present = true;
    CHECK(!hs_crack_remote_probe(&usb, "wordlist", 15645263U,
                                 0x30F79DB6U, &present));
    CHECK(writes == 1U && flushes == 1U && reads == 1U);
    CHECK(observed_timeout[0] == HS_REMOTE_PROBE_TIMEOUT_MS);
    CHECK(diag_calls == 0U && delay_calls == 0U);
    CHECK(cdc_probe_timeouts == 1U);

    writes = flushes = reads = diag_calls = delay_calls = 0;
    cdc_probe_timeouts = 0;
    diag_result = false;
    present = true;
    CHECK(hs_crack_remote_probe(&usb, "wordlist", 15645263U,
                                0x30F79DB6U, &present));
    CHECK(!present);
    CHECK(writes == 1U && flushes == 1U && reads == 1U);
    CHECK(diag_calls == 0U && delay_calls == 0U);
    CHECK(cdc_probe_timeouts == 0U);

    writes = flushes = reads = diag_calls = delay_calls = 0;
    diag_result = true;
    hs_crack_remote_worker_t mbus = {.tab = TAB_MBUS, .port = 0};
    present = true;
    CHECK(hs_crack_remote_probe(&mbus, "wordlist", 15645263U,
                                0x30F79DB6U, &present));
    CHECK(writes == 1U && flushes == 1U && reads == 1U);
    CHECK(observed_timeout[0] == HS_REMOTE_PROBE_TIMEOUT_MS);
    CHECK(diag_calls == 0U);

    if (failures) return 1;
    puts("usb_probe_recovery_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    harness = temporary / "usb_probe_recovery_contract.c"
    binary = temporary / "usb_probe_recovery_contract"
    harness.write_text(HARNESS, encoding="utf-8")
    subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-I",
            str(ROOT / "main"),
            str(harness),
            str(ROOT / "main/hs_crack_remote_core.c"),
            str(ROOT / "main/hs_crack_cache.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([binary], check=True)
