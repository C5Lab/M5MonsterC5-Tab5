"""Phase 4.7 dialog and back-route coverage audit.

Static reconciliation of the coverage ledger against the built emulator. No
browser: it reads ``coverage.json``, ``generated/manifest.json`` and
``control-contracts.json``, so it must run after a rebuild so the manifest
reflects the current slice policy.

It asserts that every ledger control and every back route is accounted for -
covered, an explicit unsupported boundary, or a catalogued open item - with no
uncategorized dead control, and that the live disposition still matches the
committed baseline. A deliberate slice change that wires or unwires a control
is expected to fail here until the baseline is regenerated:

    python tools/ui_emulator/audit_coverage.py --write-baseline
"""
import hashlib
import json
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ui_emulator'))
import audit_coverage as audit  # noqa: E402

REPORT = audit.ROOT / 'docs/ui-emulator/phase4-coverage-audit.json'


class CoverageAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = audit.compute()
        cls.baseline = audit.load_baseline()

    def test_every_ledger_control_is_categorized(self):
        """No dead control may ship without a documented reason."""
        self.assertEqual(self.result['uncategorized'], [],
                         'Uncategorized not-wired controls require triage')

    def test_all_back_routes_covered_or_deferred(self):
        """Every back route works, unless it is an explicitly deferred screen."""
        self.assertEqual(self.result['back_routes_uncovered'], [],
                         'Back routes must be covered or explicitly deferred')

    def test_open_items_are_identified(self):
        """Each open item carries a category and a human-readable reason."""
        for item in self.result['open_items']:
            self.assertIn('category', item, item)
            self.assertTrue(item.get('reason'), item)

    def test_replacement_controls_name_runtime_and_browser_proof(self):
        for item in self.result['replacements']:
            self.assertTrue((audit.ROOT / item['runtime']).is_file(), item)
            self.assertTrue((audit.ROOT / item['test']).is_file(), item)

    def test_matches_committed_baseline(self):
        """Guard against silent coverage regressions or stale baselines."""
        self.assertIsNotNone(self.baseline, 'Missing phase4-coverage-baseline.json')
        self.assertEqual(self.result['not_wired'], self.baseline['not_wired'],
                         'Set of inert controls drifted from the baseline; '
                         'regenerate with audit_coverage.py --write-baseline '
                         'after a deliberate slice change')
        self.assertEqual(self.result['replaced'], self.baseline['replaced'],
                         'Set of emulator-owned workflow replacements drifted')
        self.assertEqual(self.result['unsupported'], self.baseline['unsupported'],
                         'Unsupported-boundary set drifted from the baseline')
        self.assertGreaterEqual(self.result['summary']['covered'],
                                self.baseline['summary']['covered'],
                                'Covered-control count regressed below the baseline')

    def test_ledger_control_total_conserved(self):
        s = self.result['summary']
        self.assertEqual(s['total'],
                         s['retained'] + s['adapter'] + s['replaced'] +
                         s['unsupported'] + s['not-wired'])


class CoverageFreshness(unittest.TestCase):
    def compute_fixture(self, actual, recorded=None, manifest_sha=None, contracts_sha=None,
                        extra_screen=None):
        recorded_bytes = actual if recorded is None else recorded
        recorded_sha = hashlib.sha256(recorded_bytes.replace(b'\r\n', b'\n')).hexdigest()
        empty_sha = hashlib.sha256(b'').hexdigest()
        sources = {'main/main.c': recorded_sha,
                   'main/observer_view.inc': empty_sha,
                   'main/observer_details.inc': empty_sha}
        coverage = {'sources': sources, 'controls': [], 'routes': []}
        manifest = {'source_sha256': manifest_sha or recorded_sha,
                    'retained': [], 'boundaries': {}}
        contracts = {'sources': {**sources, 'main/main.c': contracts_sha or recorded_sha},
                     'controls': []}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'main/screens').mkdir(parents=True)
            (root / 'main/main.c').write_bytes(actual)
            (root / 'main/observer_view.inc').write_bytes(b'')
            (root / 'main/observer_details.inc').write_bytes(b'')
            if extra_screen is not None:
                (root / 'main/screens' / extra_screen).write_bytes(b'void new_screen(void) {}\n')
            with patch.object(audit, 'ROOT', root), patch.object(
                    audit, '_load', return_value=(coverage, manifest, contracts)):
                return audit.compute()

    def test_current_generated_inventory_is_fresh(self):
        coverage, manifest, contracts = audit._load()
        result = audit.compute()
        self.assertEqual(result['source_sha256'], coverage['sources']['main/main.c'])
        self.assertEqual(contracts['sources'], coverage['sources'])
        self.assertEqual(manifest['source_sha256'], result['source_sha256'])

    def test_source_changed_without_rebuilding_inventory_fails(self):
        with self.assertRaisesRegex(ValueError, 'main/main.c'):
            self.compute_fixture(b'int changed;\n', recorded=b'int original;\n')

    def test_new_screen_source_without_inventory_regeneration_fails(self):
        with self.assertRaisesRegex(ValueError, 'main/screens/new_screen.c'):
            self.compute_fixture(b'int value;\n', extra_screen='new_screen.c')

    def test_stale_generated_manifest_fails(self):
        with self.assertRaisesRegex(ValueError, 'manifest'):
            self.compute_fixture(b'int value;\n', manifest_sha='0' * 64)

    def test_stale_control_contracts_fail(self):
        with self.assertRaisesRegex(ValueError, 'control contracts'):
            self.compute_fixture(b'int value;\n', contracts_sha='0' * 64)

    def test_crlf_checkout_matches_lf_inventory(self):
        self.compute_fixture(b'int value;\r\n', recorded=b'int value;\n')


class CoverageReportStatus(unittest.TestCase):
    def report_status(self, result, baseline):
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / 'audit.json'
            with patch.object(audit, 'compute', return_value=result), patch.object(
                    audit, 'load_baseline', return_value=baseline), patch.object(
                    sys.modules[__name__], 'REPORT', report_path):
                return _write_report()['status']

    def fixtures(self):
        result = {'source_sha256': 'fixture', 'summary': {'covered': 3},
                  'not_wired': [], 'replaced': [], 'replacements': [], 'unsupported': [],
                  'back_routes_uncovered': [], 'back_routes': [],
                  'open_items': [], 'uncategorized': []}
        baseline = {'summary': {'covered': 3}, 'not_wired': [],
                    'replaced': [], 'unsupported': []}
        return result, baseline

    def test_report_passes_when_every_baseline_assertion_matches(self):
        result, baseline = self.fixtures()
        self.assertEqual(self.report_status(result, baseline), 'passed')

    def test_report_fails_when_any_baseline_disposition_drifts(self):
        for field in ('not_wired', 'replaced', 'unsupported'):
            with self.subTest(field=field):
                result, baseline = self.fixtures()
                result[field].append('changed-control')
                self.assertEqual(self.report_status(result, baseline), 'failed')

    def test_report_fails_when_covered_count_regresses(self):
        result, baseline = self.fixtures()
        result['summary']['covered'] = 2
        self.assertEqual(self.report_status(result, baseline), 'failed')


def _write_report():
    result = audit.compute()
    baseline = audit.load_baseline()
    report = {
        'status': 'passed' if (not result['uncategorized']
                               and not result['back_routes_uncovered']
                               and baseline is not None
                               and result['not_wired'] == baseline['not_wired']
                               and result['replaced'] == baseline['replaced']
                               and result['unsupported'] == baseline['unsupported']
                               and result['summary']['covered'] >=
                                   baseline['summary']['covered']) else 'failed',
        'recorded_at': datetime.now(timezone.utc).isoformat(),
        'source_sha256': result['source_sha256'],
        'summary': result['summary'],
        'open_items': result['open_items'],
        'replacements': result['replacements'],
        'unsupported': result['unsupported'],
        'back_routes': result['back_routes'],
        'uncategorized': result['uncategorized'],
        'limitations': [
            'Static ledger reconciliation; behaviour of covered controls is '
            'proven by the domain browser gates, not this audit.',
            'Workflow replacements do not compile the original firmware '
            'registration; each original action needs separate behavioral review.',
            'The remaining not-wired controls are boot/sleep chrome, ESP modem '
            'hardware, and WiGLE Wi-Fi credential prompts bypassed by the offline upload model.',
        ],
    }
    REPORT.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    _write_report()
    unittest.main(verbosity=2)
