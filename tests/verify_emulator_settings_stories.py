"""S18-S20 native settings stories and one-shot rotation restart acceptance."""
import sys
from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged

fingerprint=artifact_fingerprint()
commands=[['node','--test','tests/test_emulator_story_restart.mjs',
           *[f'tests/test_emulator_settings_s{n}.mjs' for n in (18,19,20)]]]
commands += [[sys.executable,f'tests/test_emulator_settings_s{n}.py'] for n in (18,19,20)]
for command in commands:
    result=run_logged(command,cwd=ROOT)
    if result.returncode:raise SystemExit(result.returncode)
if artifact_fingerprint()!=fingerprint:raise RuntimeError('Build changed during verification')
