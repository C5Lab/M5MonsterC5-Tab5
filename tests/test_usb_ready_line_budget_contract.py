"""Host-execute the production CRACK line reader with a slow USB READY line."""

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
#define TAG "host"
#define HS_CRACK_REMOTE_LINE_BYTES 640
#define HS_CRACK_REMOTE_LINE_SLICE_MS 500U
#define JANOS_UART_LINE_COMPLETION_MS 2000U
#define pdMS_TO_TICKS(ms) (ms)

static struct { bool cancel_requested; } hs_crack_ui;
static int64_t now_us;
static unsigned clock_calls, transport_reads;
static const uint8_t *stream;
static size_t stream_size, stream_offset;
static int64_t first_byte_at_us;
static uint32_t byte_delay_us;
static bool cross_deadline_fixture;

static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGW log_ignore
static const char *tab_transport_name(int tab)
{ (void)tab; return "USB"; }
static int64_t esp_timer_get_time(void)
{
    if (cross_deadline_fixture) {
        if (clock_calls++ == 0U) return 0;
        if (clock_calls == 2U) return 400000;
        if (clock_calls == 3U) {
            now_us = 600000;
            return now_us;
        }
    }
    return now_us;
}

static int transport_read_bytes_tab(int tab, int port, void *out, size_t size,
                                    TickType_t ticks_to_wait)
{
    (void)tab; (void)port;
    transport_reads++;
    if (size != 1U || stream_offset >= stream_size) {
        now_us += (int64_t)ticks_to_wait * 1000;
        return 0;
    }
    int64_t wait_us = (int64_t)ticks_to_wait * 1000;
    if (now_us < first_byte_at_us) {
        if (now_us + wait_us < first_byte_at_us) {
            now_us += wait_us;
            return 0;
        }
        now_us = first_byte_at_us;
    } else {
        now_us += byte_delay_us;
    }
    *(uint8_t *)out = stream[stream_offset++];
    return 1;
}
'''

HARNESS += production_function("janos_uart_read_line") + "\n"
HARNESS += production_function("hs_crack_remote_read_message") + "\n"

HARNESS += r'''
static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

int main(void)
{
    static const uint8_t ready_stream[] =
        "[CRACK/1] READY kind=capture size=393 crc32=57BF22AF "
        "offset=0 prefix_crc=00000000 bsize=1024 rx_ms=1273 "
        "ack_size=32 ack_wait_ms=2000 next_header_ms=7000 "
        "prepare_ms=10000 finish_linger_ms=7000\r\n"
        "[CRACK/1] END\r\n";
    hs_remote_message_t message = {0};
    stream = ready_stream;
    stream_size = sizeof(ready_stream) - 1U;
    stream_offset = 0;
    first_byte_at_us = 1950000;
    byte_delay_us = 5000;
    CHECK(hs_crack_remote_read_message(TAB_USB, 0, &message, 30000));
    CHECK(message.type == HS_REMOTE_READY);
    CHECK(now_us > 2000000);
    CHECK(stream_offset < stream_size); /* END remains for the raw-mode gate. */

    static const uint8_t status_stream[] =
        "[CRACK/1] STATUS job=j1 state=running checked=1 "
        "safe_offset=10 elapsed_ms=1 rate_milli=1000\r\n";
    memset(&message, 0, sizeof(message));
    now_us = 0;
    transport_reads = 0;
    stream = status_stream;
    stream_size = sizeof(status_stream) - 1U;
    stream_offset = 0;
    first_byte_at_us = 0;
    byte_delay_us = 1000;
    CHECK(hs_crack_remote_read_message(TAB_USB, 0, &message, 500));
    CHECK(message.type == HS_REMOTE_STATUS);

    memset(&message, 0, sizeof(message));
    now_us = 0;
    clock_calls = 0;
    transport_reads = 0;
    stream = NULL;
    stream_size = stream_offset = 0;
    cross_deadline_fixture = true;
    CHECK(!hs_crack_remote_read_message(TAB_USB, 0, &message, 500));
    CHECK(transport_reads <= 2U);

    if (failures) return 1;
    puts("usb_ready_line_budget_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    harness = temporary / "usb_ready_line_budget_contract.c"
    binary = temporary / "usb_ready_line_budget_contract"
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
