"""Phase 4.1: Bluetooth discovery and locate driven from the Canvas must agree
with the shared scenario model."""
import json
import re
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint, browser_session, objects, click_text, click_object

REPORT_PATH = ROOT / 'docs/ui-emulator/phase4-bluetooth-report.json'
report = {'status': 'running',
          'recorded_at': datetime.now(timezone.utc).isoformat(),
          'scope': 'Bluetooth menu, discovery list and locator driven from the Canvas',
          'limitations': ['Chromium only; Firefox, WebKit and physical hardware are Phase 6 work',
                          'Synthetic BLE fixtures from the versioned seed; no real radio',
                          'AirTag scan and the nRF24 jammer are explicit unsupported boundaries'],
          'checks': [], 'failures': []}


def rssi_value(text):
    match = re.search(r'(-?\d+)\s*dBm', text)
    return int(match.group(1)) if match else None


try:
    with browser_session() as (browser, url):
        report['browser'] = {'engine': 'chromium', 'version': browser.version}
        page = browser.new_page(viewport={'width': 1440, 'height': 1000})
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.goto(url)
        page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        page.evaluate('''() => { globalThis.unsupportedEvents=[];
            addEventListener('emulator-unavailable', e=>unsupportedEvents.push(e.detail)); }''')

        # 1. The home Bluetooth tile opens the retained BT menu.
        click_text(page, 'Bluetooth')
        assert any(o['text'] == 'BT Scan\n& Locate' for o in objects(page)), [o['text'] for o in objects(page)]
        report['checks'].append('home Bluetooth tile opens the BT menu')

        # 2. Opening BT Scan runs the synchronous scan and lists the model devices.
        devices = page.evaluate("emulator.device.device.bluetooth('grove')")
        assert len(devices) >= 1, devices
        click_text(page, 'BT Scan\n& Locate')
        rendered = '\n'.join(o['text'] for o in objects(page))
        assert f'Tap device to locate ({len(devices)} found)' in rendered, rendered
        for d in devices:
            needle = d['name'] or d['mac']
            assert needle in rendered, (needle, rendered)
        # Every device row is a distinct enrolled binding, keyed on the MAC.
        rows = [o for o in objects(page) if o['binding'].endswith('/locate')]
        assert len(rows) == len(devices), [o['binding'] for o in rows]
        assert all(any('/' + d['mac'] + '/locate' in o['binding'] for o in rows) for d in devices), \
            [o['binding'] for o in rows]
        report['checks'].append('discovery list matches the model devices with unique per-MAC bindings')
        report['devices'] = [{'mac': d['mac'], 'name': d['name'], 'rssi': d['rssi']} for d in devices]

        # 3. Tapping a device opens the locator with its identity and a live RSSI.
        target = devices[0]
        row = next(o for o in rows if '/' + target['mac'] + '/locate' in o['binding'])
        click_object(page, row)
        located = '\n'.join(o['text'] for o in objects(page))
        assert (target['name'] or target['mac']) in located, located
        assert target['mac'] in located, located
        shown = rssi_value(located)
        assert shown is not None and -95 <= shown <= -30, located
        report['checks'].append('locator opens on the tapped device and shows its RSSI')

        # 4. The locator RSSI tracks the model and drifts within the modelled band.
        page.evaluate('for (let i=0;i<40;i++) emulator.module._emu_tick(100)')
        located = '\n'.join(o['text'] for o in objects(page))
        drifted = rssi_value(located)
        model_rssi = page.evaluate("(mac) => emulator.device.device.bluetoothRssi('grove', mac)", target['mac'])
        assert drifted is not None and abs(drifted - target['rssi']) <= 5, (drifted, target['rssi'])
        assert -95 <= model_rssi <= -30, model_rssi
        report['checks'].append('locator RSSI is model-driven and stays within the drift band')

        # 5. Back returns to the discovery list without a lingering operation.
        back = [o for o in objects(page) if 'show_bt_locator_page.back_btn' in o['binding']]
        assert len(back) == 1, [o['binding'] for o in objects(page)]
        click_object(page, back[0])
        assert any('Tap device to locate' in o['text'] for o in objects(page)), \
            [o['text'] for o in objects(page)]
        report['checks'].append('back from the locator returns to the discovery list')

        assert not errors, errors
        assert page.evaluate('unsupportedEvents') == [], page.evaluate('unsupportedEvents')
        assert page.evaluate('emulatorModelErrors') == [], page.evaluate('emulatorModelErrors')
    report['status'] = 'passed'
except Exception as error:  # noqa: BLE001 - the report must record why the run stopped
    report.update(status='failed', error=repr(error))
    report['failures'].append(repr(error))
    raise
finally:
    report.update(artifact_fingerprint())
    REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
