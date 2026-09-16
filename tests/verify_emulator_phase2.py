"""Record the Phase 2 gate only when every check passes on one static build."""
from emulator_test_runner import run_logged
import json
import subprocess
import sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint

destination = ROOT / 'docs/ui-emulator/phase2-verification.json'
report = {'status':'running', 'recorded_at':datetime.now(timezone.utc).isoformat(),
          'scope':'Home/Settings/Scan browser feasibility; simulated touch and throttled mobile profile',
          'checks':[], 'limitations':['Physical mobile, Firefox/WebKit and full application coverage remain Phase 6/4 work']}
try:
    fingerprint = artifact_fingerprint()
    if not fingerprint['artifact_sha256'].get('emulator.wasm'):
        raise RuntimeError('Build the emulator before running this gate')
    report.update(fingerprint)
    commands = [[sys.executable, 'tests/'+name] for name in (
        'test_emulator_slice.py', 'test_emulator_contracts.py', 'test_emulator_browser.py',
        'test_emulator_phase2.py', 'test_emulator_settings.py', 'test_emulator_performance.py')]
    commands.insert(2, ['node','--test','tests/test_emulator_input.mjs'])
    for command in commands:
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
        report['checks'].append({'command':command,'exit_code':result.returncode,
                                 'output':result.stdout+result.stderr})
        print(('PASS ' if result.returncode==0 else 'FAIL ')+command[-1],flush=True)
        if result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
    for name in ('phase2-browser-report.json','phase2-acceptance-report.json','phase2-performance-report.json'):
        evidence=json.loads((destination.parent/name).read_text(encoding='utf-8'))
        if evidence['status']!='passed' or evidence['artifact_sha256']!=fingerprint['artifact_sha256']:
            raise RuntimeError('Failed or stale report: '+name)
    if artifact_fingerprint()!=fingerprint:
        raise RuntimeError('Static build changed during verification; rerun on a stable build')
    report['status']='passed'
except Exception as error:
    report.update(status='failed',error=str(error))
    raise
finally:
    destination.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
