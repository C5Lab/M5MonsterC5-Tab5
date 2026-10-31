"""Execute the production JanOS detector event parsers on the host."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DetectorCsaTests(unittest.TestCase):
    def test_real_deauth_and_csa_parsers(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        self.assertTrue("static bool parse_csa_line(" in source,
                      "CSA wire events need their own production parser")
        model = source[source.index("// Deauth Detector entry"):source.index("// Detector watch model begin")]
        parser = source[source.index("// Parse CSA event"):source.index("// Deauth detector monitor task")]
        harness = (ROOT / "tests/deauth_detector_test.c").read_text(encoding="utf-8")
        for marker, value in [("MODEL", model), ("PARSER", parser)]:
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
