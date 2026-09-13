"""Phase 2 lifecycle and browser-input acceptance using actual Canvas events."""
import json
import unittest
from datetime import datetime, timezone
from emulator_browser import ROOT, artifact_fingerprint, browser_session, objects, click_text, click_object

IDENTITIES = set(json.loads(
    (ROOT / 'tools/ui_emulator/control-identities.json').read_text(encoding='utf-8'))['identities'].values())
REPORT_PATH = ROOT / 'docs/ui-emulator/phase2-acceptance-report.json'
report = {'status': 'running',
          'recorded_at': datetime.now(timezone.utc).isoformat(),
          'scope': 'Phase 2 touch, drag, text entry, Settings, restart and runtime binding acceptance',
          'limitations': ['Chromium only; Firefox, WebKit and physical touch hardware are Phase 6 work',
                          'Covers the retained Home, Settings and Wi-Fi Scan slice only',
                          'Performance budgets are recorded separately by tests/test_emulator_performance.py'],
          'passed': [],
          'failed': []}


def bound(page, fragment):
    found = [o for o in objects(page) if fragment in o['binding']]
    assert len(found) == 1, (fragment, found)
    return found[0]


def position(page, obj, fx=.5, fy=.5):
    canvas = page.locator('#display')
    canvas.scroll_into_view_if_needed()
    box = canvas.bounding_box()
    size = canvas.evaluate('(c) => [c.width,c.height]')
    return (box['x']+(obj['x']+obj['width']*fx)*box['width']/size[0],
            box['y']+(obj['y']+obj['height']*fy)*box['height']/size[1])


def dropdown(page, fragment, index):
    click_object(page, bound(page, fragment))
    options = [o for o in objects(page) if '\n' in o['text'] and (
        'Portrait (0)' in o['text'] or '10 seconds' in o['text'] or 'Nokia' in o['text'])]
    assert len(options) == 1, options
    label = options[0]
    page.mouse.click(*position(page, label, .5, (index+.5)/len(label['text'].splitlines())))


def stored(page, key):
    return page.evaluate('''key => {
        const value = JSON.parse(localStorage.getItem('tab5-emulator:settings'))?.values[key];
        return value === undefined ? null : String(value);
    }''', key)


class Phase2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()
        report.update(artifact_fingerprint())
        report['browser'] = {'engine':'chromium','version':cls.browser.version}

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)
        report['failed'] = sorted(set(unittest.TestLoader().getTestCaseNames(cls)) - set(report['passed']))
        report['status'] = 'failed' if report['failed'] else 'passed'
        REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def setUp(self):
        self.completed = False
        self.context = self.browser.new_context(viewport={'width':1440,'height':1000}, has_touch=True)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda e:self.errors.append(str(e)))
        self.page.goto(self.url)
        self.ready()

    def tearDown(self):
        self.context.close()
        if self.completed and not self.errors:
            report['passed'].append(self._testMethodName)
        self.assertEqual(self.errors, [])

    def ready(self):
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')

    def settings(self):
        click_text(self.page, 'INTERNAL')
        click_text(self.page, 'Settings')

    def test_cancelled_pointer_does_not_activate_tile(self):
        p = self.page
        tile = next(
            o for o in objects(p) if o['text']=='WiFi Scan\n& Attack')
        p.mouse.move(*position(p, tile))
        p.mouse.down()
        p.locator('#display').dispatch_event('pointercancel', {'pointerId':1})
        p.mouse.up()
        self.assertEqual(p.evaluate('emulator.module._emu_scan_state(0)'), 0)
        click_text(p, 'WiFi Scan\n& Attack')
        p.wait_for_function('emulator.module._emu_scan_state(0) === 2')
        self.completed = True

    def test_numeric_text_commits_to_scan_setting(self):
        p = self.page
        self.settings()
        click_text(p, 'Scan\nSetup')
        # The real Spinbox is the parent of its displayed value label.
        obs = objects(p)
        label = next(o for o in obs if o['text']=='0100')
        spinbox = next(o for o in obs if o['id']==label['parent'])
        click_object(p, spinbox)
        p.keyboard.type('250')
        p.keyboard.press('Enter')
        click_text(p, 'Save')
        click_text(p, 'Scan\nSetup')
        self.assertTrue(any(o['text']=='0250' for o in objects(p)))
        self.completed = True

    def test_settings_controls_and_restart(self):
        p = self.page
        self.settings()
        click_text(p, 'Screen\nTimeout')
        dropdown(p, 'show_screen_timeout_popup.dropdown.', 4)
        click_text(p, 'Close')
        self.assertEqual(stored(p, 'scr_timeout'), '4')
        click_text(p, 'Screen\nBrightness')
        slider = bound(p, 'show_screen_brightness_popup.screen_brightness_slider.value_changed')
        p.mouse.move(*position(p, slider, .8))
        p.mouse.down()
        p.mouse.move(*position(p, slider, .3), steps=8)
        p.mouse.up()
        brightness = stored(p, 'scr_bright')
        self.assertIsNotNone(brightness)
        self.assertLess(int(brightness), 50)
        click_text(p, 'Close')
        click_text(p, 'Theme')
        click_object(p, bound(p, 'show_theme_popup.alert_sound_switch.'))
        dropdown(p, 'show_theme_popup.boot_sound_dropdown.', 0)
        click_text(p, 'Close')
        click_text(p, 'Screen\nRotation')
        dropdown(p, 'show_screen_rotation_popup.dropdown.', 1)
        self.assertTrue(any('Restart to apply' in o['text'] for o in objects(p)))
        click_object(p, bound(p, 'show_screen_rotation_popup.restart_btn.'))
        p.wait_for_url('**/?rotation=1')
        self.ready()
        self.assertEqual(p.evaluate('[emulator.module._emu_width(),emulator.module._emu_height()]'), [1280,720])
        self.settings()
        click_text(p, 'Theme')
        self.assertFalse(bound(p, 'show_theme_popup.alert_sound_switch.')['state'] & 1)
        click_text(p, 'Close')
        click_text(p, 'Screen\nBrightness')
        self.assertTrue(any(o['text']==brightness+'%' for o in objects(p)))
        self.completed = True

    def test_touch_text_and_cancellation_all_rotations_and_scales(self):
        p = self.page
        cdp = self.context.new_cdp_session(p)
        for width in (1440, 390):
            p.set_viewport_size({'width':width,'height':1000 if width==1440 else 844})
            for rotation in range(4):
                with self.subTest(width=width, rotation=rotation):
                    p.goto(f'{self.url}/?rotation={rotation}')
                    self.ready()
                    scan = next(o for o in objects(p) if o['text']=='WiFi Scan\n& Attack')
                    x,y = position(p, scan)
                    cdp.send('Input.dispatchTouchEvent', {'type':'touchStart','touchPoints':[{'x':x,'y':y}]})
                    cdp.send('Input.dispatchTouchEvent', {'type':'touchCancel','touchPoints':[]})
                    self.assertEqual(p.evaluate('emulator.module._emu_scan_state(0)'),0)
                    p.touchscreen.tap(x,y)
                    p.wait_for_function('emulator.module._emu_scan_state(0)===2')
                    row = next(o for o in objects(p) if '/select' in o['binding'])
                    p.touchscreen.tap(*position(p,row))
                    self.assertEqual(p.evaluate('emulator.module._emu_selected_count(0)'),1)
                    if rotation % 2:
                        x,y = position(p,row,.5,.5)
                        # Start further down the list so the upward drag stays in it.
                        box = p.locator('#display').bounding_box()
                        y = box['y']+box['height']*.65
                        cdp.send('Input.dispatchTouchEvent', {'type':'touchStart','touchPoints':[{'x':x,'y':y}]})
                        for step in range(1,9):
                            cdp.send('Input.dispatchTouchEvent', {'type':'touchMove','touchPoints':[{'x':x,'y':y-step*box['height']*.035}]})
                        cdp.send('Input.dispatchTouchEvent', {'type':'touchEnd','touchPoints':[]})
                        p.wait_for_function('''() => {emulator.module._emu_inspect();
                            return emulatorObjects.some(o=>o.scrollY>0);}''')
                    self.settings()
                    click_text(p,'Scan\nSetup')
                    obs=objects(p)
                    label=next(o for o in obs if o['text']=='0100')
                    spinbox=next(o for o in obs if o['id']==label['parent'])
                    p.touchscreen.tap(*position(p,spinbox))
                    p.locator('#text-input').fill('220')
                    p.locator('#text-input').press('Backspace')
                    p.locator('#text-input').fill('5')
                    p.locator('#text-input').press('Enter')
                    click_text(p,'Save')
                    click_text(p,'Scan\nSetup')
                    self.assertTrue(any(o['text']=='0225' for o in objects(p)))
        self.completed = True

    def test_active_scan_blocks_canvas_navigation_and_recovers(self):
        """The production scan overlay is modal: it swallows canvas input without freezing."""
        p = self.page
        p.evaluate('globalThis.unsupportedEvents=[];'
                   'addEventListener("emulator-unavailable", e=>unsupportedEvents.push(e.detail))')
        p.locator('#timing').select_option('1')
        click_text(p, 'WiFi Scan\n& Attack')
        p.wait_for_function('emulator.module._emu_scan_state(0) === 1')
        # show_scan_overlay() covers the screen and is clickable, so the tab bar
        # underneath must not react while the job runs.
        tab = next(o for o in objects(p) if o['text'] == 'MBUS')
        click_object(p, tab)
        self.assertEqual(p.evaluate('emulator.module._emu_current_tab()'), 0)
        frames = p.evaluate('emulator.measurements.frames')
        p.wait_for_function('frames => emulator.measurements.frames > frames + 10', arg=frames)
        p.wait_for_function('emulator.module._emu_scan_state(0) === 2', timeout=60000)
        self.assertEqual(p.evaluate('emulator.module._emu_network_count(0)'), 12)
        # Once the overlay is gone the same click navigates again.
        click_text(p, 'MBUS')
        self.assertEqual(p.evaluate('emulator.module._emu_current_tab()'), 2)
        self.assertEqual(p.evaluate('emulator.module._emu_network_count(2)'), 0)
        self.assertEqual(p.evaluate('unsupportedEvents'), [])
        self.completed = True

    def test_runtime_bindings_match_frozen_identities(self):
        """Every live registration is an enrolled template, unique, and released on close."""
        p = self.page
        p.evaluate('globalThis.unsupportedEvents=[];'
                   'addEventListener("emulator-unavailable", e=>unsupportedEvents.push(e.detail))')
        observed = set()

        def snapshot():
            # Include hidden live objects and every event registration on each
            # object (the brightness slider has both change and release).
            live = p.evaluate('''() => {
                emulator.module._emu_inspect_bindings(); return emulatorBindings;
            }''')
            self.assertTrue(live)
            identifiers = [o['binding'] for o in live]
            self.assertEqual(len(identifiers), len(set(identifiers)), identifiers)
            generations = [o['generation'] for o in live]
            self.assertEqual(len(generations), len(set(generations)), identifiers)
            self.assertTrue(all(g > 0 for g in generations), identifiers)
            for identifier in identifiers:
                template = identifier.split('/')[0]
                # Simulator-owned affordances carry an explicit emu. prefix; every
                # other live registration must be a frozen firmware template.
                if template.startswith('emu.'):
                    continue
                self.assertIn(template, IDENTITIES, identifier)
            observed.update(identifiers)
            return {o['binding']: o['generation'] for o in live}

        snapshot()
        click_text(p, 'WiFi Scan\n& Attack')
        p.wait_for_function('emulator.module._emu_scan_state(0) === 2')
        scan = snapshot()
        # Every discovered network owns its own select/toggle registrations.
        self.assertEqual(len([b for b in scan if b.endswith('/select')]), 12)
        p.evaluate('emulator.module._emu_show(0, 0)')
        self.settings()
        page_bindings = snapshot()
        for tile, close in (('Scan\nSetup', 'Cancel'), ('Screen\nTimeout', 'Close'),
                            ('Screen\nBrightness', 'Close'), ('Theme', 'Close'),
                            ('Screen\nRotation', 'Close')):
            with self.subTest(popup=tile):
                click_text(p, tile)
                opened = snapshot()
                popup = {b: g for b, g in opened.items() if b not in page_bindings}
                self.assertTrue(popup, tile)
                if tile == 'Screen\nBrightness':
                    self.assertEqual(len([b for b in popup if '.screen_brightness_slider.' in b]),2)
                click_text(p, close)
                closed = snapshot()
                # A deleted popup must not leave registrations behind.
                self.assertFalse(set(popup) & set(closed), tile)
                click_text(p, tile)
                reopened = snapshot()
                self.assertEqual(set(popup), set(reopened) - set(page_bindings), tile)
                for binding_id, generation in popup.items():
                    self.assertGreater(reopened[binding_id], generation, binding_id)
                click_text(p, close)
        report['observed_bindings'] = sorted(observed)
        report['observed_templates'] = sorted({b.split('/')[0] for b in observed})
        self.assertEqual(p.evaluate('unsupportedEvents'), [])
        self.completed = True


if __name__ == '__main__':
    unittest.main()
