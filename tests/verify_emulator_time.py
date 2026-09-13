"""Native Time acceptance; rebuild first. Phase 5 settings includes it."""
import json
import sys
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged

path=ROOT/'docs/ui-emulator/time-verification.json'
report={'status':'running','recorded_at':datetime.now(timezone.utc).isoformat(),'checks':[],
        'limitations':['Virtual civil clock only; no physical RTC or host clock changes',
                       'Time browser cases serve unchanged local Wasm bytes via Playwright; HTTP delivery is tested separately']}
try:
    fingerprint=artifact_fingerprint();report.update(fingerprint)
    for command in [['node','--test','tests/test_emulator_persistence.mjs'],
                    [sys.executable,'tests/test_emulator_time.py'],
                    [sys.executable,'tests/test_emulator_phase4_audit.py']]:
        result=run_logged(command,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
        report['checks'].append({'command':command,'exit_code':result.returncode,'output':result.stdout+result.stderr})
        if result.returncode:raise RuntimeError('Check failed: '+command[-1])
    if fingerprint!=artifact_fingerprint():raise RuntimeError('Build changed during verification')
    report['status']='passed'
except Exception as error:
    report.update(status='failed',error=str(error));raise
finally:
    path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
