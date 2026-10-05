"""Native LVGL regression for pending scroll refresh and newly discovered APs."""
import unittest
from emulator_browser import objects, click_text, click_object
import test_emulator_portal_demo as helpers


class ObserverStressBrowser(unittest.TestCase):
    setUp = helpers.PortalDemoBrowser.setUp
    load = helpers.PortalDemoBrowser.load
    ready = helpers.PortalDemoBrowser.ready

    def refresh(self, count, clients, step, scroll):
        self.page.evaluate('(p)=>emulator.module._emu_observer_stress(...p)', [count, clients, step, scroll])
        result = self.page.evaluate('observerStress')
        self.assertEqual(result['tiles'], count, result)
        self.assertEqual(result['clientRows'], clients if result['expanded'] else 0, result)
        self.assertLessEqual(result['totalClientRows'], 20, result)
        # This is a desktop responsiveness bound, not an ESP32 timing claim.
        self.assertLess(result['total'], 3000, result)
        return result

    def test_pending_scrolled_new_aps_never_duplicate_tiles(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                click_text(self.page, 'Network\nObserver')
                self.page.wait_for_timeout(100)
                click_text(self.page, 'Start')
                self.page.wait_for_timeout(100)
                first_tile = self.refresh(80, 18, 0, 0)["firstTile"]
                toggle = next(o for o in objects(self.page) if o['binding'].endswith('/clients-toggle'))
                click_object(self.page, toggle)
                self.page.wait_for_timeout(100)
                self.assertTrue(self.refresh(80, 20, 1, 0)['expanded'])
                for count, step in ((90, 2), (100, 3)):
                    result = self.refresh(count, 20, step, 1)
                    self.assertTrue(result['expanded'], result)
                    self.assertEqual(result['firstTile'], first_tile, result)
                    self.assertGreater(result['scrollY'], 0, result)
                for step in range(4, 8):
                    result = self.refresh(100, 20, step, step % 2)
                    self.assertTrue(result['expanded'], result)
                    self.assertEqual(result['firstTile'], first_tile, result)
                self.assertEqual(self.errors, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
