"""Phase 4 roll-up gate (closeout 4.7).

Runs the whole Phase 4 acceptance on one static build: the cumulative Phase 4.7
detectors gate (which chains 4.6 -> 4.5 -> 4.4 -> 4.3 -> 4.2 -> 4.1 -> Phase 3 ->
Phase 2), then the Phase 4.7 dialog and back-route coverage audit. Rebuild the
emulator first.

Note: wiring the Deauth Detector moved its controls from not-wired to covered, so
after the first rebuild regenerate the audit baseline once:
    python tools/ui_emulator/audit_coverage.py --write-baseline
"""
from emulator_test_runner import run_logged
import json, sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint

path = ROOT / 'docs/ui-emulator/phase4-verification.json'
report = {'status': 'running', 'recorded_at': datetime.now(timezone.utc).isoformat(), 'checks': [],
          'limitations': ['Chromium acceptance only; real hardware and cross-browser checks remain Phase 6',
                          'Deferred scope is identified in the coverage audit and Stories_TODO.md',
                          'Guided stories are additionally covered by the enclosing Phase 5 gate']}
try:
    fingerprint = artifact_fingerprint(); report.update(fingerprint)
    for command in [[sys.executable, 'tests/verify_emulator_phase4_detectors.py'],
                    [sys.executable, 'tests/verify_emulator_sae.py'],
                    [sys.executable, 'tests/verify_emulator_global_attacks.py'],
                    [sys.executable, 'tests/test_emulator_phase4_audit.py']]:
        print('RUN ' + ' '.join(command[1:]), flush=True)
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
        report['checks'].append({'command': command, 'exit_code': result.returncode, 'output': result.stdout + result.stderr})
        print(('PASS ' if result.returncode == 0 else 'FAIL ') + command[-1], flush=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    if fingerprint != artifact_fingerprint():
        raise RuntimeError('Build changed during verification')
    report['status'] = 'passed'
except Exception as error:
    report.update(status='failed', error=str(error)); raise
finally:
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
