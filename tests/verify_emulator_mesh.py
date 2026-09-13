"""Focused Mesh monitoring acceptance for the current built emulator."""
import sys
from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged

fingerprint=artifact_fingerprint()
for command in [
    ['node','--test','tests/test_emulator_device.mjs','tests/test_emulator_nettools.mjs'],
    [sys.executable,'tests/test_emulator_mesh_monitor.py'],
    [sys.executable,'tests/test_emulator_phase4_audit.py'],
]:
    result=run_logged(command,cwd=ROOT)
    if result.returncode:raise SystemExit(result.returncode)
if artifact_fingerprint()!=fingerprint:raise RuntimeError('Build changed during verification')
