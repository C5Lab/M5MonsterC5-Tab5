"""S15 guided ESPShark / PCAP analysis driven through the native viewer."""
import unittest
from emulator_browser import objects, browser_session
import test_emulator_phase4_pcap_deep as deep

class PcapStoryBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session=browser_session();cls.browser,cls.url=cls.session.__enter__()
    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None,None,None)
    text=deep.PcapDeepBrowser.text
    tick=deep.PcapDeepBrowser.tick
    wait_text=deep.PcapDeepBrowser.wait_text
    click=deep.PcapDeepBrowser.click
    click_label=deep.PcapDeepBrowser.click_label
    open_capture=deep.PcapDeepBrowser.open_capture
    close_analysis=deep.PcapDeepBrowser.close_analysis

    def setUp(self):
        self.context=self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))

    def begin(self,story_id,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.page.locator('#queue2-story').select_option(story_id)
        self.page.locator('#guide-queue2').click()
        self.page.evaluate("async id=>{const {pcapStories}=await import('./pcap-story.mjs');globalThis.pcStory=pcapStories.find(e=>e.id===id).create().story;}",story_id)
        self.page.locator('#pcap-examples').click();self.wait_text('example-http-dns-icmp.pcap')

    def stage(self,n):
        self.page.wait_for_function('(n)=>document.querySelector("#guide-title").textContent===(n===pcStory.steps.length?pcStory.completionTitle:pcStory.steps[n].title)',arg=n,timeout=9000)

    def done(self):
        self.page.wait_for_function('document.querySelector("#guide-title").textContent===pcStory.completionTitle',timeout=9000)
        self.assertIn('complete',self.page.locator('#guide-progress').inner_text())
        self.assertEqual(self.errors,[]);self.assertEqual(self.page.evaluate('unavailable'),[])

    # The deep submenu walks drive the native scrolling viewer at the default
    # rotation, matching Phase 4.3 coverage. Guide behaviour is rotation-invariant
    # (it reads native text): the four-rotation evidence is the invalid/recovery
    # walk below, and every guide state machine is covered per rotation in the
    # logic suite test_emulator_pcap_story.mjs.
    def test_analysis_walks_every_submenu(self):
        self.begin('espshark-analysis')
        self.open_capture();self.stage(1)
        self.click('pcap_viewer_render_capture_page.map_btn');self.wait_text('ESPShark Network Topology');self.stage(2)
        self.click('pcap_viewer_map_cb.close')
        self.click('pcap_viewer_render_capture_page.devices_btn');self.wait_text('Local Devices (tap to filter)');self.stage(3)
        remote=next(o['text'] for o in objects(self.page) if o['text'].startswith('REMOTE ('))
        self.click_label(remote);self.wait_text('Remote Endpoints (tap to filter)');self.stage(4)
        self.close_analysis()
        self.click('pcap_viewer_render_capture_page.connections_btn');self.wait_text('Connections (tap FILTER or FOLLOW)');self.stage(5)
        self.close_analysis()
        self.click('pcap_viewer_render_capture_page.protocols_btn');self.wait_text('Top Application Protocols');self.stage(6)
        self.close_analysis();self.done()

    def test_investigate_packet_health_fqdn_export(self):
        self.begin('espshark-investigate')
        self.open_capture();self.stage(1)
        self.click(':packet-1/packet');self.wait_text('Packet #1 details');self.stage(2)
        self.click('pcap_viewer_packet_detail_cb.close_btn')
        self.click('pcap_viewer_render_capture_page.health_btn');self.wait_text('ESPShark Offline Investigation')
        for mode in ('TIMELINE','BASELINE','INTEL'):self.click_label(mode)
        self.wait_text('Offline intelligence:');self.stage(3)
        self.close_analysis()
        self.click('pcap_viewer_render_capture_page.state--fqdn_btn');self.wait_text('FQDN ON');self.stage(4)
        self.click('pcap_viewer_render_capture_page.tools_btn');self.wait_text('ESPShark Cache & Export');self.stage(5)
        self.click_label('CLOSE');self.done()

    def test_extract_objects_preview_and_hash(self):
        self.begin('espshark-extract')
        self.open_capture();self.stage(1)
        self.click('pcap_viewer_render_capture_page.objects_btn');self.wait_text('Extracted Objects (1)')
        self.assertIn('HTTP 200',self.text());self.stage(2)
        self.click_label('PREVIEW');self.wait_text('Offline synthetic example.')
        self.assertIn('SHA-256',self.text());self.stage(3)
        self.click('pcap_viewer_show_summary_popup.close_btn')
        if 'Extracted Objects' in self.text():self.close_analysis()
        self.done()

    def test_invalid_capture_then_recovery_all_rotations(self):
        # Rotation-independence evidence for the guide across all four rotations.
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.begin('espshark-invalid',rotation)
                self.click('example-invalid-truncated.pcap/open');self.wait_text('Truncated PCAP');self.stage(1)
                self.open_capture();self.done()

if __name__=='__main__':unittest.main(verbosity=2)
