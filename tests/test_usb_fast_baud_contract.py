"""Host-execute Tab5's production USB/Grove/M-BUS baud handshake."""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(rf"static\s+[^\n]+\s+{name}\s*\([^;]+?\n\{{", SOURCE, re.S)
    assert match, f"missing production function: {name}"
    start = match.start()
    opening = match.end() - 1
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start:index + 1]
    raise AssertionError(f"unterminated production function: {name}")


HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "usb_vcp_config.h"

typedef int tab_id_t;
typedef int uart_port_t;
typedef int esp_err_t;
#define TAB_GROVE 0
#define TAB_USB 1
#define TAB_MBUS 2
#define ESP_OK 0
#define ESP_FAIL -1
#define JANOS_UART_FT_DEFAULT_BAUD 115200
#define pdMS_TO_TICKS(ms) (ms)
#define TAG "host"

typedef enum {
    JANOS_BAUD_LOST = -1,
    JANOS_BAUD_DEFAULT = 0,
    JANOS_BAUD_FAST = 1,
} janos_uart_baud_result_t;

typedef struct {
    tab_id_t tab;
    uart_port_t port;
} hs_crack_remote_worker_t;

static bool usb_vcp_config_done;
static bool usb_cdc_connected;
static void *usb_cdc_handle;
static uint16_t usb_last_vid, usb_last_pid;
static bool ch34x_result;
static int local_rates[12], uart_rates[12];
static unsigned local_rate_count, uart_rate_count;
static char writes[16][64];
static unsigned write_count, flush_count, marker_count;
static bool marker_results[20];
static const char *marker_lines[20];
static unsigned marker_result_count, marker_result_index;
static uint32_t delayed_ms;

static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGI log_ignore
#define ESP_LOGW log_ignore
#define ESP_LOGE log_ignore

static const char *tab_transport_name(tab_id_t tab)
{
    if (tab == TAB_USB) return "USB";
    if (tab == TAB_MBUS) return "MBus";
    return "Grove";
}
static bool tab_is_internal(tab_id_t tab) { (void)tab; return false; }
static bool ch34x_set_port_baud(uint32_t rate)
{
    local_rates[local_rate_count++] = (int)rate;
    return ch34x_result;
}
static esp_err_t uart_set_baudrate(uart_port_t port, uint32_t rate)
{
    (void)port;
    uart_rates[uart_rate_count++] = (int)rate;
    return ESP_OK;
}
static void compromised_transport_flush(tab_id_t tab, uart_port_t port)
{ (void)tab; (void)port; flush_count++; }
static int transport_write_bytes_tab(tab_id_t tab, uart_port_t port,
                                     const char *data, size_t size)
{
    (void)tab; (void)port;
    if (write_count < 16U) {
        size_t copy = size < sizeof(writes[0]) - 1U ? size : sizeof(writes[0]) - 1U;
        memcpy(writes[write_count], data, copy);
        writes[write_count][copy] = '\0';
    }
    write_count++;
    return (int)size;
}
static bool janos_uart_wait_marker(tab_id_t tab, uart_port_t port,
                                   const char *marker, const char *keep_prefix,
                                   char *keep, size_t keep_size,
                                   uint32_t timeout)
{
    (void)tab; (void)port; (void)marker; (void)keep_prefix; (void)timeout;
    marker_count++;
    if (marker_result_index >= marker_result_count) return false;
    unsigned index = marker_result_index++;
    bool result = marker_results[index];
    if (result && keep && keep_size && marker_lines[index])
        snprintf(keep, keep_size, "%s", marker_lines[index]);
    return result;
}
static void vTaskDelay(uint32_t ticks) { delayed_ms += ticks; }
'''

for name in (
    "janos_transport_baud_supported",
    "janos_transport_set_local_baud",
    "janos_uart_console_alive",
    "janos_uart_baud_status_matches",
    "janos_uart_set_baud",
    "janos_uart_restore_baud",
    "hs_crack_remote_abandon_fast_baud",
):
    HARNESS += production_function(name) + "\n"

HARNESS += r'''
static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

static void reset_fixture(void)
{
    usb_vcp_config_done = true;
    usb_cdc_connected = true;
    usb_cdc_handle = (void *)1;
    usb_last_vid = USB_VCP_CH34X_VID;
    usb_last_pid = USB_VCP_CH340_ALT_PID;
    ch34x_result = true;
    memset(local_rates, 0, sizeof(local_rates));
    memset(uart_rates, 0, sizeof(uart_rates));
    memset(writes, 0, sizeof(writes));
    memset(marker_results, 0, sizeof(marker_results));
    memset(marker_lines, 0, sizeof(marker_lines));
    local_rate_count = uart_rate_count = write_count = flush_count = 0;
    marker_count = marker_result_count = marker_result_index = 0;
    delayed_ms = 0;
}

static void markers(const bool *values, size_t count)
{
    memcpy(marker_results, values, count * sizeof(values[0]));
    marker_result_count = (unsigned)count;
}

int main(void)
{
    reset_fixture();
    const bool set_ok[] = {true, true};
    markers(set_ok, 2);
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_FAST);
    CHECK(local_rate_count == 1U && local_rates[0] == 921600);
    CHECK(uart_rate_count == 0U);
    CHECK(write_count == 2U);
    CHECK(strcmp(writes[0], "uart_baud 921600\r\n") == 0);
    CHECK(strcmp(writes[1], "uart_baud_confirm\r\n") == 0);

    reset_fixture();
    usb_last_pid = 0x1234;
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_DEFAULT);
    CHECK(write_count == 0U && local_rate_count == 0U);

    reset_fixture();
    const bool command_seen[] = {true, true};
    markers(command_seen, 2);
    marker_lines[1] = "[UARTB] status rate=115200 pending=no";
    ch34x_result = false;
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_DEFAULT);
    CHECK(local_rate_count == 2U);
    CHECK(local_rates[0] == 921600 && local_rates[1] == 115200);
    CHECK(write_count == 2U);
    CHECK(delayed_ms >= 11000U);

    reset_fixture();
    const bool confirm_retry_ok[] = {true, false, true};
    markers(confirm_retry_ok, 3);
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_FAST);
    CHECK(local_rate_count == 1U && local_rates[0] == 921600);
    CHECK(write_count == 3U);
    CHECK(strcmp(writes[2], "uart_baud_confirm\r\n") == 0);

    reset_fixture();
    const bool confirm_status_ok[] = {true, false, false, true};
    markers(confirm_status_ok, 4);
    marker_lines[3] = "[UARTB] status rate=921600 pending=no";
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_FAST);
    CHECK(local_rate_count == 1U && local_rates[0] == 921600);
    CHECK(write_count == 4U);
    CHECK(strcmp(writes[3], "uart_baud_status\r\n") == 0);

    reset_fixture();
    const bool confirm_fallback_ok[] = {true, false, false, false, true};
    markers(confirm_fallback_ok, 5);
    marker_lines[4] = "[UARTB] status rate=115200 pending=no";
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_DEFAULT);
    CHECK(local_rate_count == 2U);
    CHECK(local_rates[0] == 921600 && local_rates[1] == 115200);
    CHECK(delayed_ms >= 11000U);

    reset_fixture();
    const bool confirm_lost[] = {true, false, false, false, false, false};
    markers(confirm_lost, 6);
    CHECK(janos_uart_set_baud(TAB_USB, 0, 921600) == JANOS_BAUD_LOST);
    CHECK(local_rate_count == 4U);
    CHECK(local_rates[0] == 921600 && local_rates[1] == 115200);
    CHECK(local_rates[2] == 921600 && local_rates[3] == 115200);
    CHECK(delayed_ms >= 11000U);

    reset_fixture();
    const bool restore_ok[] = {true, true, true};
    markers(restore_ok, 3);
    CHECK(janos_uart_restore_baud(TAB_USB, 0, 921600));
    CHECK(local_rate_count == 1U && local_rates[0] == 115200);
    CHECK(uart_rate_count == 0U);
    CHECK(write_count == 3U);
    CHECK(strcmp(writes[0], "uart_baud 115200\r\n") == 0);
    CHECK(strcmp(writes[1], "uart_baud_confirm\r\n") == 0);
    CHECK(strcmp(writes[2], "uart_baud_status\r\n") == 0);

    reset_fixture();
    const bool grove_ok[] = {true, true};
    markers(grove_ok, 2);
    CHECK(janos_uart_set_baud(TAB_GROVE, 1, 2000000) == JANOS_BAUD_FAST);
    CHECK(local_rate_count == 0U);
    CHECK(uart_rate_count == 1U && uart_rates[0] == 2000000);

    reset_fixture();
    hs_crack_remote_worker_t usb = {.tab = TAB_USB, .port = 0};
    hs_crack_remote_abandon_fast_baud(&usb);
    CHECK(local_rate_count == 1U && local_rates[0] == 115200);
    CHECK(uart_rate_count == 0U && write_count == 0U);

    if (failures) return 1;
    puts("usb_fast_baud_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="usb-fast-baud-") as temporary:
    temporary = Path(temporary)
    harness = temporary / "usb_fast_baud_contract.c"
    binary = temporary / "usb_fast_baud_contract"
    harness.write_text(HARNESS, encoding="utf-8")
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I", str(ROOT / "main"),
        str(harness), str(ROOT / "main/usb_vcp_config.c"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([binary], check=True)
