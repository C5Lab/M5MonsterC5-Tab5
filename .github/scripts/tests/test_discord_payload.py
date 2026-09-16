import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from discord_payload import build_payload, clean_description


class DiscordPayloadTests(unittest.TestCase):
    release = json.loads((Path(__file__).parent / 'fixtures/release.json').read_text(encoding='utf-8'))
    url = release['html_url']
    flasher = 'https://C5Lab.github.io/M5MonsterC5-Tab5/'

    def payload(self, body):
        result = build_payload('1.5.3', self.url, self.flasher, body)
        content = result['content']
        self.assertLessEqual(len(content.encode('utf-16-le')) // 2, 2000)
        self.assertEqual(content.count(self.url), 1)
        self.assertEqual(content.count(self.flasher), 1)
        self.assertNotIn('/releases/download/', content)
        self.assertEqual(result['allowed_mentions'], {'parse': []})
        self.assertTrue(content.startswith('M5MonsterC5-Tab5 1.5.3\n'))
        self.assertEqual(json.loads(json.dumps(result, ensure_ascii=False)), result)
        return content

    def test_real_release(self):
        content = self.payload(self.release['body'])
        self.assertIn('Summary\nM5Monster Tab5 1.5.3 release with ESPShark', content)
        self.assertIn('Key changes', content)
        self.assertNotIn('Release v1.5.3 published.', content)
        self.assertTrue(content.endswith('... More in the release.'))

    def test_empty(self):
        for body in ('', ' \r\n\t'):
            self.assertEqual(self.payload(body), f'M5MonsterC5-Tab5 1.5.3\nRelease page: {self.url}\nWeb flasher: {self.flasher}')

    def test_unicode(self):
        body = 'Summary\n' + 'Zażółć gęślą jaźń 🐉 中文 e\u0301 👩‍💻\n' * 500
        content = self.payload(body)
        description = content.split('\n\n', 1)[1]
        self.assertTrue(body.startswith(description.removesuffix('\n... More in the release.')))
        self.assertNotIn('\ufffd', content)
        self.assertTrue(content.endswith('... More in the release.'))

    def test_repeated_blocks_and_markdown_links(self):
        body = self.release['body'].split('1.5.3\n\nSummary')[0]
        body = body * 2 + f'Summary\nFix Unicode. See [release]({self.url}) and <{self.flasher}>.\n- [BIN](https://github.com/C5Lab/M5MonsterC5-Tab5/releases/download/v1.5.3/a.bin)\nKeep [documentation](https://example.com/docs).'
        content = self.payload(body)
        self.assertIn('Summary\nFix Unicode.', content)
        self.assertIn('[documentation](https://example.com/docs)', content)
        self.assertNotIn('[BIN]', content)

    def test_preserves_changes_mentioning_files(self):
        body = 'Summary\n- Updated bootloader.bin and firmware.zip.\n- Fixed downloads.'
        self.assertEqual(clean_description(body, self.url, self.flasher), body)


if __name__ == '__main__':
    unittest.main()
