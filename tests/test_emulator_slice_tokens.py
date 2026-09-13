"""Regression for character literals swallowing later runtime dependencies."""
import sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ui_emulator'))
from prepare_browser import words

class SliceTokens(unittest.TestCase):
    def test_quote_character_preserves_following_calls(self):
        source = '''if (strchr(ssid, '"')) reject();
        /* ignored_dependency() */
        const char *message = "not_a_dependency()";
        real_dependency();
        '''
        actual=words(source)
        self.assertIn('real_dependency',actual)
        self.assertIn('reject',actual)
        self.assertNotIn('ignored_dependency',actual)
        self.assertNotIn('not_a_dependency',actual)

if __name__=='__main__':unittest.main()
