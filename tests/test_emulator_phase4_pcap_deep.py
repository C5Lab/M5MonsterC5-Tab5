"""Phase 4.3 native PCAP acceptance. Run only after rebuilding the emulator."""
import json, unittest
from emulator_browser import ROOT, browser_session, objects, click_object


class PcapDeepBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.context = self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.page.locator('#pcap-examples').click()
        self.wait_text('example-http-dns-icmp.pcap')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])

    def text(self):
        return '\n'.join(obj['text'] for obj in objects(self.page))

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def wait_text(self, text):
        for _ in range(100):
            if text in self.text():
                return
            self.tick()
            self.page.wait_for_timeout(20)
        self.fail(f'Native text missing: {text}\n{self.text()}')

    def click(self, binding):
        """Click actual canvas controls, scrolling through their clipped parents."""
        matches = [obj for obj in objects(self.page) if binding in obj['binding']]
        self.assertEqual(len(matches), 1, binding)
        exact = matches[0]['binding']
        for _ in range(20):
            rows = objects(self.page)
            obj = next(obj for obj in rows if obj['binding'] == exact)
            lookup = {row['id']: row for row in rows}
            parents = []
            parent = lookup.get(obj['parent'])
            while parent:
                parents.append(parent)
                parent = lookup.get(parent['parent'])
            width, height = self.page.locator('#display').evaluate('(c)=>[c.width,c.height]')
            clip = [0, 0, width, height]
            for parent in reversed(parents):
                candidate = [max(clip[0], parent['x']), max(clip[1], parent['y']),
                             min(clip[2], parent['x'] + parent['width']),
                             min(clip[3], parent['y'] + parent['height'])]
                if candidate[2] > candidate[0] and candidate[3] > candidate[1]:
                    clip = candidate
            x, y = obj['x'] + obj['width']/2, obj['y'] + obj['height']/2
            if clip[0] <= x < clip[2] and clip[1] <= y < clip[3]:
                click_object(self.page, obj)
                self.tick()
                return
            self.page.evaluate('(a)=>emulator.module._emu_wheel(...a)',
                               [(clip[0]+clip[2])/2, (clip[1]+clip[3])/2,
                                300 if y >= clip[3] else -300])
            self.tick()
        self.fail('Native control unreachable by scrolling: ' + exact)

    def click_label(self, label):
        rows = objects(self.page)
        matches = [obj for obj in rows if obj['text'] == label]
        self.assertEqual(len(matches), 1, label)
        lookup = {obj['id']: obj for obj in rows}
        obj = matches[0]
        while not obj['binding']:
            obj = lookup[obj['parent']]
        self.click(obj['binding'])

    def open_capture(self):
        self.click('example-http-dns-icmp.pcap/open')
        self.wait_text('14 packets')
        self.assertIn('indexed 14', self.text())

    def close_analysis(self):
        self.click('pcap_viewer_create_analysis_list.close_btn')

    def test_native_deep_views(self):
        self.open_capture()
        self.click('pcap_viewer_render_capture_page.map_btn')
        self.wait_text('ESPShark Network Topology')
        self.assertIn('nodes', self.text())
        for mode in ('TRAFFIC', 'THREATS', 'SERVICES', 'TOPOLOGY'):
            self.click_label(mode)
        self.click('pcap_viewer_map_cb.close')
        self.click('pcap_viewer_render_capture_page.devices_btn')
        self.wait_text('Local Devices (tap to filter)')
        # Documentation IPv4 ranges are WAN peers; exercise the remote inventory.
        remote = next(obj['text'] for obj in objects(self.page) if obj['text'].startswith('REMOTE ('))
        self.click_label(remote)
        self.wait_text('Remote Endpoints (tap to filter)')
        self.assertIn('198.51.100.20', self.text())
        self.close_analysis()
        for button, title, evidence in (
            ('connections_btn', 'Connections (tap FILTER or FOLLOW)', '198.51.100.20:80'),
            ('protocols_btn', 'Top Application Protocols', 'HTTP'),
            ('health_btn', 'ESPShark Offline Investigation', 'Passive bounded evidence'),
        ):
            self.click('pcap_viewer_render_capture_page.' + button)
            self.wait_text(title)
            self.assertIn(evidence, self.text())
            if button == 'health_btn':
                for mode in ('TIMELINE', 'BASELINE', 'INTEL'):
                    self.click_label(mode)
                self.assertIn('Offline intelligence:', self.text())
            self.close_analysis()
        self.click('pcap_viewer_render_capture_page.state--fqdn_btn')
        self.assertIn('example.com', self.text())
        self.click(':packet-1/packet')
        self.wait_text('Packet #1 details')
        self.assertIn('HEX / ASCII', self.text())
        self.assertIn('203.0.113.53', self.text())
        self.click('pcap_viewer_packet_detail_cb.close_btn')
        self.click('pcap_viewer_render_capture_page.tools_btn')
        self.wait_text('ESPShark Cache & Export')
        self.assertIn('14 indexed match', self.text())
        self.assertIn('EXPORT FILTERED PCAP', self.text())
        self.click_label('CLOSE')

    def test_file_pages_back_after_switching_sources(self):
        self.click('pcap_viewer_add_header.back_btn')
        for _ in range(2):
            self.click_label('\uf019  READ FROM MONSTER')
            self.wait_text('ESPShark - Monster SD')
            self.click('show_compromised_file_page.back_btn')
            self.wait_text('Capture  |  Inspect  |  Analyze')
            self.click('show_espshark_page.back_btn')
            self.click('/Handshakes')
            self.click('show_compromised_file_page.back_btn')
            self.wait_text('Compromised Data')
            self.click('show_compromised_data_page.back_btn')
            self.click('/Compromised Data')
            self.click('/ESPShark')
        self.click_label('\uf06e  OPEN FROM TAB5')
        self.wait_text('ESPShark - TAB5 SD')
        self.click('pcap_viewer_add_header.back_btn')
        self.wait_text('Capture  |  Inspect  |  Analyze')

    def test_native_http_object_export_preserves_capture(self):
        self.open_capture()
        files = self.page.evaluate('''() => {
          const fs=emulator.module.FS, out=[];
          function visit(path){for(const name of fs.readdir(path)){
            if(name==='.'||name==='..')continue;
            const p=path+'/'+name;
            if(fs.isDir(fs.stat(p).mode))visit(p);else out.push(p);
          }} visit('/sdcard');return out;
        }''')
        source = next(path for path in files if path.endswith('/example-http-dns-icmp.pcap'))
        original = self.page.evaluate('(p)=>Array.from(emulator.module.FS.readFile(p))', source)
        self.assertEqual(len(original), 1225)
        self.click('pcap_viewer_render_capture_page.objects_btn')
        self.wait_text('Extracted Objects (1)')
        self.assertIn('HTTP 200', self.text())
        self.assertIn('example.com', self.text())
        self.click_label('PREVIEW')
        self.wait_text('Offline synthetic example.')
        expected = list(b'Offline synthetic example.\n')
        artifacts = self.page.evaluate('''size => {
          const fs=emulator.module.FS, out=[];
          function visit(path){for(const name of fs.readdir(path)){
            if(name==='.'||name==='..')continue;
            const p=path+'/'+name;
            if(fs.isDir(fs.stat(p).mode))visit(p);
            else if(fs.stat(p).size===size)out.push({path:p,bytes:Array.from(fs.readFile(p))});
          }} visit('/sdcard');return out;
        }''', len(expected))
        self.assertTrue(any(item['bytes'] == expected for item in artifacts), artifacts)
        self.assertEqual(self.page.evaluate('(p)=>Array.from(emulator.module.FS.readFile(p))', source), original)

    def test_invalid_capture_shows_native_error(self):
        self.click('example-invalid-truncated.pcap/open')
        self.wait_text('Truncated PCAP')
        self.assertIn('example-invalid-truncated.pcap', self.text())
        self.assertNotIn('indexed 14', self.text())

    def test_full_storage_shows_error_without_partial_install(self):
        seed = json.loads((ROOT/'docs/ui-emulator/neon-district.seed.json').read_text())
        self.page.evaluate('''seed => {
          const bridge=emulator.device;
          bridge.device=new bridge.device.constructor(seed,{sdCapacityBytes:200});
        }''', seed)
        self.page.locator('#pcap-examples').click()
        self.assertIn('storage_full', self.page.locator('#notice').inner_text())
        self.assertEqual(self.page.evaluate("emulator.device.device.files('grove')"), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
