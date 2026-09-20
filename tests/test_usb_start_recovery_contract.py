"""Host-execute Tab5's production USB worker-start recovery path."""

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
#define TAB_GROVE 0
#define TAB_USB 1
#define TAB_MBUS 2
#define TAG "host"
#define LV_SYMBOL_OK "OK"

typedef struct {
    tab_id_t tab;
    uart_port_t port;
    bool usable, active, finished, failed, counted, cancel_pending;
    uint8_t stage_attempt, stage_max;
    char stage[16], stage_reason[24];
    char job[32];
    uint64_t shard_start, shard_end, checked, accounted;
    hs_remote_lease_t lease;
    uint8_t found_ssid[32];
    size_t found_ssid_length;
    bool pending_found;
    uint8_t pending_password[63];
    size_t pending_password_length;
    uint8_t pending_ssid[32];
    size_t pending_ssid_length;
} hs_crack_remote_worker_t;

static struct {
    bool cancel_requested;
    char worker_status[3][60];
} hs_crack_ui;

typedef struct {
    bool received;
    hs_remote_message_t message;
} event_t;

static event_t events[40];
static size_t event_count, event_index;
static int64_t now_us;
static unsigned writes, flushes, renders, cdc_state_logs;
static char commands[8][256];
static char last_cdc_state[40];

static void log_ignore(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
#define ESP_LOGI log_ignore
#define ESP_LOGW log_ignore
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(uint32_t ticks) { now_us += (int64_t)ticks * 1000; }

static const char *tab_transport_name(int tab)
{ return tab == TAB_USB ? "USB" : (tab == TAB_MBUS ? "MBus" : "Grove"); }
static int64_t esp_timer_get_time(void)
{ return now_us; }
static void compromised_transport_flush(int tab, int port)
{ (void)tab; (void)port; flushes++; }
static void hs_crack_render_workers(void)
{ renders++; }
static void hs_crack_remote_report(hs_crack_remote_worker_t *worker,
                                   const char *stage, unsigned attempt,
                                   const char *reason)
{
    worker->stage_attempt = (uint8_t)attempt;
    snprintf(worker->stage, sizeof(worker->stage), "%s", stage);
    snprintf(worker->stage_reason, sizeof(worker->stage_reason), "%s", reason);
}
static void hs_crack_remote_report_current(hs_crack_remote_worker_t *worker,
                                           const char *reason)
{
    hs_crack_remote_report(worker,
                           worker->stage[0] ? worker->stage : "start",
                           worker->stage_attempt ? worker->stage_attempt : 1U,
                           reason);
}
static bool hs_crack_remote_query_diag(hs_crack_remote_worker_t *worker)
{ (void)worker; return true; }
static void hs_crack_remote_sync_lost(hs_crack_remote_worker_t *worker,
                                      const char *reason)
{
    worker->failed = true;
    worker->usable = false;
    hs_crack_remote_report_current(worker, reason);
}
static __attribute__((unused)) void usb_log_cdc_state(const char *where)
{
    cdc_state_logs++;
    snprintf(last_cdc_state, sizeof(last_cdc_state), "%s", where);
}
static bool hs_crack_remote_send_line(int tab, int port, const char *command)
{
    (void)tab; (void)port;
    if (writes >= 8U) return false;
    snprintf(commands[writes], sizeof(commands[writes]), "%s", command);
    writes++;
    return true;
}
static bool hs_crack_remote_read_message(int tab, int port,
                                         hs_remote_message_t *message,
                                         uint32_t timeout_ms)
{
    (void)tab; (void)port;
    now_us += (int64_t)timeout_ms * 1000;
    if (event_index >= event_count) return false;
    event_t event = events[event_index++];
    if (!event.received) return false;
    *message = event.message;
    return true;
}

static void reset_fixture(void)
{
    memset(events, 0, sizeof(events));
    memset(commands, 0, sizeof(commands));
    memset(&hs_crack_ui, 0, sizeof(hs_crack_ui));
    event_count = event_index = 0;
    now_us = 0;
    writes = flushes = renders = cdc_state_logs = 0;
    last_cdc_state[0] = '\0';
}
static void push_timeout(void)
{
    events[event_count++].received = false;
}
static void push_message(hs_remote_message_type_t type, const char *job,
                         hs_remote_result_t result, uint64_t checked,
                         uint64_t offset, const char *code)
{
    event_t *event = &events[event_count++];
    event->received = true;
    event->message.type = type;
    event->message.result = result;
    event->message.checked = checked;
    event->message.offset = offset;
    if (job) snprintf(event->message.job, sizeof(event->message.job), "%s", job);
    if (code) snprintf(event->message.code, sizeof(event->message.code), "%s", code);
}
static void push_silent_start(void)
{
    for (unsigned i = 0; i < 10U; ++i) push_timeout();
}
static void push_found(hs_remote_message_type_t type, const char *job,
                       uint64_t checked, uint64_t offset,
                       const char *password, const char *ssid)
{
    push_message(type, job, HS_REMOTE_RESULT_FOUND, checked, offset, NULL);
    hs_remote_message_t *message = &events[event_count - 1U].message;
    message->password_length = strlen(password);
    memcpy(message->password, password, message->password_length);
    message->ssid_length = strlen(ssid);
    memcpy(message->ssid, ssid, message->ssid_length);
}
'''

for function_name in (
    "hs_crack_remote_start_confirmed",
    "hs_crack_remote_wait_start",
    "hs_crack_remote_start",
    "hs_crack_remote_add_checked",
    "hs_crack_remote_take_pending_found",
):
    HARNESS += production_function(function_name) + "\n"

HARNESS += r'''
static int failures;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; \
} } while (0)

static hs_crack_remote_worker_t worker_for(int tab)
{
    hs_crack_remote_worker_t worker = {0};
    worker.tab = tab;
    worker.port = tab;
    worker.usable = true;
    return worker;
}

int main(void)
{
    const uint64_t start = 7822632U;
    const uint64_t end = 11733948U;

    reset_fixture();
    hs_crack_remote_worker_t usb = worker_for(TAB_USB);
    push_message(HS_REMOTE_ACCEPTED, "t00000000w1", HS_REMOTE_RESULT_NONE,
                 0, 0, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 1U && flushes == 1U && cdc_state_logs == 0U);
    CHECK(usb.active && usb.lease.confirmed_safe_offset == start);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "stale_receive_error");
    push_message(HS_REMOTE_ACCEPTED, "t00000000w1", HS_REMOTE_RESULT_NONE,
                 0, 0, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 1U && usb.active && !usb.cancel_pending);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_message(HS_REMOTE_STATUS, "t00000000w1", HS_REMOTE_RESULT_RUNNING,
                 17, start + 168U, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 2U);
    CHECK(strncmp(commands[1], "crack_worker status t00000000w1", 32) == 0);
    CHECK(cdc_state_logs == 1U && strcmp(last_cdc_state, "start_timeout") == 0);
    CHECK(usb.active && usb.checked == 17U && usb.accounted == 0U);
    CHECK(usb.lease.confirmed_safe_offset == start + 168U);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_found(HS_REMOTE_STATUS, "t00000000w1", 23, start + 230U,
               "secretpass", "TestAP");
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(usb.pending_found);
    uint32_t tried = 0;
    char found_pw[64] = {0};
    CHECK(hs_crack_remote_take_pending_found(&usb, &tried,
                                              found_pw, sizeof(found_pw)));
    CHECK(strcmp(found_pw, "secretpass") == 0 && tried == 23U);
    CHECK(!usb.active && usb.finished && !usb.pending_found);
    CHECK(usb.found_ssid_length == 6U &&
          memcmp(usb.found_ssid, "TestAP", 6U) == 0);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_found(HS_REMOTE_DONE, "t00000000w1", 31, start + 310U,
               "donepass", "DoneAP");
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    tried = 0;
    memset(found_pw, 0, sizeof(found_pw));
    CHECK(hs_crack_remote_take_pending_found(&usb, &tried,
                                              found_pw, sizeof(found_pw)));
    CHECK(strcmp(found_pw, "donepass") == 0 && tried == 31U);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "unknown_job");
    push_message(HS_REMOTE_ACCEPTED, "t00000000w1", HS_REMOTE_RESULT_NONE,
                 0, 0, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 3U && flushes == 2U);
    CHECK(strcmp(commands[0], commands[2]) == 0);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "unknown_job");
    push_silent_start();
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "unknown_job");
    push_message(HS_REMOTE_ACCEPTED, "t00000000w1", HS_REMOTE_RESULT_NONE,
                 0, 0, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 5U && flushes == 3U);
    CHECK(strcmp(commands[0], commands[2]) == 0);
    CHECK(strcmp(commands[0], commands[4]) == 0);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "unknown_job");
    push_silent_start();
    push_message(HS_REMOTE_STATUS, "t00000000w1", HS_REMOTE_RESULT_RUNNING,
                 9, start + 91U, NULL);
    CHECK(hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                start, end, 1));
    CHECK(writes == 4U && flushes == 2U);
    CHECK(strcmp(commands[0], commands[2]) == 0);
    CHECK(strncmp(commands[3], "crack_worker status t00000000w1", 32) == 0);
    CHECK(usb.lease.confirmed_safe_offset == start + 91U);

    reset_fixture();
    usb = worker_for(TAB_USB);
    push_silent_start();
    push_message(HS_REMOTE_REJECTED, NULL, HS_REMOTE_RESULT_NONE,
                 0, 0, "busy");
    CHECK(!hs_crack_remote_start(&usb, 393, 1, 15645263, 2,
                                 start, end, 1));
    CHECK(writes == 2U && flushes == 1U);
    CHECK(usb.cancel_pending);

    reset_fixture();
    hs_crack_remote_worker_t mbus = worker_for(TAB_MBUS);
    push_silent_start();
    push_message(HS_REMOTE_STATUS, "t00000000w2", HS_REMOTE_RESULT_RUNNING,
                 3, start + 10U, NULL);
    CHECK(hs_crack_remote_start(&mbus, 393, 1, 15645263, 2,
                                start, end, 2));
    CHECK(writes == 2U && cdc_state_logs == 0U);

    if (failures) return 1;
    puts("usb_start_recovery_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    harness = temporary / "usb_start_recovery_contract.c"
    binary = temporary / "usb_start_recovery_contract"
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
