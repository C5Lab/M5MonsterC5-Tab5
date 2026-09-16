"""Independent Observer discovery and downstream native lifecycle acceptance."""
import sys
from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged

fingerprint=artifact_fingerprint()
commands=[['node','--test','tests/test_emulator_bridge.mjs','tests/test_emulator_attacks.mjs','tests/test_emulator_device.mjs']]
commands += [[sys.executable,'tests/'+name+'.py'] for name in [
    'test_emulator_observer_exit','test_emulator_lifecycle','test_emulator_observer_activity',
    'test_emulator_phase3','test_emulator_phase4_audit']]
for command in commands:
    result=run_logged(command,cwd=ROOT)
    if result.returncode:raise SystemExit(result.returncode)
if artifact_fingerprint()!=fingerprint:raise RuntimeError('Build changed during verification')
