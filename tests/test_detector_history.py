"""Compile the production Tab5 detector history model as a host-only C test."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DetectorHistoryTests(unittest.TestCase):
    def test_grouping_timestamps_order_and_capacity(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        model = source[source.index("// Deauth Detector entry"):
                       source.index("// Detector watch model begin")]
        harness = (ROOT / "tests/detector_history_test.c").read_text(encoding="utf-8")
        test_source = harness.replace("/* MODEL */", model)
        with tempfile.TemporaryDirectory() as temporary:
            test = Path(temporary) / "test.c"
            test.write_text(test_source, encoding="utf-8")
            compiler = shutil.which("gcc") or shutil.which("cc")
            if compiler:
                executable = Path(temporary) / "test.exe"
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                str(test), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            else:
                subprocess.run(["docker", "run", "--rm", "--mount",
                                f"type=bind,source={temporary},target=/work",
                                "emscripten/emsdk:4.0.14", "bash", "-lc",
                                "cc -std=c11 -Wall -Wextra -Werror /work/test.c -o /work/test && /work/test"],
                               check=True)


if __name__ == "__main__":
    unittest.main()
