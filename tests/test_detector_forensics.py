"""Compile the production Tab5 forensic parser and bounded model as host C."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SANITIZER_FLAGS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]


class DetectorForensicsTests(unittest.TestCase):
    def test_strict_parser_bursts_sequences_stats_and_capacity(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        model = source[source.index("// Detector forensic model begin"):
                       source.index("// Detector forensic model end")]
        payload = source[source.index("static const char *detector_watch_status_payload("):
                         source.index("static bool detector_watch_payload_starts(")]
        logic = source[source.index("// Detector forensic logic begin"):
                       source.index("// Detector forensic logic end")]
        harness = (ROOT / "tests/detector_forensic_test.c").read_text(encoding="utf-8")
        harness = (harness.replace("/* MODEL */", model)
                          .replace("/* LOGIC */", payload + "\n" + logic))
        with tempfile.TemporaryDirectory() as temporary:
            test = Path(temporary) / "test.c"
            test.write_text(harness, encoding="utf-8")
            compiler = shutil.which("gcc") or shutil.which("cc")
            if compiler:
                executable = Path(temporary) / "test.exe"
                subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                *SANITIZER_FLAGS,
                                str(test), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)
            else:
                subprocess.run([
                    "docker", "run", "--rm", "--mount",
                    f"type=bind,source={temporary},target=/work",
                    "emscripten/emsdk:4.0.14", "bash", "-lc",
                    "cc -std=c11 -Wall -Wextra -Werror "
                    "-fsanitize=address,undefined -fno-omit-frame-pointer "
                    "/work/test.c -o /work/test && /work/test",
                ], check=True)


if __name__ == "__main__":
    unittest.main()
