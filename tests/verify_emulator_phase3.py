"""Record the Phase 3 gate only when the model, the capture reader, the browser
workflow and the whole Phase 2 regression set pass on one static build."""
from emulator_test_runner import run_logged
import json
import subprocess
import sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint

destination = ROOT / 'docs/ui-emulator/phase3-verification.json'
report = {'status': 'running', 'recorded_at': datetime.now(timezone.utc).isoformat(),
          'scope': 'Shared scenario: scan, clients, synthetic capture, production PCAP analysis and lifecycle regression',
          'checks': [],
          'limitations': ['Synthetic Ethernet/ARP fixtures only: no radio capture or authentication',
                          'Advanced analysis tools, extraction and full application coverage remain Phase 4 work',
                          'Physical mobile, Firefox/WebKit and full application coverage remain Phase 6/4 work']}
try:
    fingerprint = artifact_fingerprint()
    if not fingerprint['artifact_sha256'].get('emulator.wasm'):
        raise RuntimeError('Build the emulator before running this gate')
    report.update(fingerprint)
    # The Phase 2 gate runs first: the Observer slice widens the same build, so
    # its earlier result does not carry over to this artifact.
    commands = [[sys.executable, 'tests/verify_emulator_phase2.py'],
                ['node', '--test', 'tests/test_emulator_device.mjs'],
                ['node', '--test', 'tests/test_emulator_bridge.mjs'],
                [sys.executable, 'tests/run_emulator_capture_tests.py'],
                [sys.executable, 'tests/test_emulator_phase3.py'],
                [sys.executable, 'tests/test_emulator_lifecycle.py']]
    for command in commands:
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        report['checks'].append({'command': command, 'exit_code': result.returncode,
                                 'output': result.stdout + result.stderr})
        print(('PASS ' if result.returncode == 0 else 'FAIL ') + command[-1], flush=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    evidence = json.loads((destination.parent / 'phase3-browser-report.json').read_text(encoding='utf-8'))
    if evidence['status'] != 'passed' or evidence['artifact_sha256'] != fingerprint['artifact_sha256']:
        raise RuntimeError('Failed or stale report: phase3-browser-report.json')
    for name in ('phase2-verification.json', 'phase3-capture-report.json'):
        if json.loads((destination.parent / name).read_text(encoding='utf-8'))['status'] != 'passed':
            raise RuntimeError('Failed report: ' + name)
    if artifact_fingerprint() != fingerprint:
        raise RuntimeError('Static build changed during verification; rerun on a stable build')
    report['status'] = 'passed'
except Exception as error:
    report.update(status='failed', error=str(error))
    raise
finally:
    destination.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
