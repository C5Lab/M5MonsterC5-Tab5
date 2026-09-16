"""Native confirmation before leaving a running Observer."""
import unittest
from emulator_browser import objects,click_text,click_object
import test_emulator_portal_demo as helpers
class ObserverExitBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    scan=helpers.PortalDemoBrowser.scan
    text=helpers.PortalDemoBrowser.text
    def back(self):
        click_object(self.page,next(o for o in objects(self.page) if 'show_observer_page.back_btn' in o['binding']))
    def test_keep_running_then_stop_and_exit_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                click_text(p,'Network\nObserver');click_text(p,'Start')
                self.assertEqual(len([o for o in objects(p) if o['binding'].endswith('/observe')]),12)
                self.assertTrue(any(o['binding'].endswith('/client') for o in objects(p)))
                self.assertEqual(p.evaluate('emulator.device.snapshot(0).networks'),[])
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).selected'))
                self.back()
                self.assertIn('OBSERVER IS RUNNING',self.text())
                click_text(p,'Keep running');self.assertTrue(p.evaluate('emulator.device.device.observer("grove").running'))
                self.back();click_text(p,'Stop and exit',contains=True)
                self.assertFalse(p.evaluate('emulator.device.device.observer("grove").running'))
                self.assertNotIn('OBSERVER IS RUNNING',self.text());self.assertIn('Network\nObserver',self.text());self.clean()
if __name__=='__main__':unittest.main(verbosity=2)
