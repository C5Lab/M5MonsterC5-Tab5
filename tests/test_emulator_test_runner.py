"""Small subprocess checks; no browser or emulator build required."""
import os
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

class RunnerTests(unittest.TestCase):
    def test_nested_sections_have_one_summary_and_preserve_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            child = base / 'verify_emulator_child.py'
            child.write_text("import sys\nfrom emulator_test_runner import run_logged\nr=run_logged([sys.executable,'-c','print(123)'],cwd='.')\nraise SystemExit(r.returncode)\n")
            env = dict(os.environ, PYTHONPATH=str(Path(__file__).parent.resolve()),
                       EMULATOR_TEST_REPORT_DIR=directory)
            env.pop('EMULATOR_TEST_SESSION', None)
            code = ("import sys\nfrom emulator_test_runner import run_logged\n"
                    f"run_logged([sys.executable,{str(child)!r}],cwd='.')\n"
                    "run_logged(['nonexistent-emulator-test-executable'],cwd='.')\n")
            result = subprocess.run([sys.executable, '-c', code], env=env,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout.count('TEST SUMMARY'), 1)
            report = json.loads(next(base.rglob('summary.json')).read_text())
            self.assertEqual(report['status'], 'FAIL')
            self.assertEqual(len(report['checks']), 3)
            self.assertEqual({r['status'] for r in report['checks']}, {'PASS', 'ERROR'})
            self.assertIn('child', {r['section'] for r in report['checks']})
            self.assertTrue(all(r['seconds'] >= 0 for r in report['checks']))

    def test_nested_results_times_logs_and_plain_terminal(self):
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, PYTHONPATH=str(Path(__file__).parent.resolve()),
                       EMULATOR_TEST_REPORT_DIR=directory)
            env.pop('EMULATOR_TEST_SESSION', None)
            code = """
import sys
from emulator_test_runner import run_logged
r = run_logged([sys.executable, '-c', "print('\\x1b[31mred\\x1b[0m');print('\\x1b]0;title\\x07');raise SystemExit(3)"], cwd='.')
assert r.returncode == 3
assert '\\x1b' not in r.stdout
"""
            result = subprocess.run([sys.executable, '-c', code], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('\x1b', result.stdout)
            self.assertIn('TEST SUMMARY', result.stdout)
            self.assertIn('FAIL', result.stdout)
            self.assertIn('Total elapsed:', result.stdout)
            self.assertTrue(list(Path(directory).rglob('summary.json')))
            self.assertTrue(list(Path(directory).rglob('*.log')))

if __name__ == '__main__':
    unittest.main()
