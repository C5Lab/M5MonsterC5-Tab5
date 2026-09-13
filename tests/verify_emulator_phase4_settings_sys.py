"""Phase 4.6 gate, including previous acceptance. Run after rebuilding."""
from emulator_test_runner import run_logged
import json,subprocess,sys
from datetime import datetime,timezone
from emulator_browser import ROOT,artifact_fingerprint

path=ROOT/'docs/ui-emulator/phase4-settings-sys-verification.json'
report={'status':'running','recorded_at':datetime.now(timezone.utc).isoformat(),'checks':[],
        'limitations':['Offline system jobs only; no firmware download, hardware reboot or network service',
                       'Chromium acceptance only; real hardware and cross-browser checks remain pending',
                       'Ad Hoc Portal remains a separate unsupported application; Nmap portrait accessibility remains open']}
try:
    fingerprint=artifact_fingerprint();report.update(fingerprint)
    for command in [[sys.executable,'tests/verify_emulator_phase4_attacks.py'],
                    ['node','--test','tests/test_emulator_settings_sys.mjs'],
                    [sys.executable,'tests/test_emulator_phase4_settings_sys.py']]:
        print('RUN '+' '.join(command[1:]),flush=True)
        result=run_logged(command,cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
        report['checks'].append({'command':command,'exit_code':result.returncode,'output':result.stdout+result.stderr})
        print(('PASS ' if result.returncode==0 else 'FAIL ')+command[-1],flush=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    if fingerprint!=artifact_fingerprint():raise RuntimeError('Build changed during verification')
    report['status']='passed'
except Exception as error:
    report.update(status='failed',error=str(error));raise
finally:path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
