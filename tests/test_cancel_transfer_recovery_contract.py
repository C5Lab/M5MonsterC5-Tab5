"""Host-execute the raw-transfer END drain used after user cancellation.

Regression caught: replacing the forced reader with the normal cancel-aware
reader strands the peer at the fast baud after Cancel.
"""

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


HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hs_crack_remote_core.h"

typedef int tab_id_t;
typedef int uart_port_t;

static bool cancel_requested;
static int64_t now_us;
static unsigned normal_reads;
static unsigned forced_reads;
static unsigned prompt_reads;

static int64_t esp_timer_get_time(void)
{
    return now_us;
}

static __attribute__((unused)) bool hs_crack_remote_read_message(
    tab_id_t tab, uart_port_t port, hs_remote_message_t *message,
    uint32_t timeout_ms)
{
    (void)tab;
    (void)port;
    (void)message;
    normal_reads++;
    now_us += (int64_t)timeout_ms * 1000;
    return !cancel_requested;
}

static __attribute__((unused)) bool hs_crack_remote_read_message_force(
    tab_id_t tab, uart_port_t port, hs_remote_message_t *message,
    uint32_t timeout_ms)
{
    (void)tab;
    (void)port;
    (void)timeout_ms;
    forced_reads++;
    memset(message, 0, sizeof(*message));
    message->type = HS_REMOTE_END;
    return true;
}

static bool janos_uart_read_line(tab_id_t tab, uart_port_t port,
                                 char *line, size_t line_size,
                                 uint32_t timeout_ms)
{
    (void)tab;
    (void)port;
    (void)timeout_ms;
    prompt_reads++;
    if (line_size > 0U) line[0] = '\0';
    return true;
}

static int failures;
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)
'''

HARNESS += "\n" + production_function("hs_crack_remote_wait_end") + "\n"
HARNESS += r'''
int main(void)
{
    cancel_requested = true;
    CHECK(hs_crack_remote_wait_end(0, 0, 12000));
    CHECK(normal_reads == 0U);
    CHECK(forced_reads == 1U);
    CHECK(prompt_reads == 1U);
    if (failures != 0) return 1;
    puts("cancel_transfer_recovery_contract: PASS");
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="cancel_transfer_recovery_") as temp_dir:
    binary = str(Path(temp_dir) / "cancel_transfer_recovery_test")
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
