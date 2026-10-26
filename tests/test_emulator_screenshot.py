"""Status-bar screenshot saves the virtual display as a browser PNG."""
import struct
import unittest

from emulator_browser import browser_session, click_text


TITLE = '#FF2FA3 LAB5# | control the chaos'


class ScreenshotBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1000},
                                                accept_downloads=True)
        self.page = self.context.new_page()
        self.errors = []
        self.downloads = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.on('download', lambda download: self.downloads.append(download.suggested_filename))
        self.page.add_init_script('''
            globalThis.screenshotUnavailable = [];
            addEventListener('emulator-unavailable', event => screenshotUnavailable.push(event.detail));
        ''')
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=30000)

    def tearDown(self):
        self.context.close()

    def test_logo_downloads_display_png_without_sd_claim(self):
        with self.page.expect_download(timeout=5000) as pending:
            click_text(self.page, TITLE)
        download = pending.value
        self.assertTrue(download.suggested_filename.endswith('.png'))
        data = download.path().read_bytes()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack('>II', data[16:24]), (720, 1280))
        self.assertEqual(self.downloads, [download.suggested_filename])
        self.assertIn('browser download', self.page.locator('#notice').inner_text().lower())
        self.assertEqual(self.page.evaluate('screenshotUnavailable'), [])
        self.assertEqual(self.errors, [])

    def test_missing_download_api_is_explained_without_unavailable_event(self):
        self.page.evaluate("document.querySelector('#display').toBlob = undefined")
        click_text(self.page, TITLE)
        self.assertIn('screenshot download is unavailable',
                      self.page.locator('#notice').inner_text().lower())
        self.assertEqual(self.page.evaluate('screenshotUnavailable'), [])
        self.assertEqual(self.downloads, [])
        self.assertEqual(self.errors, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
