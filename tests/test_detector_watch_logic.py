"""Run the native Tab5 detector selection and response state against literal JanOS lines."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DetectorWatchLogicTests(unittest.TestCase):
    def test_scan_selection_ack_watch_and_two_sessions(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        model = source[source.index("// Detector watch model begin"):source.index("// Detector watch model end")]
        logic = source[source.index("// Detector watch logic begin"):source.index("// Detector watch logic end")]
        harness = (ROOT / "tests/detector_watch_logic_test.c").read_text(encoding="utf-8")
        harness = harness.replace("/* MODEL */", model).replace("/* LOGIC */", logic)
        with tempfile.TemporaryDirectory() as temp:
            test = Path(temp) / "test.c"
            test.write_text(harness, encoding="utf-8")
            compiler = shutil.which("gcc") or shutil.which("cc")
            if compiler:
                executable = Path(temp) / "test.exe"
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            else:
                subprocess.run(["docker", "run", "--rm", "--mount",
                                f"type=bind,source={temp},target=/work",
                                "emscripten/emsdk:4.0.14", "bash", "-lc",
                                "cc -std=c11 -Wall -Wextra -Werror /work/test.c -o /work/test && /work/test"],
                               check=True)

    def test_scan_start_and_stop_keep_captured_tab_and_port(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        send = source[source.index("static bool deauth_detector_send_bound("):
                      source.index("static void deauth_detector_render_status(")]
        harness = (ROOT / "tests/detector_transport_binding_test.c").read_text(encoding="utf-8")
        harness = harness.replace("/* SEND */", send)
        with tempfile.TemporaryDirectory() as temp:
            test = Path(temp) / "test.c"
            test.write_text(harness, encoding="utf-8")
            compiler = shutil.which("gcc") or shutil.which("cc")
            if compiler:
                executable = Path(temp) / "test.exe"
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                str(test), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            else:
                subprocess.run(["docker", "run", "--rm", "--mount",
                                f"type=bind,source={temp},target=/work",
                                "emscripten/emsdk:4.0.14", "bash", "-lc",
                                "cc -std=c11 -Wall -Wextra -Werror /work/test.c -o /work/test && /work/test"],
                               check=True)


if __name__ == "__main__":
    unittest.main()
