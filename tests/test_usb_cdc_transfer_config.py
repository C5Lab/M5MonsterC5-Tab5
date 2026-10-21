import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETTING = "CONFIG_IN_TRANSFER_BUFFER_SIZE"
CH34X_BULK_MPS = 32


def config_value(path: Path, setting: str):
    match = re.search(
        rf"^{re.escape(setting)}=(\d+)$",
        path.read_text(encoding="utf-8"),
        flags=re.MULTILINE,
    )
    return int(match.group(1)) if match else None


class UsbCdcTransferConfigTest(unittest.TestCase):
    def test_active_config_completes_one_full_ch34x_packet(self):
        self.assertEqual(
            config_value(ROOT / "sdkconfig", SETTING),
            CH34X_BULK_MPS,
        )

    def test_clean_build_keeps_the_ch34x_packet_sized_transfer(self):
        self.assertEqual(
            config_value(ROOT / "sdkconfig.defaults", SETTING),
            CH34X_BULK_MPS,
        )

    def test_driver_exposes_bidirectional_transfer_diagnostics(self):
        header = (
            ROOT
            / "managed_components/espressif__iot_usbh_cdc/include/iot_usbh_cdc.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "managed_components/espressif__iot_usbh_cdc/iot_usbh_cdc.c"
        ).read_text(encoding="utf-8")

        self.assertIn("usbh_cdc_debug_stats_t", header)
        self.assertIn("usbh_cdc_get_debug_stats", header)
        for field in (
            "rx_buffered",
            "tx_buffered",
            "rx_completed",
            "tx_completed",
            "rx_bytes",
            "tx_bytes",
            "rx_errors",
            "tx_errors",
            "rx_submit_errors",
            "tx_submit_errors",
            "last_rx_status",
            "last_tx_status",
            "last_rx_submit_error",
            "last_tx_submit_error",
        ):
            self.assertIn(field, header)
        self.assertIn("usbh_cdc_get_debug_stats", source)

    def test_application_keeps_driver_warnings_and_logs_timeout_snapshots(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        self.assertIn('esp_log_level_set("USBH_CDC", ESP_LOG_WARN)', source)
        self.assertIn('usb_log_cdc_state("ack_timeout")', source)
        self.assertIn("usbh_cdc_get_debug_stats", source)
        self.assertRegex(source, r"#define HS_CRACK_REMOTE_LINE_BYTES\s+640")


if __name__ == "__main__":
    unittest.main()
