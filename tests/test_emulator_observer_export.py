"""Native Observer export and navigation over the offline module model."""
import csv
import io
import unittest

from emulator_browser import objects, click_object, click_text
import test_emulator_portal_demo as helpers


class ObserverExportBrowser(unittest.TestCase):
    setUp = helpers.PortalDemoBrowser.setUp
    load = helpers.PortalDemoBrowser.load
    ready = helpers.PortalDemoBrowser.ready
    text = helpers.PortalDemoBrowser.text
    clean = helpers.PortalDemoBrowser.clean

    def click_binding(self, needle):
        matches = [obj for obj in objects(self.page) if needle in obj['binding']]
        self.assertEqual(len(matches), 1, needle)
        click_object(self.page, matches[0])
        self.page.evaluate('emulator.module._emu_tick(100)')

    def observer(self, module='GROVE'):
        click_text(self.page, module)
        click_text(self.page, 'Network\nObserver')
        click_text(self.page, 'Start')
        self.assertEqual(len([obj for obj in objects(self.page) if obj['binding'].endswith('/observe')]), 12)

    def exports(self, module):
        return self.page.evaluate("id => emulator.device.device.files(id).filter(f => f.path.startsWith('/sdcard/lab/observer/'))", module)

    def csv_text(self, module, path):
        return self.page.evaluate("([id,path]) => new TextDecoder().decode(emulator.device.device.readFile(id,path))", [module, path])

    def test_stop_then_export_csv_with_stable_path_and_fields(self):
        self.load()
        self.observer()
        self.click_binding('show_observer_page.ctx--observer_export_btn')
        self.assertIn('Stop observer before export', self.text())
        self.assertEqual(self.exports('grove'), [])
        click_text(self.page, 'Stop')
        self.click_binding('show_observer_page.ctx--observer_export_btn')
        self.assertIn('Exported 12 networks', self.text())
        exported = self.exports('grove')
        self.assertEqual([f['path'] for f in exported], ['/sdcard/lab/observer/observer_grove_0001.csv'])
        rows = list(csv.reader(io.StringIO(self.csv_text('grove', exported[0]['path']))))
        self.assertEqual(rows[0], ['scan_index', 'ssid', 'bssid', 'channel', 'rssi', 'band', 'vendor',
                                   'client_count', 'clients', 'extended', 'client_metadata'])
        self.assertEqual(len(rows), 13)
        self.assertEqual(rows[1][1], 'NEON-BAZAAR')
        self.assertIn('ext_ver=1', rows[1][9])
        self.click_binding('show_observer_page.ctx--observer_export_btn')
        self.assertEqual([f['path'] for f in self.exports('grove')],
                         ['/sdcard/lab/observer/observer_grove_0001.csv',
                          '/sdcard/lab/observer/observer_grove_0002.csv'])
        self.clean()

    def test_export_error_and_back_keeps_modules_separate(self):
        self.load()
        self.observer()
        click_text(self.page, 'Stop')
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:false})")
        self.click_binding('show_observer_page.ctx--observer_export_btn')
        self.assertIn('Export failed: SD card not mounted', self.text())
        self.assertEqual(self.exports('grove'), [])
        self.click_binding('show_observer_page.back_btn')
        self.assertIn('Network\nObserver', self.text())
        self.observer('MBUS')
        click_text(self.page, 'Stop')
        self.click_binding('show_observer_page.ctx--observer_export_btn')
        self.assertEqual([f['path'] for f in self.exports('mbus')], ['/sdcard/lab/observer/observer_mbus_0001.csv'])
        self.assertEqual(self.exports('grove'), [])
        self.clean()

    def test_options_details_and_running_exit_navigation(self):
        self.load()
        self.observer()
        click_text(self.page, 'Options')
        self.assertIn('Observer Options', self.text())
        self.page.locator('#display').focus()
        self.page.keyboard.press('Escape')
        self.assertNotIn('Observer Options', self.text())
        row = next(obj for obj in objects(self.page) if obj['binding'].endswith('/observe'))
        click_object(self.page, row)
        self.assertIn('SSID:', self.text())
        self.page.locator('#display').focus()
        self.page.keyboard.press('Escape')
        self.assertNotIn('SSID:', self.text())
        self.click_binding('show_observer_page.back_btn')
        self.assertIn('OBSERVER IS RUNNING', self.text())
        click_text(self.page, 'Keep running')
        self.assertTrue(self.page.evaluate("emulator.device.device.observer('grove').running"))
        click_text(self.page, 'Stop')
        self.click_binding('show_observer_page.back_btn')
        self.assertIn('Network\nObserver', self.text())
        self.clean()


if __name__ == '__main__':
    unittest.main(verbosity=2)
