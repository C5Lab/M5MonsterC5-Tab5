"""Guided stories and native dialog regression, including Queue 2."""
import sys
from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged

fingerprint=artifact_fingerprint()
commands=[['node','--test','tests/test_emulator_stories.mjs','tests/test_emulator_startup_story.mjs','tests/test_emulator_wifi_stories.mjs','tests/test_emulator_recon_stories.mjs','tests/test_emulator_dialogs.mjs']]
commands += [[sys.executable,'tests/'+name+'.py'] for name in [
    'test_emulator_stories','test_emulator_story_lifecycle','test_emulator_startup_story','test_emulator_wifi_stories','test_emulator_recon_stories','test_emulator_dialogs','test_emulator_phase4_audit']]
commands += [[sys.executable,'tests/'+name+'.py'] for name in [
    'verify_emulator_portal_story','verify_emulator_pcap_story',
    'verify_emulator_settings_stories','verify_emulator_queue2']]
for command in commands:
    result=run_logged(command,cwd=ROOT)
    if result.returncode:raise SystemExit(result.returncode)
if artifact_fingerprint()!=fingerprint:raise RuntimeError('Build changed during verification')
