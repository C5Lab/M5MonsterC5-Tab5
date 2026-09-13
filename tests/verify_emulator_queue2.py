"""Queue 2 guided stories: offline native evidence, all families and variants."""
import sys
from emulator_browser import ROOT,artifact_fingerprint
from emulator_test_runner import run_logged

fingerprint=artifact_fingerprint()
commands=[['node','--test',*[f'tests/test_emulator_queue2_{name}.mjs' for name in ['common','system','attacks','nettools','wardrive']]]]
commands += [[sys.executable,f'tests/test_emulator_queue2_{name}.py'] for name in ['system','attacks','nettools','wardrive']]
commands += [[sys.executable,'tests/test_emulator_runtime_inventory.py']]
for command in commands:
    result=run_logged(command,cwd=ROOT)
    if result.returncode:raise SystemExit(result.returncode)
if artifact_fingerprint()!=fingerprint:raise RuntimeError('Build changed during verification')
