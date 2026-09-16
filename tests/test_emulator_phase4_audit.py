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
import json
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

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

    def test_matches_committed_baseline(self):
        """Guard against silent coverage regressions or stale baselines."""
        self.assertIsNotNone(self.baseline, 'Missing phase4-coverage-baseline.json')
        self.assertEqual(self.result['not_wired'], self.baseline['not_wired'],
                         'Set of inert controls drifted from the baseline; '
                         'regenerate with audit_coverage.py --write-baseline '
                         'after a deliberate slice change')
        self.assertEqual(self.result['unsupported'], self.baseline['unsupported'],
                         'Unsupported-boundary set drifted from the baseline')
        self.assertGreaterEqual(self.result['summary']['covered'],
                                self.baseline['summary']['covered'],
                                'Covered-control count regressed below the baseline')

    def test_ledger_control_total_conserved(self):
        s = self.result['summary']
        self.assertEqual(s['total'],
                         s['retained'] + s['adapter'] + s['unsupported'] + s['not-wired'])


def _write_report():
    result = audit.compute()
    baseline = audit.load_baseline()
    report = {
        'status': 'passed' if (not result['uncategorized']
                               and not result['back_routes_uncovered']
                               and baseline is not None
                               and result['not_wired'] == baseline['not_wired']) else 'failed',
        'recorded_at': datetime.now(timezone.utc).isoformat(),
        'source_sha256': result['source_sha256'],
        'summary': result['summary'],
        'open_items': result['open_items'],
        'unsupported': result['unsupported'],
        'back_routes': result['back_routes'],
        'uncategorized': result['uncategorized'],
        'limitations': [
            'Static ledger reconciliation; behaviour of covered controls is '
            'proven by the domain browser gates, not this audit.',
            'Open candidates are reachable dialogs inside covered flows; '
            'deferred items are out of the current emulator scope.',
        ],
    }
    REPORT.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    _write_report()
    unittest.main(verbosity=2)
