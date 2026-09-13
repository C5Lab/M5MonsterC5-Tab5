"""Phase 3: real Canvas actions must agree with the shared scenario model."""
import hashlib
import json
from datetime import datetime, timezone
from emulator_browser import DIST, ROOT, artifact_fingerprint, browser_session, objects, click_text, click_object

REPORT_PATH = ROOT / 'docs/ui-emulator/phase3-browser-report.json'
report = {'status': 'running',
          'recorded_at': datetime.now(timezone.utc).isoformat(),
          'scope': 'Scan, Network Observer, clients, synthetic capture and production PCAP analysis driven from the Canvas',
          'limitations': ['Chromium only; Firefox, WebKit and physical hardware are Phase 6 work',
                          'Synthetic Ethernet/ARP fixtures only: no radio capture, handshake or authentication',
                          'Observer clients come from the versioned seed, not from live sniffing'],
          'checks': [], 'failures': []}


def bound(page, fragment):
    found = [o for o in objects(page) if fragment in o['binding']]
    assert len(found) == 1, (fragment, [o['binding'] for o in found])
    return found[0]


def watch(page):
    page.evaluate('''() => { globalThis.unsupportedEvents=[];
        addEventListener('emulator-unavailable', e=>unsupportedEvents.push(e.detail)); }''')


def clean(page):
    assert page.evaluate('unsupportedEvents') == [], page.evaluate('unsupportedEvents')
    assert page.evaluate('emulatorModelErrors') == [], page.evaluate('emulatorModelErrors')


try:
    with browser_session() as (browser, url):
        report['browser'] = {'engine': 'chromium', 'version': browser.version}
        page = browser.new_page(viewport={'width': 1440, 'height': 1000})
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
        page.goto(url)
        page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        watch(page)

        # 1. The retained scan list is filled from the shared scenario, not a local fixture.
        click_text(page, 'WiFi Scan\n& Attack')
        page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        labels = [o['text'] for o in objects(page)]
        assert any('NEON-BAZAAR' in text for text in labels), labels
        assert any('02:20:77:00:00:01' in o['binding'] for o in objects(page))
        assert page.evaluate('emulator.device.snapshot(0).networks[0].id') == 'net-1'
        report['checks'].append('retained scan UI shows shared scenario identities')

        # 2. Scan selection must not restrict the independent Observer inventory.
        row = bound(page, '02:20:77:00:00:01/select')
        click_object(page, row)
        assert page.evaluate('emulator.module._emu_selected_count(0)') == 1
        page.evaluate('emulator.module._emu_show(0, 0)')
        scan_selection = page.evaluate('emulator.device.snapshot(0).selected')
        click_text(page, 'Network\nObserver')
        click_text(page, 'Start')
        observed = [o for o in objects(page) if o['binding'].endswith('/observe')]
        assert len(observed) == 12, [o['binding'] for o in observed]
        assert '02:20:77:00:00:01' in observed[0]['binding'], observed[0]['binding']
        assert page.evaluate('emulator.device.snapshot(0).selected') == scan_selection
        clients = page.evaluate("emulator.device.device.observer('grove').networks.find(n=>n.id==='net-1').clients.map(c=>c.mac)")
        assert clients, 'The seed must give the selected network clients'
        rendered = {o['text'] for o in objects(page)}
        assert all(mac in rendered for mac in clients), (clients, sorted(rendered))
        report['checks'].append('Observer independently includes all networks and their scenario clients')
        report['observer'] = {'network': '02:20:77:00:00:01', 'clients': clients}

        # 3. The production popup opens on that row and lists the same clients.
        click_object(page, observed[0])
        assert any(o['text'] == f'Clients ({len(clients)}):' for o in objects(page)), sorted(rendered)

        # 4. The labelled simulator affordance generates a file through the model.
        click_object(page, bound(page, 'emu.phase3.observer.synthetic_capture'))
        click_object(page, bound(page, 'emu.phase3.capture.generate'))
        page.wait_for_function("() => emulator.device.device.files('grove').length === 1")
        files = page.evaluate("emulator.device.device.files('grove')")
        assert files[0]['networkId'] == 'net-1', files
        assert files[0]['packetCount'] == len(clients), files
        assert sorted(files[0]['clientIds']) == sorted(
            page.evaluate("emulator.device.device.clients('grove').map(c=>c.id)")), files
        capture = page.evaluate(f"Array.from(emulator.device.device.readFile('grove',{json.dumps(files[0]['path'])}))")
        assert bytes(capture[:4]) == b'\xd4\xc3\xb2\xa1', capture[:4]
        report['checks'].append('synthetic capture agrees with the selected network and its clients')
        report['capture'] = {'path': files[0]['path'], 'size_bytes': files[0]['sizeBytes'],
                             'packet_count': files[0]['packetCount'],
                             'sha256': hashlib.sha256(bytes(capture)).hexdigest()}

        # 5. Follow the real analysis controls and verify output from the PCAP bytes.
        click_object(page, bound(page, 'emu.phase3.capture.analyze'))
        assert any('Found 1 local capture(s)' in o['text'] for o in objects(page))
        click_object(page, bound(page, 'ui.pcap_viewer_render_file_list.open_btn.clicked'))
        page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o=>o.text.includes('3 packets'));
        }''', timeout=10000)
        rendered = '\n'.join(o['text'] for o in objects(page))
        assert 'Ethernet' in rendered and '198 B' in rendered and '0.002 s' in rendered, rendered
        assert all(mac in rendered for mac in clients), rendered
        report['checks'].append('production analysis displays capture size, duration, packet count and endpoints')
        assert page.evaluate("emulator.device.device.files('grove').length") == 1
        assert page.evaluate("emulator.device.snapshot(0).active") is None

        # 6. MBus Observer discovers independently without populating WiFi Scan.
        page.evaluate('emulator.module._emu_show(0, 2)')
        click_text(page, 'Network\nObserver')
        click_text(page, 'Start')
        assert page.evaluate('emulator.module._emu_network_count(2)') == 0
        assert len([o for o in objects(page) if o['binding'].endswith('/observe')]) == 12
        assert page.evaluate('emulator.device.snapshot(2).selected') is None
        report['checks'].append('MBus Observer discovers without a scan and leaves scan selection untouched')

        assert not errors, errors
        clean(page)
    report['status'] = 'passed'
except Exception as error:  # noqa: BLE001 - the report must record why the run stopped
    report.update(status='failed', error=repr(error))
    report['failures'].append(repr(error))
    raise
finally:
    report.update(artifact_fingerprint())
    REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
