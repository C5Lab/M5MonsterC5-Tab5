"""Phase 4.7 detectors gate (Deauth Detector, Anti-surveillance, Handshaker),
including previous acceptance. Rebuild first."""
from emulator_test_runner import run_logged
import json, sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint

path = ROOT / 'docs/ui-emulator/phase4-detectors-verification.json'
report = {'status': 'running', 'recorded_at': datetime.now(timezone.utc).isoformat(), 'checks': [],
          'limitations': ['Deauth Detector, Anti-surveillance and Handshaker; SAE is verified by the separate SAE gate; global Handshaker is verified by the separate global attacks gate',
                          'Detected frames/followers/handshakes are synthesized from the scenario; no real radio monitoring',
                          'Chromium acceptance only; real hardware and cross-browser checks remain pending']}
try:
    fingerprint = artifact_fingerprint(); report.update(fingerprint)
    for command in [[sys.executable, 'tests/verify_emulator_phase4_settings_sys.py'],
                    ['node', '--test', 'tests/test_emulator_detectors.mjs'],
                    [sys.executable, 'tests/test_emulator_phase4_detectors.py']]:
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
