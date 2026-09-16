"""Exercise real Canvas input against the compiled production LVGL slice."""
import json
from datetime import datetime, timezone
from playwright.sync_api import expect
from emulator_browser import ROOT, artifact_fingerprint, browser_session, objects, click_text, click_object

report = {'status':'running', 'cases':[],
          'scope':'Phase 2 navigation/scan/Scan Setup subset; not the complete acceptance gate',
          'recorded_at':datetime.now(timezone.utc).isoformat(),
          'checks':['Canvas navigation', 'cancellation and late-event suppression',
                    'rescan clears selection', 'stable network binding IDs',
                    'Grove/MBus result isolation', 'landscape wheel scrolling',
                    'Scan Setup save and reopen', 'page errors and unsupported boundaries'],
          'limitations':['No physical mobile device or touch validation',
                         'No text input or complete Settings coverage',
                         'No cross-browser or performance-budget acceptance']}
report_path = ROOT / 'docs/ui-emulator/phase2-browser-report.json'

def dump(page):
    return [(o['text'], o['binding'], o['x'], o['y']) for o in objects(page) if o['text'] or o['binding']]

def binding(page, fragment):
    matches = [o for o in objects(page) if fragment in o['binding']]
    assert len(matches) == 1, (fragment, matches)
    return matches[0]

try:
    with browser_session() as (browser, url):
        report['browser']={'engine':'chromium','version':browser.version}
        for rotation, viewport in [(r, v) for v in
                ({'width':1440,'height':1000}, {'width':390,'height':844}) for r in range(4)]:
            context = browser.new_context(viewport=viewport)
            page = context.new_page()
            errors=[]
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.goto(f'{url}/?rotation={rotation}')
            page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
            page.evaluate('globalThis.unsupportedEvents=[]; addEventListener("emulator-unavailable", e=>unsupportedEvents.push(e.detail))')
            assert page.evaluate('[emulator.module._emu_width(),emulator.module._emu_height()]') == ([1280,720] if rotation%2 else [720,1280])
            page.locator('#timing').select_option('1')
            click_text(page, 'WiFi Scan\n& Attack')
            page.wait_for_function('emulator.module._emu_scan_state(0) === 1')
            expect(page.locator('#timing')).to_be_disabled()
            page.locator('#cancel').click()
            page.wait_for_function('emulator.module._emu_scan_state(0) === 3')
            assert page.evaluate('emulator.module._emu_network_count(0)') == 0
            # Later scheduled time cannot resurrect a cancelled scan.
            page.evaluate('for(let i=0;i<60;i++)emulator.module._emu_tick(100)')
            assert page.evaluate('emulator.module._emu_network_count(0)') == 0
            page.locator('#timing').select_option('0')
            click_object(page, binding(page, 'ui.show_scan_page.ctx--scan_btn.'))
            page.wait_for_function('emulator.module._emu_scan_state(0) === 2')
            assert page.evaluate('emulator.module._emu_network_count(0)') == 12
            row_bindings=[o for o in objects(page) if '/select' in o['binding']]
            assert len(row_bindings)==12, dump(page)
            click_object(page,row_bindings[0])
            assert page.evaluate('emulator.module._emu_selected_count(0)') == 1
            assert len({o['binding'] for o in objects(page) if o['binding']}) == len([o for o in objects(page) if o['binding']])
            # Wheel input must move the actual network list at this CSS scale.
            if rotation % 2:  # Twelve rows overflow the landscape list.
                canvas = page.locator('#display')
                box = canvas.bounding_box()
                page.mouse.move(box['x']+box['width']/2, box['y']+box['height']*.65)
                before = {str(o['id']):o['scrollY'] for o in objects(page)}
                page.mouse.wheel(0, 250)
                page.wait_for_function('''before => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(o => o.scrollY > (before[o.id] || 0));
                }''', arg=before)
            # Fresh scan clears selection and retains stable entity IDs.
            click_object(page, binding(page, 'ui.show_scan_page.ctx--scan_btn.'))
            page.wait_for_function('emulator.module._emu_scan_state(0) === 2')
            assert page.evaluate('emulator.module._emu_selected_count(0)') == 0
            assert {o['binding'] for o in objects(page) if '/select' in o['binding']} == {o['binding'] for o in row_bindings}
            # MBus discovery must not reuse or erase Grove results.
            click_text(page, 'MBUS')
            click_text(page, 'WiFi Scan\n& Attack')
            page.wait_for_function('emulator.module._emu_scan_state(2) === 2')
            assert page.evaluate('[emulator.module._emu_network_count(0),emulator.module._emu_network_count(2)]') == [12,12]
            assert not ({o['binding'] for o in objects(page) if '/select' in o['binding']} & {o['binding'] for o in row_bindings})
            click_text(page,'INTERNAL')
            click_text(page,'Settings')
            assert any(o['text']=='Scan\nSetup' for o in objects(page)), dump(page)
            # Save a real production Settings change and reopen the dialog.
            click_text(page, 'Scan\nSetup')
            increment = next(o for o in objects(page) if
                'inc_btn.clicked' in o['binding'] and '/Grove/static/Min time:' in o['binding'])
            click_object(page, increment)
            click_text(page, 'Save')
            click_text(page, 'Scan\nSetup')
            assert any(o['text']=='0150' for o in objects(page)), dump(page)
            click_text(page, 'Cancel')
            print('Navigation, scan and Settings passed:',rotation,viewport['width'],flush=True)
            assert not errors, errors
            assert not page.evaluate('unsupportedEvents'), page.evaluate('unsupportedEvents')
            page.screenshot(path=str(ROOT/f'docs/ui-emulator/prototype-settings-{rotation}-{viewport["width"]}.png'), full_page=True)
            report['cases'].append({'rotation':rotation,'viewport':viewport,'status':'passed','metrics':page.evaluate('emulator.measurements')})
            context.close()
    report['status']='passed'
except Exception as error:
    report.update(status='failed',error=repr(error))
    raise
finally:
    report.update(artifact_fingerprint())
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
