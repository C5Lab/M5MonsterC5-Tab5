"""Phase 4.2 gate: full prior regression and Wardrive on a stable static build."""
from emulator_test_runner import run_logged
import json,subprocess,sys
from datetime import datetime,timezone
from emulator_browser import ROOT,artifact_fingerprint
path=ROOT/'docs/ui-emulator/phase4-wardrive-verification.json'
report={'status':'running','recorded_at':datetime.now(timezone.utc).isoformat(),'checks':[],
        'limitations':['Synthetic GPS, virtual sessions and simulated upload only; no external service or radio',
                       'Native LVGL Setup/GPS/Upload/Home/Blacklist screens; hardware, settings transport and upload use simulator adapters; Cleanup/Fix remain unsupported',
                       'Chromium only; physical hardware and other browser engines not validated']}
try:
    fingerprint=artifact_fingerprint();report.update(fingerprint)
    for command in [[sys.executable,'tests/verify_emulator_phase4_bluetooth.py'],
                    ['node','--test','tests/test_emulator_wardrive.mjs'],
                    [sys.executable,'tests/test_emulator_phase4_wardrive.py']]:
        result=run_logged(command,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
        report['checks'].append({'command':command,'exit_code':result.returncode,'output':result.stdout+result.stderr})
        print(('PASS ' if result.returncode==0 else 'FAIL ')+command[-1],flush=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    if fingerprint!=artifact_fingerprint():raise RuntimeError('Build changed during verification')
    report['status']='passed'
except Exception as error:
    report.update(status='failed',error=str(error));raise
finally:path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
