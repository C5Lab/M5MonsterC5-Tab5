"""Host-execute the production lost-worker ping and status reconciliation."""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(
        r"static [^\n]+ " + re.escape(name) + r"\([^;]+?\n\{.*?\n\}",
        SOURCE,
        re.S,
    )
    if not match:
        raise RuntimeError(f"production function missing: {name}")
    return match.group(0)


PING = production_function("hs_crack_remote_recovery_ping")
STATUS = production_function("hs_crack_remote_recovery_status")

assert "detect_boards" not in PING
assert "ping_usb" not in PING
assert "ping_uart" not in PING

HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hs_crack_remote_core.h"
#include "hs_crack_scheduler.h"

typedef int tab_id_t;
typedef int uart_port_t;
#define HS_CRACK_REMOTE_LINE_BYTES 640

static int64_t now_us;
static const char *raw_lines[16];
static size_t raw_count;
static size_t raw_pos;
static hs_remote_message_t messages[8];
static size_t message_count;
static size_t message_pos;
static char sent[8][96];
static size_t sent_count;

static int64_t esp_timer_get_time(void)
{
    return now_us;
}

static bool hs_crack_remote_send_line(tab_id_t tab, uart_port_t port,
                                      const char *line)
{
    (void)tab;
    (void)port;
    if (sent_count >= 8U) return false;
    snprintf(sent[sent_count++], sizeof(sent[0]), "%s", line);
    return true;
}

static bool janos_uart_read_line(tab_id_t tab, uart_port_t port,
                                 char *line, size_t line_size,
                                 uint32_t timeout_ms)
{
    (void)tab;
    (void)port;
    now_us += (int64_t)timeout_ms * 1000;
    if (raw_pos >= raw_count) return false;
    snprintf(line, line_size, "%s", raw_lines[raw_pos++]);
    return true;
}

static bool hs_crack_remote_read_message_force(
    tab_id_t tab, uart_port_t port, hs_remote_message_t *message,
    uint32_t timeout_ms)
{
    (void)tab;
    (void)port;
    now_us += (int64_t)timeout_ms * 1000;
    if (message_pos >= message_count) return false;
    *message = messages[message_pos++];
    return true;
}

static void reset_fixture(void)
{
    now_us = 0;
    raw_count = 0;
    raw_pos = 0;
    message_count = 0;
    message_pos = 0;
    sent_count = 0;
    memset(messages, 0, sizeof(messages));
    memset(sent, 0, sizeof(sent));
}

static void push_message(hs_remote_message_type_t type,
                         hs_remote_result_t result,
                         const char *job, const char *code)
{
    hs_remote_message_t *message = &messages[message_count++];
    memset(message, 0, sizeof(*message));
    message->type = type;
    message->result = result;
    if (job) snprintf(message->job, sizeof(message->job), "%s", job);
    if (code) snprintf(message->code, sizeof(message->code), "%s", code);
}

static int failures;
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)
'''

HARNESS += "\n" + PING + "\n" + STATUS + "\n"
HARNESS += r'''
int main(void)
{
    hs_remote_message_t reply;

    reset_fixture();
    raw_lines[raw_count++] = "JanOS version: 1.7.5";
    raw_lines[raw_count++] = ">";
    raw_lines[raw_count++] = "pong";
    CHECK(hs_crack_remote_recovery_ping(0, 0));
    CHECK(sent_count == 1U);
    CHECK(strcmp(sent[0], "ping") == 0);

    reset_fixture();
    CHECK(!hs_crack_remote_recovery_ping(0, 0));
    CHECK(now_us >= 1500000LL);

    reset_fixture();
    push_message(HS_REMOTE_STATUS, HS_REMOTE_RESULT_RUNNING, "old-job", NULL);
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_OLD_JOB_RUNNING);
    CHECK(strcmp(sent[0], "crack_worker status old-job") == 0);

    reset_fixture();
    push_message(HS_REMOTE_DONE, HS_REMOTE_RESULT_FOUND, "old-job", NULL);
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_OLD_JOB_FOUND);

    reset_fixture();
    push_message(HS_REMOTE_DONE, HS_REMOTE_RESULT_NOT_FOUND, "old-job", NULL);
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_OLD_JOB_NOT_FOUND);

    reset_fixture();
    push_message(HS_REMOTE_REJECTED, HS_REMOTE_RESULT_NONE, NULL, "unknown_job");
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_UNKNOWN_JOB);

    reset_fixture();
    push_message(HS_REMOTE_STATUS, HS_REMOTE_RESULT_RUNNING, "other-job", NULL);
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_AMBIGUOUS);

    reset_fixture();
    CHECK(hs_crack_remote_recovery_status(0, 0, "old-job", &reply) ==
          HS_SCHED_AMBIGUOUS);
    CHECK(now_us >= 2000000LL);

    if (failures != 0) return 1;
    puts("worker_auto_recovery_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="worker_auto_recovery_") as temp_dir:
    binary = str(Path(temp_dir) / "worker_auto_recovery_test")
    subprocess.run(
        [
            "gcc",
            "-x",
            "c",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-o",
            binary,
            "-",
        ],
        input=HARNESS,
        text=True,
        check=True,
    )
    subprocess.run([binary], check=True)
