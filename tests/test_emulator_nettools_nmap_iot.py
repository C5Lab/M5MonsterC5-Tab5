"""Native nmap/IoT acceptance; run after rebuilding the emulator."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object

class NettoolsNmapIotBrowser(unittest.TestCase):
    def test_mesh_scan_clear_and_navigation(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'Mesh\nRecon')
                click_text(page, 'Start', contains=True)
                click_text(page, 'Stop', contains=True)
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertNotIn('0x', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'Start', contains=True)
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                text = '\n'.join(o['text'] for o in objects(page))
                self.assertIn('0x', text)
                pans = [o for o in objects(page) if o['binding'].endswith('/pan')]
                self.assertEqual(len(pans), 1)
                click_object(page, pans[0])
                nodes = [o for o in objects(page) if o['binding'].endswith('/node')]
                self.assertGreaterEqual(len(nodes), 1)
                click_object(page, nodes[0])
                self.assertIn('0x', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'Clear')
                self.assertIn('No Mesh', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'Start', contains=True)
                page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertNotIn('0x', '\n'.join(o['text'] for o in objects(page)))
                page.evaluate("emulator.device.device.setModule('grove',{connected:true})")
                click_text(page, 'Start', contains=True)
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertIn('0x', '\n'.join(o['text'] for o in objects(page)))

            finally:
                page.close()

    def test_nmap_connect_hosts_quick_scan(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url + '/?rotation=1')
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'WiFi Scan\n& Attack')
                page.wait_for_function('emulator.module._emu_scan_state(0)===2')
                bssid = page.evaluate("emulator.device.snapshot(0).networks.find(n=>n.ssid==='AFTERLIFE-GUEST').bssid")
                rows = [o for o in objects(page) if o['binding'].endswith(bssid + '/select')]
                self.assertEqual(len(rows), 1)
                click_object(page, rows[0])
                click_text(page, 'Nmap')
                self.assertIn('AFTERLIFE-GUEST', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'Connect')
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertIn('Connected (synthetic)', '\n'.join(o['text'] for o in objects(page)))
                click_text(page, 'List Hosts')
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                hosts = [o for o in objects(page) if o['binding'].endswith('192.0.2.10/host')]
                self.assertEqual(len(hosts), 1)
                click_object(page, hosts[0])
                levels = [o for o in objects(page) if o['binding'].endswith('quick/scan-level')]
                self.assertEqual(len(levels), 1)
                click_object(page, levels[0])
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                text = '\n'.join(o['text'] for o in objects(page))
                self.assertIn('80/tcp', text)
                self.assertIn('Done! 1 hosts, 1 open ports', text)
                click_text(page, 'STOP')
                self.assertNotIn('80/tcp', '\n'.join(o['text'] for o in objects(page)))
            finally:
                page.close()

    def test_nmap_requires_single_network(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url + '/?rotation=1')
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'WiFi Scan\n& Attack')
                page.wait_for_function('emulator.module._emu_scan_state(0)===2')
                click_text(page, 'Nmap')
                self.assertIn('Select exactly 1 network', '\n'.join(o['text'] for o in objects(page)))
            finally:
                page.close()

if __name__ == '__main__': unittest.main()
