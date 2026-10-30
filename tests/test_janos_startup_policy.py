"""Exercise the startup policy helpers extracted from the real Tab5 C source."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main" / "main.c"


def function_body(source: str, name: str) -> str:
    import re

    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\([^;\n]*\)\s*\{", source, re.M)
    if match is None:
        raise AssertionError(f"missing production helper {name}")
    opening = match.end() - 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start() : end]


def wsl_path(path: Path) -> str:
    path = path.resolve()
    return f"/mnt/{path.drive[0].lower()}/{path.as_posix()[3:]}"


class JanosStartupPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            function_body(source, name)
            for name in (
                "janos_version_is_confirmed_stop_capable",
                "janos_parse_wps_status_line",
                "janos_wps_phase_is_active",
                "janos_scan_line_is_busy",
            )
        )
        cls.temp = tempfile.TemporaryDirectory(prefix="janos-startup-")
        folder = Path(cls.temp.name)
        program = folder / "test.c"
        cls.executable = folder / ("test.exe" if os.name == "nt" else "test")
        program.write_text(
            '#include <stdbool.h>\n#include <stddef.h>\n#include <stdio.h>\n'
            '#include <stdlib.h>\n#include <string.h>\n#include <ctype.h>\n'
            f"{functions}\n"
            'int main(int argc, char **argv) {\n'
            '  if (argc != 3) return 2;\n'
            '  if (!strcmp(argv[1], "version")) {\n'
            '    printf("%d", janos_version_is_confirmed_stop_capable(argv[2]));\n'
            '  } else if (!strcmp(argv[1], "status")) {\n'
            '    char phase[16] = {0};\n'
            '    bool valid = janos_parse_wps_status_line(argv[2], phase, sizeof(phase));\n'
            '    printf("%d:%s:%d", valid, phase, valid && janos_wps_phase_is_active(phase));\n'
            '  } else if (!strcmp(argv[1], "busy")) {\n'
            '    printf("%d", janos_scan_line_is_busy(argv[2]));\n'
            '  } else return 2;\n'
            '  return 0;\n'
            '}\n',
            encoding="utf-8",
        )
        if shutil.which("cc"):
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(program), "-o", str(cls.executable)],
                check=True,
            )
            cls.run_prefix = []
        elif os.name == "nt" and shutil.which("wsl"):
            subprocess.run(
                ["wsl", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 wsl_path(program), "-o", wsl_path(cls.executable)],
                check=True,
            )
            cls.run_prefix = ["wsl", "--exec"]
            cls.executable = wsl_path(cls.executable)
        else:
            raise unittest.SkipTest("No host C compiler available")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def run_policy(self, mode: str, value: str) -> str:
        return subprocess.check_output(
            [*self.run_prefix, str(self.executable), mode, value], text=True
        ).strip()

    def test_only_stable_janos_1_7_7_or_later_confirms_stop(self):
        for version, expected in (
            ("1.7.6", "0"), ("1.7.7", "1"), ("1.7.8", "1"),
            ("1.8.0", "1"), ("2.0.0", "1"), ("1.7.7-rc1", "0"),
            ("unknown", "0"), ("<1.5.8", "0"), ("1.7", "0"),
            ("1.7.7junk", "0"),
        ):
            with self.subTest(version=version):
                self.assertEqual(expected, self.run_policy("version", version))

    def test_wps_status_parser_accepts_only_schema_one_status_records(self):
        for response, expected in (
            ('{"schema_version":1,"record_type":"wps_status","phase":"running","run_id":7}', "1:running:1"),
            ('{"schema_version":1,"record_type":"wps_status","phase":"preparing"}', "1:preparing:1"),
            ('{"schema_version":1,"record_type":"wps_status","phase":"cleanup"}', "1:cleanup:1"),
            ('{"schema_version":1,"record_type":"wps_status","phase":"quarantined"}', "1:quarantined:1"),
            ('{"schema_version":1,"record_type":"wps_status","phase":"idle"}', "1:idle:0"),
            ('{"schema_version":1,"record_type":"terminal","phase":"running"}', "0::0"),
            ('{"schema_version":2,"record_type":"wps_status","phase":"running"}', "0::0"),
            ('{"schema_version":1,"record_type":"wps_status","phase":"bogus"}', "0::0"),
        ):
            with self.subTest(response=response):
                self.assertEqual(expected, self.run_policy("status", response))

    def test_radio_refusal_is_not_a_scan_result(self):
        for line, expected in (
            ("FAILED: radio busy (wps). Use 'stop' first.", "1"),
            ("WiFi init failed: 259", "1"),
            ("Scan already in progress. Use 'show_scan_results'", "1"),
            ("Wardrive is active. Use 'stop' to stop it first before scanning.", "1"),
            ("FAILED: radio_lease owner=wps", "1"),
            ('"1","radio busy cafe","","00:11:22:33:44:55","6","WPA2","-40","2.4GHz"', "0"),
            ("Scan results printed", "0"),
        ):
            with self.subTest(line=line):
                self.assertEqual(expected, self.run_policy("busy", line))


if __name__ == "__main__":
    unittest.main()
