"""Acceptance gate for the 2026-10-26 Tab5 emulator feature-parity work.

Build ``tools/ui_emulator/dist`` first. This runs the new offline feature
journeys and the cumulative Phase 5 gate against one unchanged browser bundle.
"""

import json
import sys
import time
from datetime import datetime, timezone

from emulator_browser import ROOT, artifact_fingerprint
from emulator_test_runner import run_logged


NODE_MODELS = [
    'tests/test_emulator_analyzer.mjs',
    'tests/test_emulator_analyzer_guide.mjs',
    'tests/test_emulator_bluetooth_tags.mjs',
    'tests/test_emulator_compromised_model.mjs',
    'tests/test_emulator_nfc_model.mjs',
    'tests/test_emulator_portal_flows.mjs',
    'tests/test_emulator_rogue_gitm.mjs',
    'tests/test_emulator_settings_sys.mjs',
    'tests/test_emulator_subghz_model.mjs',
    'tests/test_emulator_wpa_auditor.mjs',
]

BROWSER_SUITES = [
    'tests/test_emulator_analyzer.py',
    'tests/test_emulator_bluetooth_parity.py',
    'tests/test_emulator_compromised_passwords.py',
    'tests/test_emulator_nfc.py',
    'tests/test_emulator_observer_export.py',
    'tests/test_emulator_observer_exit.py',
    'tests/test_emulator_ota_variant.py',
    'tests/test_emulator_portal_flows.py',
    'tests/test_emulator_radio_availability.py',
    'tests/test_emulator_radio_rotations.py',
    'tests/test_emulator_rogue_gitm.py',
    'tests/test_emulator_screenshot.py',
    'tests/test_emulator_settings_parity.py',
    'tests/test_emulator_subghz.py',
    'tests/test_emulator_wardrive_autoup.py',
    'tests/test_emulator_wpa_auditor.py',
]

REPORT_PATH = ROOT / 'docs/ui-emulator/feature-parity-verification.json'
report = {
    'status': 'running',
    'recorded_at': datetime.now(timezone.utc).isoformat(),
    'checks': [],
    'limitations': [
        'Chromium and offline browser models only; no JanOS board, radio, SD card or firmware validation',
        'No Firefox, WebKit, physical phone or published Pages validation',
    ],
}

try:
    fingerprint = artifact_fingerprint()
    report.update(fingerprint)
    commands = [
        ('new model suites', ['node', '--test', *NODE_MODELS]),
        *((path, [sys.executable, path]) for path in BROWSER_SUITES),
        ('cumulative Phase 5', [sys.executable, 'tests/verify_emulator_phase5_settings.py']),
    ]
    for name, command in commands:
        started = time.perf_counter()
        result = run_logged(command, cwd=ROOT, capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
        elapsed = round(time.perf_counter() - started, 2)
        output = result.stdout + result.stderr
        report['checks'].append({
            'name': name,
            'command': command,
            'exit_code': result.returncode,
            'duration_s': elapsed,
            'output_tail': output[-6000:],
        })
        print(('PASS ' if result.returncode == 0 else 'FAIL ') + name, flush=True)
        if result.returncode:
            raise RuntimeError(f'Check failed: {name}')
    if fingerprint != artifact_fingerprint():
        raise RuntimeError('Build changed during verification')
    report['status'] = 'passed'
except Exception as error:
    report.update(status='failed', error=str(error))
    raise
finally:
    REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
