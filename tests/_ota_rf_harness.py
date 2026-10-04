"""Shared helper: host-compile a C harness against the real main/ota_rf.c.

The Monster OTA / JanOS RF logic under test lives in main/ota_rf.c and is linked
into the firmware unchanged. These contract tests compile that exact source with
a small C harness so they exercise the production parser and decision code, not a
reimplementation in Python.

On a Windows workstation without a host compiler, run the same command inside WSL
against the /mnt/c/... checkout (see tests/README.md).
"""

import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OTA_RF_C = ROOT / "main" / "ota_rf.c"
MAIN_INC = ROOT / "main"


def _find_compiler() -> str:
    for name in ("cc", "gcc", "clang"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError(
        "No host C compiler (cc/gcc/clang) found. On Windows, run this test "
        "inside WSL against the /mnt/c/... checkout (see tests/README.md)."
    )


def run_harness(harness_c: str) -> subprocess.CompletedProcess:
    """Compile `harness_c` with main/ota_rf.c and run it. Returns the result."""
    cc = _find_compiler()
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "harness.c"
        src.write_text(harness_c, encoding="utf-8")
        exe = Path(td) / "harness"
        compile_cmd = [
            cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-g",
            f"-I{MAIN_INC}",
            "-o",
            str(exe),
            str(src),
            str(OTA_RF_C),
        ]
        subprocess.run(compile_cmd, check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)


# A tiny assertion harness prelude shared by every test's C body. It defines
# EXPECT(cond) and a main() that the test appends `run_all()` bodies to.
HARNESS_PRELUDE = r"""
#include "ota_rf.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_checks = 0;
#define EXPECT(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

__attribute__((unused))
static void feed_lines(ota_info_report_t *r, const char *const *lines, int n) {
    ota_info_report_reset(r);
    for (int i = 0; i < n; i++) ota_info_report_feed_line(r, lines[i]);
}
"""


def assert_ok(result: subprocess.CompletedProcess) -> None:
    if result.returncode != 0:
        raise AssertionError(
            "harness reported failures:\n" + result.stdout + result.stderr
        )
