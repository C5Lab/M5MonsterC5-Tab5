"""First Phase 5 slice: settings persistence/reset plus the full Phase 4 gate."""
import json
import sys
from datetime import datetime, timezone
from emulator_test_runner import run_logged
from emulator_browser import ROOT, artifact_fingerprint

path = ROOT / 'docs/ui-emulator/phase5-settings-verification.json'
report = {'status': 'running', 'recorded_at': datetime.now(timezone.utc).isoformat(), 'checks': [],
          'limitations': ['Guides S01-S21 and S27 included; deferred scope is tracked in Stories_TODO.md; virtual SD stays volatile by user decision',
                         'If both storage tiers fail, edits last until reload; reset intent is retained in the URL',
                         'Chromium only; hardware and cross-browser checks remain open']}
try:
    fingerprint = artifact_fingerprint()
    report.update(fingerprint)
    for command in [['node', '--test', 'tests/test_emulator_persistence.mjs'],
                    [sys.executable, 'tests/test_emulator_adapter_sites.py'],
                    [sys.executable, 'tests/test_emulator_phase5_settings.py'],
                    [sys.executable, 'tests/verify_emulator_phase4.py'],
                    [sys.executable, 'tests/verify_emulator_time.py'],
                    [sys.executable, 'tests/verify_emulator_portal_demo.py'],
                    [sys.executable, 'tests/verify_emulator_radar.py'],
                    [sys.executable, 'tests/verify_emulator_settings_followup.py'],
                    [sys.executable, 'tests/verify_emulator_guided_dialogs.py']]:
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
        report['checks'].append({'command': command, 'exit_code': result.returncode,
                                 'output': result.stdout + result.stderr})
        if result.returncode:
            raise RuntimeError('Check failed: ' + command[-1])
    if fingerprint != artifact_fingerprint():
        raise RuntimeError('Build changed during verification')
    report['status'] = 'passed'
except Exception as error:
    report.update(status='failed', error=str(error))
    raise
finally:
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
