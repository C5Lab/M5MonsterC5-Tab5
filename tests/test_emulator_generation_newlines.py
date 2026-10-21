"""Windows generation must preserve native event registration line numbers."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ui_emulator'))
from prepare_browser import write_changed


class GeneratedNewlines(unittest.TestCase):
    def test_crlf_source_becomes_single_lf_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'browser.c'
            write_changed(path, '#line 12 "main/main.c"\r\nfoo();\r\nbar();\n')
            self.assertEqual(path.read_bytes(), b'#line 12 "main/main.c"\nfoo();\nbar();\n')
            stamp = path.stat().st_mtime_ns
            write_changed(path, '#line 12 "main/main.c"\r\nfoo();\r\nbar();\r\n')
            self.assertEqual(path.stat().st_mtime_ns, stamp)


if __name__ == '__main__':
    unittest.main()
