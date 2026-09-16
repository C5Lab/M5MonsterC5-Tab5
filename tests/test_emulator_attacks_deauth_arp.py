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
