"""Record the Phase 4.1 Bluetooth gate only when the Bluetooth workflow and the
whole Phase 3 gate pass on one static build."""
from emulator_test_runner import run_logged
import json
import subprocess
import sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint

destination = ROOT / 'docs/ui-emulator/phase4-bluetooth-verification.json'
report = {'status': 'running', 'recorded_at': datetime.now(timezone.utc).isoformat(),
          'scope': 'Bluetooth discovery and locate plus the full Phase 3 regression on one build',
          'checks': [],
          'limitations': ['Synthetic BLE fixtures only; no real radio or pairing',
                          'AirTag scan and the nRF24 jammer remain explicit unsupported boundaries',
                          'Physical mobile, Firefox/WebKit and full application coverage remain Phase 6/4 work']}
try:
    fingerprint = artifact_fingerprint()
    if not fingerprint['artifact_sha256'].get('emulator.wasm'):
        raise RuntimeError('Build the emulator before running this gate')
    report.update(fingerprint)
    # The Phase 3 gate runs first: the Bluetooth slice widens the same build, so
    # the earlier Phase 3 result does not carry over to this artifact.
    commands = [[sys.executable, 'tests/verify_emulator_phase3.py'],
                [sys.executable, 'tests/test_emulator_phase4_bluetooth.py'],
                [sys.executable, 'tests/test_emulator_bluetooth_lifecycle.py']]
    for command in commands:
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        report['checks'].append({'command': command, 'exit_code': result.returncode,
                                 'output': result.stdout + result.stderr})
        print(('PASS ' if result.returncode == 0 else 'FAIL ') + command[-1], flush=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    evidence = json.loads((destination.parent / 'phase4-bluetooth-report.json').read_text(encoding='utf-8'))
    if evidence['status'] != 'passed' or evidence['artifact_sha256'] != fingerprint['artifact_sha256']:
        raise RuntimeError('Failed or stale report: phase4-bluetooth-report.json')
    if json.loads((destination.parent / 'phase3-verification.json').read_text(encoding='utf-8'))['status'] != 'passed':
        raise RuntimeError('Failed report: phase3-verification.json')
    if artifact_fingerprint() != fingerprint:
        raise RuntimeError('Static build changed during verification; rerun on a stable build')
    report['status'] = 'passed'
except Exception as error:
    report.update(status='failed', error=str(error))
    raise
finally:
    destination.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
