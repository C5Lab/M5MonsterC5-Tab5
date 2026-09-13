"""Continuous offline Mesh recon: native controls, live data and cleanup."""
import unittest
from emulator_browser import objects, click_text, click_object
import test_emulator_portal_demo as helpers

class MeshMonitorBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    text=helpers.PortalDemoBrowser.text
    advance=helpers.PortalDemoBrowser.advance
    clean=helpers.PortalDemoBrowser.clean

    def test_continuous_results_stop_restart_clear_and_back_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                click_text(p,'Mesh\nRecon');click_text(p,'Start',contains=True)
                job=p.evaluate('emulator.device.snapshot(0).active')
                if rotation==0:
                    p.wait_for_function('(id)=>emulator.device.device.job(id).result?.text.includes("pans=2")',arg=job,timeout=9000)
                else:
                    self.advance(6000)
                self.assertIn('Monitoring synthetic Mesh',self.text())
                self.assertEqual(len([o for o in objects(p) if o['binding'].endswith('/pan')]),2)
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'running')
                pan=next(o for o in objects(p) if o['binding'].endswith('/pan'))
                click_object(p,pan)
                self.advance(1200)
                self.assertTrue(any(o['binding'].endswith('/node') for o in objects(p)))
                click_text(p,'Stop',contains=True)
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                self.assertIn('stopped - results stay visible',self.text())
                frozen=self.text();self.advance(6000);self.assertEqual(self.text(),frozen)
                click_text(p,'Start',contains=True)
                self.assertIn('waiting for networks',self.text())
                self.assertNotIn('0x1A2B',self.text())
                self.advance(6000);click_text(p,'Clear')
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                self.advance(6000);self.assertIn('No Mesh',self.text())
                click_text(p,'Start',contains=True)
                back=next(o for o in objects(p) if 'show_zig_recon_page.back_btn' in o['binding'])
                click_object(p,back)
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                click_text(p,'Mesh\nRecon')
                self.assertIn('stopped - results stay visible',self.text());self.clean()

    def test_disconnect_and_shell_cancel_release_stop(self):
        p=self.page;self.load();click_text(p,'Mesh\nRecon')
        for disconnect in [True,False]:
            click_text(p,'Start',contains=True);self.advance(3000)
            if disconnect:p.evaluate("emulator.device.device.setModule('grove',{connected:false})")
            else:p.locator('#cancel').click()
            self.advance(100)
            self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
            frozen=self.text();self.advance(6000);self.assertEqual(self.text(),frozen)
            p.evaluate("emulator.device.device.setModule('grove',{connected:true})")
        self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
