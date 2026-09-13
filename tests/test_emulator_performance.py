"""Phase 2 performance budgets on desktop and throttled mobile-class conditions.

Measures the static bundle and the live render loop against
``tools/ui_emulator/performance-budgets.json`` and records
``docs/ui-emulator/phase2-performance-report.json``. Throttled Chromium
approximates a mid-range phone; it is not a physical-device measurement.
"""
import json
import time
from datetime import datetime, timezone
from emulator_browser import ROOT, DIST, artifact_fingerprint, browser_session, click_text

BUDGETS = json.loads((ROOT / 'tools/ui_emulator/performance-budgets.json').read_text(encoding='utf-8'))
REPORT_PATH = ROOT / 'docs/ui-emulator/phase2-performance-report.json'
report = {'status': 'running',
          'recorded_at': datetime.now(timezone.utc).isoformat(),
          'budgets_version': BUDGETS['version'],
          'scope': BUDGETS['scope'],
          'limitations': ['Chromium only; Firefox and WebKit are Phase 6 work',
                          'Throttled emulation, not a physical phone or a real mobile network',
                          'Render callback timing excludes browser paint and compositing'],
          'profiles': {},
          'failures': []}


def check(failures, profile, name, value, limit):
    passed = value <= limit
    if not passed:
        failures.append(f'{profile}: {name} {value} exceeds budget {limit}')
    return {'value': value, 'budget': limit, 'passed': passed}


def download_measurements(failures):
    sizes = {p.name: p.stat().st_size for p in DIST.glob('*') if p.is_file()}
    budget = BUDGETS['download']
    return {'file_bytes': sizes,
            'total_bytes': check(failures, 'download', 'total_bytes',
                                 sum(sizes.values()), budget['total_bytes_max']),
            'largest_file_bytes': check(failures, 'download', 'largest_file_bytes',
                                        max(sizes.values()), budget['largest_file_bytes_max'])}


def measure_profile(browser, url, name, profile, failures):
    context = browser.new_context(viewport=profile['viewport'])
    page = context.new_page()
    errors = []
    page.on('pageerror', lambda event: errors.append(str(event)))
    try:
        cdp = context.new_cdp_session(page)
        cdp.send('Network.enable')
        network = profile['network']
        cdp.send('Network.emulateNetworkConditions', {
            'offline': False,
            'latency': network['latency_ms'] if network else 0,
            'downloadThroughput': network['download_bytes_per_second'] if network else -1,
            'uploadThroughput': network['upload_bytes_per_second'] if network else -1})
        cdp.send('Emulation.setCPUThrottlingRate', {'rate': profile['cpu_throttling_rate']})

        started = time.perf_counter()
        page.goto(url)
        page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=60000)
        load_ms = (time.perf_counter() - started) * 1000

        # Time frames across the busiest supported workload: a realistic-speed scan.
        page.locator('#timing').select_option('1')
        before = page.evaluate('() => ({...emulator.measurements})')
        click_text(page, 'WiFi Scan\n& Attack')
        page.wait_for_function('emulator.module._emu_scan_state(0) === 2', timeout=60000)
        after = page.evaluate('() => ({...emulator.measurements})')
        frames = after['frames'] - before['frames']
        assert frames > 30, f'{name}: only {frames} frames during the simulated scan'
        average = (after['totalFrameMs'] - before['totalFrameMs']) / frames

        assert not errors, errors
        return {'description': profile['description'],
                'viewport': profile['viewport'],
                'cpu_throttling_rate': profile['cpu_throttling_rate'],
                'network': network,
                'scan_frames': frames,
                'first_frame_ms': check(failures, name, 'first_frame_ms',
                                        after['startupMs'], profile['first_frame_ms_max']),
                'page_load_to_first_frame_ms': check(failures, name, 'page_load_to_first_frame_ms',
                                                     load_ms, profile['page_load_to_first_frame_ms_max']),
                'average_frame_ms': check(failures, name, 'average_frame_ms',
                                          average, profile['average_frame_ms_max']),
                'peak_frame_ms': check(failures, name, 'peak_frame_ms',
                                       after['maxFrameMs'], profile['peak_frame_ms_max']),
                'wasm_memory_bytes': check(failures, name, 'wasm_memory_bytes',
                                           after['memoryBytes'], profile['wasm_memory_bytes_max'])}
    finally:
        context.close()


try:
    failures = report['failures']
    report['download'] = download_measurements(failures)
    with browser_session() as (browser, url):
        report['browser'] = {'engine': 'chromium', 'version': browser.version}
        for name, profile in BUDGETS['profiles'].items():
            report['profiles'][name] = measure_profile(browser, url, name, profile, failures)
            print(f'{name} profile measured', flush=True)
    report['status'] = 'failed' if failures else 'passed'
except Exception as error:  # noqa: BLE001 - the report must record why the run stopped
    report.update(status='failed', error=repr(error))
    raise
finally:
    report.update(artifact_fingerprint())
    REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

if report['failures']:
    raise SystemExit('Performance budgets exceeded:\n' + '\n'.join(report['failures']))
