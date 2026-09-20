"""Host-test the production USB read wrapper against a timed CDC ring buffer."""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/main.c").read_text(encoding="utf-8")


def function(name: str) -> str:
    match = re.search(
        r"static [^\n]+ " + re.escape(name) + r"\([^;]+?\n\{.*?\n\}",
        SOURCE,
        re.S,
    )
    if not match:
        raise RuntimeError(f"production function missing: {name}")
    return match.group(0)


harness = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef unsigned TickType_t;
typedef void *usbh_cdc_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define TAG "host"
static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGD log_ignore
#define ESP_LOGW log_ignore

static usbh_cdc_handle_t usb_cdc_handle = (void *)1;
static bool usb_cdc_connected = true;
static bool usb_transport_ready = true;
static bool usb_debug_logs = false;
static unsigned now_ms;
static unsigned size_queries;
static unsigned blocking_reads;
static bool deliver_data;

static void usb_transport_init(void) {}
static void usb_log_cdc_state(const char *where) { (void)where; }
static const char *esp_err_to_name(esp_err_t err) { (void)err; return "ERR"; }
static __attribute__((unused)) void vTaskDelay(TickType_t ticks) { now_ms += ticks; }

static __attribute__((unused)) esp_err_t usbh_cdc_get_rx_buffer_size(
    usbh_cdc_handle_t handle, size_t *size)
{
    (void)handle;
    size_queries++;
    *size = 0;
    return ESP_OK;
}

static esp_err_t usbh_cdc_read_bytes(usbh_cdc_handle_t handle, uint8_t *buf,
                                     size_t *length, TickType_t ticks_to_wait)
{
    (void)handle;
    blocking_reads++;
    if (!deliver_data) {
        now_ms += ticks_to_wait;
        *length = 0;
        return ESP_ERR_TIMEOUT;
    }
    now_ms += 7;
    memset(buf, 0xA5, *length);
    return ESP_OK;
}

static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)
'''

harness += function("usb_transport_read")
harness += r'''

int main(void)
{
    uint8_t bytes[32] = {0};
    deliver_data = true;
    CHECK(usb_transport_read(bytes, sizeof(bytes), 100) == 32);
    CHECK(now_ms == 7);
    CHECK(blocking_reads == 1);
    CHECK(size_queries == 0);
    CHECK(bytes[0] == 0xA5 && bytes[31] == 0xA5);

    now_ms = size_queries = blocking_reads = 0;
    deliver_data = false;
    CHECK(usb_transport_read(bytes, sizeof(bytes), 25) == 0);
    CHECK(now_ms == 25);
    CHECK(blocking_reads == 1);
    CHECK(size_queries == 0);

    usb_cdc_connected = false;
    CHECK(usb_transport_read(bytes, sizeof(bytes), 25) == -1);

    if (failures) return 1;
    puts("usb_blocking_read_contract: PASS");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    source = directory / "usb_blocking_read_contract.c"
    binary = directory / "usb_blocking_read_contract"
    source.write_text(harness, encoding="utf-8")
    subprocess.run(
        ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
