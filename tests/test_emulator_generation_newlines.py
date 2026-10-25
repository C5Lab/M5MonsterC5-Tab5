"""Windows generation must preserve native event registration line numbers."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ui_emulator'))
from prepare_browser import write_changed
from source_index import Source, source_bytes


class GeneratedNewlines(unittest.TestCase):
    def test_source_index_uses_lf_for_checkout_line_endings(self):
        source = b'static int sample(void) {\n    return 7;\n}\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sample.c'
            path.write_bytes(source.replace(b'\n', b'\r\n'))
            self.assertEqual(source_bytes(path), source)
            indexed = Source(path)
            self.assertEqual(indexed.data, source)
            self.assertEqual(indexed.function('sample'), source.decode().rstrip())

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
