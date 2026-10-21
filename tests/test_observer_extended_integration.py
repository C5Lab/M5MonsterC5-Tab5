from pathlib import Path
import unittest
from test_observer_incremental_ui_contract import function_body

SOURCE = (Path(__file__).resolve().parents[1] / 'main/main.c').read_text(encoding='utf-8')

class ExtendedIntegration(unittest.TestCase):
    def test_both_poll_paths_share_bssid_receiver_and_skip_interleaved_logs(self):
        for name in ('popup_poll_task', 'observer_poll_task'):
            body = function_body(SOURCE, 'static void ' + name)
            self.assertIn('observer_receive_ap', body)
            self.assertIn('observer_is_noise_line', body)
            self.assertIn('line_overflow', body)
            self.assertIn('observer_query_command', body)
            self.assertIn('observer_is_prompt_line', body)

    def test_options_apply_does_not_send_radio_commands(self):
        body = function_body(SOURCE, 'static void observer_options_applied')
        self.assertNotIn('transport_write', body)
        self.assertNotIn('uart_send', body)
        self.assertIn('update_observer_table', body)

    def test_export_retains_full_extension_independent_of_visibility(self):
        body = function_body(SOURCE, 'static bool observer_export_csv')
        self.assertIn('net->extended.raw', body)
        self.assertNotIn('observer_view_prefs', body)

if __name__ == '__main__':
    unittest.main()
