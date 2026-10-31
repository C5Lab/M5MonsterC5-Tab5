"""Execute the production detector parser, renderer and UART loop on the host.

LVGL and FreeRTOS are boundary fakes; parser/row generation/task bodies come
from main.c. Literal wire records independently define the expected values.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    match = re.search(re.escape(signature) + r"\([^;{}]*\)\s*\{", source)
    if not match:
        raise AssertionError(f"missing production function: {signature}")
    opening = source.index("{", match.start())
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():pos + 1]
    raise AssertionError("unterminated function")


class DetectorCsaTests(unittest.TestCase):
    def test_real_parser_rows_transport_and_commands(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        self.assertTrue("static bool parse_csa_line(" in source,
                      "CSA wire events need their own production parser")
        model = source[source.index("// Deauth Detector entry"):source.index("// BT device storage")]
        parser = source[source.index("// Parse CSA event"):source.index("// Deauth detector monitor task")]
        table = definition(source, "static void update_deauth_table")
        task = definition(source, "static void deauth_detector_task")
        callbacks = "\n".join(definition(source, "static void " + name) for name in
                              ["deauth_detector_start_cb", "deauth_detector_stop_cb"])
        runtime = (ROOT / "tools/ui_emulator/runtime/detectors.c").read_text(encoding="utf-8")
        adapter = runtime[runtime.index("static struct {"):runtime.index("/* ==========================================================================")]
        harness = (ROOT / "tests/deauth_detector_test.c").read_text(encoding="utf-8")
        for marker, value in [("MODEL", model), ("PARSER", parser), ("TABLE", table),
                              ("TASK", task), ("CALLBACKS", callbacks), ("ADAPTER", adapter)]:
            harness = harness.replace("/* " + marker + " */", value)
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "test.c").write_text(harness, encoding="utf-8")
            compiler = shutil.which("gcc") or shutil.which("cc")
            if compiler:
                executable = folder / "test.exe"
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                str(folder / "test.c"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            else:
                subprocess.run(["docker", "run", "--rm", "--mount",
                                f"type=bind,source={folder},target=/work",
                                "emscripten/emsdk:4.0.14", "bash", "-lc",
                                "cc -std=c11 -Wall -Wextra -Werror /work/test.c -o /work/test && /work/test"],
                               check=True)


if __name__ == "__main__":
    unittest.main()
