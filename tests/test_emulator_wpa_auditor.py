"""Native LVGL acceptance for the offline WPA PSK Auditor. Rebuild Wasm first."""
import unittest
from emulator_browser import browser_session, objects, click_text
import test_emulator_phase4_pcap_deep as navigation


class WpaAuditorBrowser(unittest.TestCase):
    click = navigation.PcapDeepBrowser.click
    click_label = navigation.PcapDeepBrowser.click_label
    wait_text = navigation.PcapDeepBrowser.wait_text

    def setUp(self):
        self.session=browser_session()
        self.browser,url=self.session.__enter__()
        self.addCleanup(self.session.__exit__,None,None,None)
        self.context=self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page=self.context.new_page()
        self.errors=[]
        self.page.on('pageerror',lambda error:self.errors.append(str(error)))
        self.page.goto(url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page,'INTERNAL')
        self.tick()

    def tearDown(self):
        self.assertEqual(self.errors,[])
        self.assertEqual(self.page.evaluate('unavailable'),[])

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(10)')

    def advance(self,ms):
        self.page.evaluate('(ms)=>{for(let left=ms;left>0;left-=100)emulator.module._emu_tick(Math.min(100,left))}',ms)

    def text(self):
        return '\n'.join(obj['text'] for obj in objects(self.page))

    def open(self):
        self.click_label('WPA PSK\nAuditor')
        self.wait_text('Capture readiness, durable resume and audit history')

    def test_catalog_audit_pause_resume_history_and_back(self):
        self.open()
        self.assertIn('NEON-BAZAAR',self.text())
        self.assertIn('Missing M2',self.text())
        self.click('emu.wpa.capture.capture-neon.audit')
        self.wait_text('Audit running')
        self.advance(1200)
        self.click('emu.wpa.audit.pause')
        self.wait_text('Resume')
        self.assertGreater(self.page.evaluate('emulator.device.wpa.sessions()[0].tried'),0)
        self.click('emu.wpa.session.audit-1.resume')
        self.advance(6000)
        self.wait_text('NeonDemo-2026!')
        self.assertIn('FOUND',self.text())
        self.assertNotIn('NeonDemo-2026!',str(self.page.evaluate('emulator.device.wpa.history()')))
        self.click('emu.wpa.back')
        self.assertIn('WPA PSK\nAuditor',self.text())

    def test_batch_sync_and_reload_restore_checkpoint(self):
        self.open()
        self.click('emu.wpa.sync-all')
        self.wait_text('2 copied')
        self.click('emu.wpa.capture.capture-neon.select')
        self.click('emu.wpa.capture.capture-ghost.select')
        self.click('emu.wpa.batch.add')
        self.wait_text('2 queued')
        self.click('emu.wpa.batch.start')
        self.advance(1300)
        self.click('emu.wpa.audit.pause')
        self.assertEqual(len(self.page.evaluate("emulator.device.device.files('internal')")),3)
        self.page.reload()
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page,'INTERNAL')
        self.tick()
        self.open()
        self.assertIn('Paused',self.text())
        self.assertEqual(len(self.page.evaluate('emulator.device.wpa.queue()')),2)
        self.assertFalse(self.page.evaluate("emulator.device.wpa.catalog().find(row=>row.id==='capture-neon').local"))
        self.assertFalse(self.page.evaluate("emulator.device.wpa.catalog().find(row=>row.id==='capture-ghost').local"))
        self.assertEqual(self.page.evaluate("emulator.device.device.files('internal').map(file=>file.path)"),
                         ['/sdcard/lab/handshakes/synapse-relay-local.hccapx'])

    def test_handshaker_crack_latest_opens_matching_synthetic_capture(self):
        self.click_label('GROVE')
        self.click_label('WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        bssid=self.page.evaluate("emulator.device.snapshot(0).networks[0].bssid")
        self.click(bssid+'/select')
        self.click_label('Handshake')
        self.advance(5000)
        self.assertIn('Handshake captured',self.text())
        self.click_label('Crack latest')
        self.wait_text('WPA PSK Auditor')
        self.assertIn('capture-neon is ready',self.text())
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),3)

    def test_handshaker_crack_latest_uses_captured_network_after_multiple_selections(self):
        self.click_label('GROVE')
        self.click_label('WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        networks=self.page.evaluate('emulator.device.snapshot(0).networks')
        self.click(networks[1]['bssid']+'/select')
        self.click(networks[0]['bssid']+'/select')
        self.click_label('Handshake')
        self.advance(5000)
        self.assertIn('Handshake captured: NEON-BAZAAR',self.text())
        self.click_label('Crack latest')
        self.wait_text('WPA PSK Auditor')
        self.assertIn('capture-neon is ready',self.text())
        self.assertNotIn('Crack latest: capture-ghost',self.text())

    def test_catalog_selection_preserves_scroll_position(self):
        self.open()
        self.page.evaluate('emulator.module._emu_wheel(350,700,300)')
        self.tick()
        self.assertTrue(any(obj['scrollY']>100 for obj in objects(self.page)))
        self.click('emu.wpa.capture.capture-synapse.select')
        self.assertTrue(any(obj['scrollY']>0 for obj in objects(self.page)))
        self.assertIn('Selected',self.text())

    def test_handshakes_page_keeps_files_and_links_to_synthetic_catalog(self):
        self.click_label('GROVE')
        self.click_label('Compromised\nData')
        self.click_label('Handshakes')
        self.wait_text('Send to wpa-sec')
        self.wait_text('Open WPA Auditor')
        self.click_label('Open WPA Auditor')
        self.wait_text('WPA PSK Auditor')
        self.assertIn('synthetic catalog',self.text())
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),3)

    def test_handshakes_file_crack_routes_known_capture_and_explains_unknown(self):
        self.page.evaluate('''() => {
          const encode=new TextEncoder(),base='/sdcard/lab/handshakes/';
          emulator.device.device.commitFileChanges('grove',[
            {path:base+'neon-bazaar-4way.pcap',bytes:encode.encode('OFFLINE WPA AUDIT FIXTURE capture-neon\\n')},
            {path:base+'chrome-clinic-incomplete.pcap',bytes:encode.encode('OFFLINE WPA AUDIT FIXTURE capture-chrome\\n')},
            {path:base+'unmatched.pcap',bytes:encode.encode('OFFLINE UNMATCHED PCAP')}
          ]);
        }''')
        self.click_label('GROVE')
        self.click_label('Compromised\nData')
        self.click_label('Handshakes')
        self.wait_text('unmatched.pcap')
        self.click('unmatched.pcap/crack')
        self.wait_text('No matching synthetic WPA capture')
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),0)
        self.click('chrome-clinic-incomplete.pcap/crack')
        self.wait_text('needs investigation')
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),0)
        self.click('neon-bazaar-4way.pcap/crack')
        self.wait_text('WPA PSK Auditor')
        self.assertIn('capture-neon is ready',self.text())
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),3)

    def test_handshakes_rejects_forged_bytes_at_fixture_path(self):
        self.page.evaluate('''() => emulator.device.device.commitFileChanges('grove',[
          {path:'/sdcard/lab/handshakes/neon-bazaar-4way.pcap',
           bytes:new TextEncoder().encode('arbitrary PCAP with a fixture name')}
        ])''')
        self.click_label('GROVE')
        self.click_label('Compromised\nData')
        self.click_label('Handshakes')
        self.wait_text('neon-bazaar-4way.pcap')
        self.click('neon-bazaar-4way.pcap/crack')
        self.wait_text('No matching synthetic WPA capture')
        self.assertEqual(self.page.evaluate('emulator.module._emu_current_tab()'),0)
        self.assertEqual(self.page.evaluate('emulator.device.wpa.sessions()'),[])

    def test_handshakes_lists_only_handshake_pcap_paths(self):
        self.page.evaluate('''() => {
          const bytes=new TextEncoder().encode('offline capture');
          emulator.device.device.commitFileChanges('grove',[
            {path:'/sdcard/lab/handshakes/canonical-handshake.pcap',bytes},
            {path:'/sdcard/handshakes/legacy-handshake.pcap',bytes},
            {path:'/sdcard/lab/handshakes/not-a-handshake.pcapng',bytes},
            {path:'/sdcard/lab/pcaps/grove/general-capture.pcap',bytes}
          ]);
        }''')
        self.click_label('GROVE')
        self.click_label('Compromised\nData')
        self.click_label('Handshakes')
        self.wait_text('canonical-handshake.pcap')
        self.assertNotIn('legacy-handshake.pcap',self.text())
        self.assertNotIn('general-capture.pcap',self.text())
        self.assertNotIn('not-a-handshake.pcapng',self.text())

    def test_handshakes_uses_legacy_directory_when_primary_empty(self):
        self.page.evaluate('''() => emulator.device.device.commitFileChanges('grove',[
          {path:'/sdcard/handshakes/legacy-handshake.pcap',
           bytes:new TextEncoder().encode('offline capture')}
        ])''')
        self.click_label('GROVE')
        self.click_label('Compromised\nData')
        self.click_label('Handshakes')
        self.wait_text('legacy-handshake.pcap')

    def test_start_over_and_delete_require_confirmation(self):
        self.open()
        self.click('emu.wpa.capture.capture-neon.audit')
        self.advance(800)
        self.click('emu.wpa.audit.pause')
        checkpoint=self.page.evaluate('emulator.device.wpa.sessions()[0].tried')
        self.click('emu.wpa.session.audit-1.start-over')
        self.wait_text('Start this audit over from zero?')
        self.click('emu.wpa.confirm.cancel')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.sessions()[0].tried'),checkpoint)
        self.click('emu.wpa.session.audit-1.start-over')
        self.click('emu.wpa.confirm.yes')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.active().tried'),0)
        self.click('emu.wpa.audit.pause')
        self.click('emu.wpa.session.audit-1.delete')
        self.wait_text('Delete this saved audit?')
        self.click('emu.wpa.confirm.yes')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.sessions().length'),0)
        self.assertEqual(self.page.evaluate('emulator.device.wpa.catalog().length'),4)

    def test_batch_remove_skip_and_cancel_preserve_checkpoints(self):
        self.open()
        self.click('emu.wpa.capture.capture-neon.select')
        self.click('emu.wpa.capture.capture-ghost.select')
        self.click('emu.wpa.batch.add')
        self.click('emu.wpa.batch.row.1.remove')
        self.assertEqual([row['captureId'] for row in self.page.evaluate('emulator.device.wpa.queue()')],['capture-neon'])
        self.click('emu.wpa.capture.capture-ghost.select')
        self.click('emu.wpa.batch.add')
        self.click('emu.wpa.batch.start')
        self.advance(500)
        self.click('emu.wpa.batch.skip')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.active().captureId'),'capture-ghost')
        self.click('emu.wpa.batch.cancel')
        self.wait_text('Cancel entire batch?')
        self.click('emu.wpa.confirm.cancel')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.queue()[1].state'),'running')
        self.click('emu.wpa.batch.cancel')
        self.click('emu.wpa.confirm.yes')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.queue().map(row=>row.state)'),['cancelled','cancelled'])
        self.assertEqual(self.page.evaluate('emulator.device.wpa.sessions().length'),2)

    def test_delete_local_copy_and_catalog_state_filter(self):
        self.open()
        path='/sdcard/lab/handshakes/synapse-relay-local.hccapx'
        self.assertIn(path,[row['path'] for row in self.page.evaluate("emulator.device.device.files('internal')")])
        self.click('emu.wpa.capture.capture-synapse.select')
        self.click('emu.wpa.capture.capture-synapse.delete-local')
        self.wait_text('Delete local capture?')
        self.click('emu.wpa.confirm.cancel')
        self.assertIn(path,[row['path'] for row in self.page.evaluate("emulator.device.device.files('internal')")])
        self.click('emu.wpa.capture.capture-synapse.delete-local')
        self.click('emu.wpa.confirm.yes')
        self.assertNotIn(path,[row['path'] for row in self.page.evaluate("emulator.device.device.files('internal')")])
        self.assertNotIn('capture-synapse',[row['id'] for row in self.page.evaluate('emulator.device.wpa.catalog()')])
        self.wait_text('Deleted local Tab5 copy')
        self.assertFalse(any(obj['binding'].startswith('emu.wpa.batch.add/') for obj in objects(self.page)))
        self.assertEqual(self.page.evaluate("emulator.device.wpa.catalogState('capture-chrome')"),'Invalid')
        self.click('emu.wpa.state')
        menu=next(obj for obj in objects(self.page) if obj['text'].startswith('All states\n'))
        canvas=self.page.locator('#display')
        box=canvas.bounding_box()
        size=canvas.evaluate('(c)=>({width:c.width,height:c.height})')
        x=menu['x']+menu['width']/2
        y=menu['y']+menu['height']*6.5/7
        self.page.mouse.click(box['x']+x*box['width']/size['width'],box['y']+y*box['height']/size['height'])
        for _ in range(100):
            if 'NEON-BAZAAR | HCCAPX' not in self.text():
                break
            self.tick()
            self.page.wait_for_timeout(20)
        self.assertNotIn('NEON-BAZAAR | HCCAPX',self.text())

    def test_select_visible_excludes_invalid_and_clear_selection(self):
        self.open()
        self.click('emu.wpa.select-visible')
        self.assertIn('Selected',self.text())
        self.click('emu.wpa.clear-selection')
        self.assertFalse(any(obj['binding'].startswith('emu.wpa.batch.add/') for obj in objects(self.page)))
        self.click('emu.wpa.select-visible')
        self.click('emu.wpa.batch.add')
        self.assertEqual([row['captureId'] for row in self.page.evaluate('emulator.device.wpa.queue()')],
                         ['capture-neon','capture-ghost','capture-synapse'])
        self.assertFalse(any(obj['binding'].startswith('emu.wpa.batch.add/') for obj in objects(self.page)))

    def test_queued_wordlist_change_and_new_batch_keep_checkpoint(self):
        self.open()
        self.click('emu.wpa.capture.capture-neon.select')
        self.click('emu.wpa.batch.add')
        self.click('emu.wpa.wordlist')
        menu=next(obj for obj in objects(self.page) if obj['text'].startswith('Demo shortlist\nExpanded demo'))
        canvas=self.page.locator('#display')
        box=canvas.bounding_box()
        size=canvas.evaluate('(c)=>({width:c.width,height:c.height})')
        x=menu['x']+menu['width']/2
        y=menu['y']+menu['height']*.75
        self.page.mouse.click(box['x']+x*box['width']/size['width'],box['y']+y*box['height']/size['height'])
        self.assertEqual(self.page.evaluate('emulator.device.wpa.queue()[0].wordlist'),'Expanded demo')
        self.click('emu.wpa.batch.start')
        self.advance(500)
        self.click('emu.wpa.audit.pause')
        offset=self.page.evaluate('emulator.device.wpa.sessions()[0].tried')
        self.click('emu.wpa.batch.new')
        self.assertEqual(self.page.evaluate('emulator.device.wpa.queue().length'),0)
        self.assertEqual(self.page.evaluate('emulator.device.wpa.sessions()[0].tried'),offset)


if __name__=='__main__':
    unittest.main(verbosity=2)
