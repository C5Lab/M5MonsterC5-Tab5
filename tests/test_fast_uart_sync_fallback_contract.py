"""Host-execute Tab5's production fast-UART sync fallback path."""

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
#include <stdarg.h>
#include "hs_crack_remote_core.h"
#include "transfer_speed_config.h"

typedef int tab_id_t;
typedef int uart_port_t;
#define TAB_GROVE 0
#define TAB_USB 1
#define TAB_MBUS 2
#define TAG "host"

typedef enum {
    JANOS_BAUD_LOST = -1,
    JANOS_BAUD_DEFAULT = 0,
    JANOS_BAUD_FAST = 1,
} janos_uart_baud_result_t;

typedef enum {
    HS_REMOTE_CACHE_ERROR = -1,
    HS_REMOTE_CACHE_MISSING = 0,
    HS_REMOTE_CACHE_PRESENT = 1,
} hs_remote_cache_state_t;

typedef struct {
    int tab, port;
    bool failed;
    uint8_t stage_attempt;
    char stage_reason[24];
} hs_crack_remote_worker_t;
static struct { bool cancel_requested; } hs_crack_ui;
static unsigned cache_calls, set_baud_calls, restore_calls, upload_calls;
static int set_baud_rate, restore_baud_rate;
static janos_uart_baud_result_t set_baud_result;
static bool restore_result;
static hs_remote_cache_state_t cache_results[3];
static bool upload_result[3];
static int janos_ft_baud = 2000000;
static __attribute__((unused)) int janos_usb_ft_baud = 1500000;
#define pdMS_TO_TICKS(ms) (ms)
static void __attribute__((unused)) vTaskDelay(uint32_t ticks) { (void)ticks; }

static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGW log_ignore
static const char *tab_transport_name(int tab)
{ return tab == TAB_USB ? "USB" : tab == TAB_MBUS ? "MBus" : "Grove"; }
static janos_uart_baud_result_t janos_uart_set_baud(int tab, int port, int rate)
{
    (void)tab; (void)port;
    set_baud_rate = rate;
    set_baud_calls++;
    return set_baud_result;
}
static bool janos_uart_restore_baud(int tab, int port, int rate)
{
    (void)tab; (void)port;
    restore_baud_rate = rate;
    restore_calls++;
    return restore_result;
}
static __attribute__((unused)) hs_remote_cache_state_t hs_crack_remote_check_cache(
    hs_crack_remote_worker_t *worker, const char *kind,
    uint64_t size, uint32_t crc32)
{
    (void)worker; (void)kind; (void)size; (void)crc32;
    return cache_results[cache_calls++];
}
static bool hs_crack_remote_upload(hs_crack_remote_worker_t *worker,
                                   const char *kind, const char *path,
                                   uint64_t size, uint32_t crc32)
{
    (void)worker; (void)kind; (void)path; (void)size; (void)crc32;
    return upload_result[upload_calls++];
}
static void __attribute__((unused)) hs_crack_remote_report(hs_crack_remote_worker_t *worker,
                                   const char *stage, unsigned attempt,
                                   const char *reason)
{ (void)worker; (void)stage; (void)attempt; (void)reason; }
static void hs_crack_remote_report_current(hs_crack_remote_worker_t *worker,
                                           const char *reason)
{ (void)worker; (void)reason; }
static void hs_crack_remote_abandon_fast_baud(hs_crack_remote_worker_t *worker)
{ (void)worker; }
'''

HARNESS += production_function("hs_crack_remote_sync_file") + "\n"

HARNESS += r'''
static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

static void reset(void)
{
    cache_calls = set_baud_calls = restore_calls = upload_calls = 0;
    set_baud_rate = restore_baud_rate = 0;
    set_baud_result = JANOS_BAUD_DEFAULT;
    restore_result = true;
    cache_results[0] = cache_results[1] = cache_results[2] =
        HS_REMOTE_CACHE_MISSING;
    upload_result[0] = upload_result[1] = upload_result[2] = false;
    hs_crack_ui.cancel_requested = false;
}

int main(void)
{
    hs_crack_remote_worker_t grove = {.tab = TAB_GROVE, .port = 1};
    hs_crack_remote_worker_t usb = {.tab = TAB_USB, .port = 0};

    reset();
    grove.failed = false;
    CHECK(hs_crack_remote_sync_file(&grove, false, "wordlist", "/w", 42, 7));
    CHECK(set_baud_calls == 0U && restore_calls == 0U && upload_calls == 0U);

    reset();
    usb.failed = false;
    cache_results[0] = HS_REMOTE_CACHE_PRESENT;
    CHECK(hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
    CHECK(cache_calls == 1U);
    CHECK(set_baud_calls == 0U);
    CHECK(restore_calls == 0U);
    CHECK(upload_calls == 0U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    upload_result[0] = false;
    upload_result[1] = true;
    CHECK(hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 2U);
    CHECK(set_baud_rate == 2000000 && restore_baud_rate == 2000000);

    reset();
    usb.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    upload_result[0] = true;
    CHECK(hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
    CHECK(cache_calls == 1U);
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 1U);
    CHECK(set_baud_rate == 1500000 && restore_baud_rate == 1500000);

    reset();
    usb.failed = false;
    cache_results[0] = HS_REMOTE_CACHE_ERROR;
    cache_results[1] = HS_REMOTE_CACHE_ERROR;
    cache_results[2] = HS_REMOTE_CACHE_ERROR;
    CHECK(!hs_crack_remote_sync_file(&usb, true, "wordlist", "/w", 42, 7));
    CHECK(cache_calls == 3U);
    CHECK(set_baud_calls == 0U);
    CHECK(restore_calls == 0U);
    CHECK(upload_calls == 0U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    upload_result[0] = false;
    upload_result[1] = false;
    upload_result[2] = true;
    CHECK(hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 3U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    restore_result = false;
    upload_result[0] = true;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "capture", "/c", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 1U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    restore_result = false;
    upload_result[0] = false;
    upload_result[1] = true;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 1U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    upload_result[0] = false;
    hs_crack_ui.cancel_requested = true;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "capture", "/c", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 1U && upload_calls == 1U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_DEFAULT;
    upload_result[0] = false;
    upload_result[1] = false;
    upload_result[2] = false;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(set_baud_calls == 1U && restore_calls == 0U && upload_calls == 3U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_FAST;
    upload_result[0] = false;
    grove.failed = true;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(upload_calls == 1U);

    reset();
    grove.failed = false;
    set_baud_result = JANOS_BAUD_LOST;
    CHECK(!hs_crack_remote_sync_file(&grove, true, "wordlist", "/w", 42, 7));
    CHECK(grove.failed);
    CHECK(upload_calls == 0U && restore_calls == 0U);

    if (failures) return 1;
    puts("fast_uart_sync_fallback_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    harness = temporary / "fast_uart_sync_fallback_contract.c"
    binary = temporary / "fast_uart_sync_fallback_contract"
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
            str(ROOT / "main/transfer_speed_config.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([binary], check=True)
