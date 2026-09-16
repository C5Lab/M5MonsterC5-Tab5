"""S16/S17 acceptance through the visible Queue 2 guide and native Canvas controls."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object, DIST
import test_emulator_phase4_wardrive as wardrive_helpers

class Queue2WardriveBrowser(unittest.TestCase):
    click=wardrive_helpers.WardriveBrowser.click
    text=wardrive_helpers.WardriveBrowser.text
    def setUp(self):
        self.session=browser_session();self.browser,self.url=self.session.__enter__()
        self.addCleanup(self.session.__exit__,None,None,None)
        self.context=self.browser.new_context(viewport={'width':1440,'height':1100})
        self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
    def tearDown(self):self.assertEqual(self.errors,[])
    def load(self,story,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.locator('#queue2-story').select_option(story)
        self.page.locator('#guide-queue2').click()
        self.assertIn('Step 1 of',self.page.locator('#guide-progress').inner_text())
    def at(self,title):
        self.page.wait_for_function('(t)=>document.querySelector("#guide-title").textContent===t',arg=title,timeout=15000)
    def complete(self):
        self.page.wait_for_function('document.querySelector("#guide-progress").textContent.includes("complete")',timeout=15000)
        self.assertEqual(self.page.evaluate('unavailable'),[])
    def open_wd(self):click_text(self.page,'Wardrive')
    def setup(self):
        self.open_wd();self.click('show_wardrive_page.ctx--wardrive_setup_btn')
    def setup_close(self):self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
    def wdback(self):self.click('show_wardrive_page.back_btn')
    def record(self,open_page=True):
        if open_page:self.open_wd()
        self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.at('Stop and save this recording')
        self.click('show_wardrive_page.ctx--wardrive_stop_btn')
    def add_home(self):
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
        self.click('show_home_mgmt_overlay.scan_btn')
        self.click('emu.phase4.wardrive.home.02:20:77:00:00:01.pick')
        self.click('show_home_mgmt_overlay.add_btn')
    def files(self,wardrive):
        click_text(self.page,'Compromised\nData')
        if wardrive:self.click('/Wardrive Files')
        else:click_text(self.page,'Handshakes')
    def file_control(self,fragment,target):
        rows=objects(self.page);lookup={o['id']:o for o in rows}
        candidates=[o for o in rows if fragment in o['binding']]
        matching=[o for o in candidates if target in o['binding']]
        if len(matching)==1:self.click(matching[0]['binding']);return
        for obj in candidates:
            parent=lookup.get(obj['parent'])
            if parent and any(target in r['text'] for r in rows if r['parent']==parent['id']):
                self.click(obj['binding']);return
        self.assertEqual(len(candidates),1,(fragment,target))
        self.click(candidates[0]['binding'])

    def test_record_providers_and_settings_four_rotations(self):
        for rotation in range(4):
            for variant in ['record','wigle','wdgwars','settings']:
                with self.subTest(rotation=rotation,variant=variant):
                    self.load('s17-'+variant,rotation)
                    if variant=='settings':
                        self.setup();self.at('Change and Apply')
                        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_trace_sw')
                        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_apply_btn')
                        self.at('Close Setup');self.setup_close();self.complete();continue
                    self.record()
                    if variant!='record':
                        self.at('Open '+('WiGLE' if variant=='wigle' else 'WDGWars')+' upload')
                        self.click('show_wardrive_page.ctx--wardrive_upload_btn')
                        self.click('show_wardrive_upload_menu.'+variant+'_btn')
                        self.at('Sync the new recording')
                        self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn')
                        self.at('Return to GROVE')
                        self.click('show_wardrive_upload_popup.close_btn')
                        self.click('show_wardrive_upload_menu.close_btn')
                    else:self.at('Return to GROVE')
                    self.wdback();self.complete()

    def test_gps_home_and_blacklist_four_rotations(self):
        for rotation in range(4):
            for variant in ['gps-debug','home','blacklist','home-keep','home-upload']:
                with self.subTest(rotation=rotation,variant=variant):
                    self.load('s17-'+variant,rotation);self.setup()
                    if variant=='gps-debug':
                        self.at('Open GPS Debug')
                        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_gps_debug_btn')
                        self.click('wardrive_gps_debug_btn_cb.ctx--wardrive_gps_debug_start_btn')
                        self.at('Stop debug');self.click('wardrive_gps_debug_btn_cb.ctx--wardrive_gps_debug_stop_btn')
                        self.at('Close debug and Setup');self.click('wardrive_gps_debug_btn_cb.ctx--wardrive_gps_debug_close_btn')
                        self.setup_close();self.complete();continue
                    if variant=='blacklist':
                        self.click('wardrive_setup_btn_cb.bl_btn');self.click('wardrive_blacklist_btn_cb.scan_btn')
                        self.click('/wardrive/Grove/02:20:77:02:00:01/pick')
                        self.at('Close Setup');self.click('wardrive_blacklist_btn_cb.close_btn');self.setup_close()
                        self.at('Start a fresh recording');self.click('show_wardrive_page.ctx--wardrive_start_btn')
                        self.at('Stop and save this recording');self.click('show_wardrive_page.ctx--wardrive_stop_btn')
                        self.at('Return to GROVE');self.wdback();self.complete();continue
                    self.add_home()
                    if variant=='home':
                        self.at('Close Home');self.click('show_home_mgmt_overlay.close_btn')
                        self.at('Close Setup');self.setup_close();self.complete();continue
                    self.at('Arm offline Home upload');self.click('show_home_mgmt_overlay.close_btn')
                    for control in ['wardrive_setup_autoup_sw','wardrive_setup_autoup_wigle_cb']:
                        obj=next(o for o in objects(self.page) if 'wardrive_setup_btn_cb.ctx--'+control in o['binding'])
                        if not obj['state']&1:self.click(obj['binding'])
                    self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_apply_btn');self.setup_close()
                    self.at('Wait for the Home decision');self.click('show_wardrive_page.ctx--wardrive_start_btn')
                    self.at('Keep recording' if variant=='home-keep' else 'Stop and upload')
                    self.click('show_wardrive_home_confirm.'+('no' if variant=='home-keep' else 'yes')+'.clicked')
                    if variant=='home-keep':
                        self.at('Stop and save this recording');self.click('show_wardrive_page.ctx--wardrive_stop_btn')
                    self.at('Return to GROVE');self.wdback();self.complete()

    def test_files_empty_delete_cancel_and_copy_four_rotations(self):
        for rotation in range(4):
            for kind in ['handshakes','wardrive']:
                for action in ['empty','delete','normal']:
                    with self.subTest(rotation=rotation,kind=kind,action=action):
                        wardrive=kind=='wardrive';suffix='' if action=='normal' else '-'+action
                        self.load('s16-'+kind+suffix,rotation)
                        if action!='empty':
                            if wardrive:
                                self.record();self.at('Open Wardrive Files');self.wdback()
                            else:
                                self.page.locator('#pcap-examples').click()
                                self.click('pcap_viewer_add_header.back_btn');self.click('show_espshark_page.back_btn')
                                self.click('show_compromised_data_page.back_btn')
                        self.files(wardrive)
                        if action=='empty':
                            self.at('Return from the empty list');self.click('show_compromised_file_page.back_btn');self.complete();continue
                        if action=='delete':
                            self.at('Ask to delete the target')
                            target='session-' if wardrive else 'example-http-dns-icmp.pcap'
                            self.file_control('delete_btn',target)
                            self.at('Cancel deletion');self.click('show_compromised_delete_confirm.no_btn')
                            self.at('Confirm the same target again');self.file_control('delete_btn',target)
                            self.at('Delete and verify');self.click('show_compromised_delete_confirm.yes_btn')
                            self.at('Close the result');self.click('show_compromised_cleanup_popup.ctx--compromised_cleanup_close_btn')
                            self.click('show_compromised_file_page.back_btn');self.complete();continue
                        if wardrive:
                            self.at('Select this recording');self.click('show_compromised_file_page.cb.value_changed')
                            self.at('Open WiGLE upload');self.click('show_compromised_file_page.back_btn')
                            self.click('show_compromised_data_page.back_btn');self.open_wd()
                            self.click('show_wardrive_page.ctx--wardrive_upload_btn');self.click('show_wardrive_upload_menu.wigle_btn')
                            self.at('Sync the new recording');self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn')
                            self.at('Return to GROVE');self.click('show_wardrive_upload_popup.close_btn')
                            self.click('show_wardrive_upload_menu.close_btn');self.wdback()
                        else:
                            self.at('Copy the HTTP/DNS fixture');self.file_control('copy_btn','example-http-dns-icmp.pcap')
                            self.at('Close the transfer and return');self.click('compromised_transfer_show_popup.compromised_transfer_ui.action_btn')
                            self.click('show_compromised_file_page.back_btn')
                        self.complete()

    def test_restart_wrong_module_cancel_and_disconnect_do_not_complete(self):
        self.load('s17-record');self.open_wd();self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.at('Watch GPS and networks')
        self.click('create_tab_bar.internal_tab_btn')
        self.page.locator('#guide-restart').click();self.at('Start a fresh recording')
        self.click('create_tab_bar.grove_tab_btn');self.page.wait_for_timeout(350)
        self.at('Start a fresh recording')
        self.click('create_tab_bar.mbus_tab_btn');click_text(self.page,'Wardrive')
        self.click('show_wardrive_page.ctx--wardrive_start_btn');self.page.wait_for_timeout(500)
        self.at('Start a fresh recording')
        self.click('create_tab_bar.grove_tab_btn');self.page.locator('#cancel').click()
        self.click('show_wardrive_page.ctx--wardrive_start_btn');self.at('Watch GPS and networks')
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.page.wait_for_timeout(500)
        self.assertNotIn('complete',self.page.locator('#guide-progress').inner_text())

    def test_missing_gps_and_sd_four_rotations(self):
        for rotation in range(4):
            for variant in ['no-gps','no-sd']:
                with self.subTest(rotation=rotation,variant=variant):
                    self.load('s17-'+variant,rotation)
                    self.page.get_by_text('Demo module conditions',exact=True).click()
                    if variant=='no-gps':
                        self.page.locator('#live-gps').select_option('missing');self.at('Start a fresh recording')
                    self.open_wd();self.click('show_wardrive_page.ctx--wardrive_start_btn')
                    if variant=='no-gps':
                        self.at('Restore GPS');self.page.locator('#live-gps').select_option('available')
                        self.at('Stop and save this recording')
                    else:
                        self.at('Stop with missing SD');self.page.locator('#live-sd').select_option('missing')
                    self.click('show_wardrive_page.ctx--wardrive_stop_btn')
                    if variant=='no-sd':
                        self.at('Restore SD');self.page.locator('#live-sd').select_option('available')
                    self.at('Return to GROVE');self.wdback();self.complete()

    def test_handshakes_demo_upload_cancel_retry_four_rotations(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load('s16-handshakes-upload',rotation)
                self.page.locator('#timing').select_option('1')
                self.files(False);self.at('Start a demo WPA-SEC upload')
                for attempt in range(2):
                    click_text(self.page,'Send to wpa-sec',contains=True)
                    self.page.wait_for_function("()=>{emulator.module._emu_inspect();return emulatorObjects.some(o=>o.text==='NEON-BAZAAR')} ")
                    self.click('emu.phase4.wpasec.network.0.0.pick/Grove')
                    self.page.wait_for_timeout(100)
                    click_text(self.page,'Connect')
                    self.at('Cancel this upload' if attempt==0 else 'Verify both demo results')
                    if attempt==0:
                        click_text(self.page,'Close');self.at('Retry the demo upload')
                self.at('Close and return');click_text(self.page,'Close')
                self.click('show_compromised_file_page.back_btn');self.complete()

    def test_native_uploaded_all_none_selection_is_distinct(self):
        self.load('s17-wigle');self.record()
        self.at('Open WiGLE upload');self.click('show_wardrive_page.ctx--wardrive_upload_btn')
        self.click('show_wardrive_upload_menu.wigle_btn');self.at('Sync the new recording')
        self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn');self.at('Return to GROVE')
        self.click('show_wardrive_upload_popup.close_btn')
        # Closing a successful upload reloads the native menu asynchronously;
        # its provider buttons exist but remain disabled until the cache renders.
        self.page.wait_for_function("""()=>{emulator.module._emu_inspect();return emulatorObjects.some(o=>
            emulator.module.UTF8ToString(emulator.module._emu_binding_id(o.id)).includes('show_wardrive_upload_menu.wdgwars_btn') && !(o.state & 128))}""")
        self.click('show_wardrive_upload_menu.wdgwars_btn')
        self.page.wait_for_function("""()=>{emulator.module._emu_inspect();return emulator.module._emu_queue2_wardrive_state(0,21)===2 && emulatorObjects.some(o=>
            emulator.module.UTF8ToString(emulator.module._emu_binding_id(o.id)).includes('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn'))}""")
        self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn')
        self.page.wait_for_function("emulator.device.device.wardrive('grove').sessions[0].uploaded")
        self.click('show_wardrive_upload_popup.close_btn');self.click('show_wardrive_upload_menu.close_btn')
        self.wdback();self.complete();self.page.locator('#guide-leave').click()
        self.open_wd();self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.page.wait_for_function("emulator.device.device.wardrive('grove').wifiCount>0")
        self.click('show_wardrive_page.ctx--wardrive_stop_btn');self.wdback();self.files(True)
        for slot,want in [('select-uploaded',1),('select-all',2),('select-none',0)]:
            self.click('/'+slot)
            self.assertEqual(self.page.evaluate('emulator.module._emu_queue2_wardrive_state(0,19)'),want)
        self.assertEqual(len(self.page.evaluate("emulator.device.device.wardrive('grove').sessions")),2)
        self.assertEqual(self.page.evaluate("emulator.device.device.wardrive('mbus').sessions"),[])
        self.assertEqual(self.page.evaluate('unavailable'),[])

if __name__=='__main__':unittest.main(verbosity=2)
