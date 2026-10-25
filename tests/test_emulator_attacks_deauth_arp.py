"""Native deauth/ARP browser acceptance. Run only after rebuilding emulator."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object

class AttacksDeauthArpBrowser(unittest.TestCase):
    def advance(self, page):
        page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')

    def select_guest(self, page):
        page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(page, 'WiFi Scan\n& Attack')
        page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        bssid = page.evaluate("emulator.device.snapshot(0).networks.find(n=>n.ssid==='AFTERLIFE-GUEST').bssid")
        click_object(page, next(o for o in objects(page) if o['binding'].endswith(bssid + '/select')))

    def test_scan_deauth_start_stop(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.select_guest(page)
                click_text(page, 'Deauth')
                self.advance(page)
                self.assertIn('packets', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'STOP ATTACK')
                self.assertNotIn('STOP ATTACK', '\n'.join(o['text'] for o in objects(page)))
            finally:
                page.close()

    def test_observer_station_deauth_can_start_again_after_stop(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={'width': 1440, 'height': 1000})
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'Network\nObserver')
                click_text(page, 'Start')
                for attempt in range(2):
                    client_rows = [o for o in objects(page) if o['binding'].endswith('/client')]
                    if not client_rows:
                        toggle = next(o for o in objects(page) if o['binding'].endswith('/clients-toggle'))
                        click_object(page, toggle)
                        client_rows = [o for o in objects(page) if o['binding'].endswith('/client')]
                    self.assertTrue(client_rows)
                    click_object(page, client_rows[0])
                    buttons = [o for o in objects(page) if 'ui.show_deauth_popup.deauth_btn.clicked' in o['binding']]
                    self.assertTrue(buttons)
                    button = buttons[0]
                    click_object(page, button)
                    active = page.evaluate('emulator.device.snapshot(0).active')
                    self.assertIsNotNone(active, f'Observer deauth attempt {attempt + 1} did not start')
                    click_text(page, 'STOP')
                    self.assertIsNone(page.evaluate('emulator.device.snapshot(0).active'))
            finally:
                page.close()

    def test_observer_station_deauth_can_restart_after_attack_tile(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={'width': 1440, 'height': 1000})
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'Network\nObserver')
                click_text(page, 'Start')
                toggle = next(o for o in objects(page) if o['binding'].endswith('/clients-toggle'))
                click_object(page, toggle)
                client = next(o for o in objects(page) if o['binding'].endswith('/client'))
                click_object(page, client)
                button = next(o for o in objects(page) if 'ui.show_deauth_popup.deauth_btn.clicked' in o['binding'])
                click_object(page, button)
                self.assertIsNotNone(page.evaluate('emulator.device.snapshot(0).active'))
                self.assertFalse(any('ui.show_network_popup.' in o['binding'] for o in objects(page)))
                tiles = [o for o in objects(page) if o['binding'].endswith('/Evil Twin')]
                self.assertEqual(len(tiles), 1, [(o['id'], o['parent'], o['x'], o['y']) for o in tiles])
                tile = tiles[0]
                click_object(page, tile)
                station_controls = [o['binding'] for o in objects(page)
                                    if 'ui.show_deauth_popup.' in o['binding']]
                self.assertEqual(station_controls, [])
                evil_stop = next(o for o in objects(page)
                                 if 'ui.show_evil_twin_popup.stop_btn.clicked' in o['binding'])
                click_object(page, evil_stop)
                page.evaluate('emulator.module._emu_show(0,0)')
                click_text(page, 'Network\nObserver')
                client_rows = [o for o in objects(page) if o['binding'].endswith('/client')]
                if not client_rows:
                    toggles = [o for o in objects(page) if o['binding'].endswith('/clients-toggle')]
                    if not toggles:
                        self.assertTrue(any(o['text'] == 'Start' for o in objects(page)),
                                        [(o['binding'], o['text']) for o in objects(page) if o['binding'] or o['text']])
                        click_text(page, 'Start')
                        toggles = [o for o in objects(page) if o['binding'].endswith('/clients-toggle')]
                    self.assertTrue(toggles)
                    toggle = toggles[0]
                    click_object(page, toggle)
                    client_rows = [o for o in objects(page) if o['binding'].endswith('/client')]
                self.assertTrue(client_rows)
                click_object(page, client_rows[0])
                button = next(o for o in objects(page) if 'ui.show_deauth_popup.deauth_btn.clicked' in o['binding'])
                click_object(page, button)
                self.assertIsNotNone(page.evaluate('emulator.device.snapshot(0).active'))
            finally:
                page.close()

    def test_observer_header_stop_closes_station_card(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={'width': 1440, 'height': 1000})
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'Network\nObserver')
                click_text(page, 'Start')
                toggle = next(o for o in objects(page) if o['binding'].endswith('/clients-toggle'))
                click_object(page, toggle)
                client = next(o for o in objects(page) if o['binding'].endswith('/client'))
                click_object(page, client)
                button = next(o for o in objects(page) if 'ui.show_deauth_popup.deauth_btn.clicked' in o['binding'])
                click_object(page, button)
                self.assertIsNotNone(page.evaluate('emulator.device.snapshot(0).active'))
                observer_stop = next(o for o in objects(page)
                                     if 'ui.show_observer_page.ctx--observer_stop_btn.clicked' in o['binding'])
                click_object(page, observer_stop)
                self.assertIsNone(page.evaluate('emulator.device.snapshot(0).active'))
                self.assertFalse(any('ui.show_deauth_popup.' in o['binding'] for o in objects(page)))
            finally:
                page.close()

    def test_arp_connect_hosts_attack_disconnect(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.select_guest(page)
                click_text(page, 'ARP')
                click_text(page, 'Connect')
                self.advance(page)
                self.assertIn('Connected (synthetic)', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'List Hosts')
                self.advance(page)
                host = next(o for o in objects(page) if o['binding'].endswith('192.0.2.10/host'))
                click_object(page, host)
                self.advance(page)
                self.assertIn('packets', '\n'.join(o['text'] for o in objects(page)))
                page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
                self.advance(page)
                self.assertNotIn('Simulated |', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'STOP')
            finally:
                page.close()

if __name__ == '__main__':
    unittest.main()
