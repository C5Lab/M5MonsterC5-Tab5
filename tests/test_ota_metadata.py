"""Contract: parsing JanOS `ota_info` metadata on the Tab5 side.

Pins that the Tab5 reads the ota_info output for what it actually proves:
  - the "OTA RF source:" line proves the updater understands the rf commands,
    NOT that the physical board is an RF board (a classic build with the new
    updater prints it too);
  - RF partition-layout compatibility comes only from the explicit
    "OTA RF layout:" line - absent means UNKNOWN, never compatible;
  - the classic 0x8000 table is not RF-compatible; RF requires 0x10000.

Exercises the real parser in main/ota_rf.c.

    python3 tests/test_ota_metadata.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _ota_rf_harness import HARNESS_PRELUDE, assert_ok, run_harness  # noqa: E402


HARNESS = HARNESS_PRELUDE + r"""
int main(void) {
    // --- classic build carrying the new updater -------------------------
    {
        const char *lines[] = {
            "OTA default source: classic (C5Lab/projectZero), channel=main",
            "OTA RF source: elpadrino26/janosrf-web-flasher (one-shot: ota_check rf <tag>)",
            "OTA partition table offset: 0x8000",
            "OTA RF layout: incompatible",
            "OTA boot: ota_0",
            "OTA running: ota_0 state=1",
            "OTA next: ota_1",
            "APP[0]: ota_0 state=1 ver=1.7.5",
            "APP[1]: ota_1 state=0 ver=1.7.4",
        };
        ota_info_report_t r;
        feed_lines(&r, lines, (int)(sizeof(lines)/sizeof(lines[0])));
        EXPECT(r.has_rf_source);
        EXPECT(ota_info_supports_rf_commands(&r));          // capability proven
        EXPECT(r.have_offset && r.table_offset == 0x8000);
        EXPECT(r.layout == OTA_RF_LAYOUT_INCOMPATIBLE);     // classic table != RF
        // RF-repo line must NOT be read as RF hardware; auto must not pick RF.
        EXPECT(!ota_rf_auto_should_use_rf(&r, /*subghz_seen=*/false));
        EXPECT(strcmp(r.running, "ota_0") == 0 && r.running_state == 1);
        EXPECT(r.slots[0].have_ver && strcmp(r.slots[0].ver, "1.7.5") == 0);
        EXPECT(strcmp(r.boot, "ota_0") == 0 && strcmp(r.next, "ota_1") == 0);
    }

    // --- genuine RF layout ------------------------------------------------
    {
        const char *lines[] = {
            "OTA default source: classic (C5Lab/projectZero), channel=dev",
            "OTA RF source: elpadrino26/janosrf-web-flasher (one-shot: ota_check rf <tag>)",
            "OTA partition table offset: 0x10000",
            "OTA RF layout: compatible (release files still require verification)",
            "OTA running: ota_1 state=2",
        };
        ota_info_report_t r;
        feed_lines(&r, lines, (int)(sizeof(lines)/sizeof(lines[0])));
        EXPECT(ota_info_supports_rf_commands(&r));
        EXPECT(r.have_offset && r.table_offset == 0x10000);
        EXPECT(r.layout == OTA_RF_LAYOUT_COMPATIBLE);
        EXPECT(ota_rf_auto_should_use_rf(&r, false));       // capable + compatible
    }

    // --- old updater: no RF lines at all ----------------------------------
    {
        const char *lines[] = {
            "OTA boot: ota_0",
            "OTA running: ota_0 state=1",
            "OTA next: ota_1",
            "APP[0]: ota_0 state=1 ver=1.6.8",
            "APP[1]: ota_1 missing",
        };
        ota_info_report_t r;
        feed_lines(&r, lines, (int)(sizeof(lines)/sizeof(lines[0])));
        EXPECT(!r.has_rf_source);
        EXPECT(!ota_info_supports_rf_commands(&r));         // capability unproven
        EXPECT(r.layout == OTA_RF_LAYOUT_UNKNOWN);          // never assume compatible
        EXPECT(!r.slots[1].present);                         // "missing" slot
        EXPECT(!ota_rf_auto_should_use_rf(&r, false));
        // A live SUB-GHz reply proves RF firmware, so it routes RF on its own
        // even though ota_info here shows no RF lines (the reported field case).
        EXPECT(ota_rf_auto_should_use_rf(&r, true));
    }

    // --- foreign/echo/prompt noise interleaved with ota_info --------------
    {
        const char *lines[] = {
            "janos> ota_info",                               // echoed prompt+command
            "I (5123) ota: dumping",                          // foreign log line
            "OTA RF source: elpadrino26/janosrf-web-flasher", // still captured
            "OTA RF layout: compatible",
            "some unrelated trailing text",
        };
        ota_info_report_t r;
        feed_lines(&r, lines, (int)(sizeof(lines)/sizeof(lines[0])));
        EXPECT(ota_info_supports_rf_commands(&r));
        EXPECT(r.layout == OTA_RF_LAYOUT_COMPATIBLE);
    }

    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
"""


def test_ota_metadata():
    result = run_harness(HARNESS)
    assert_ok(result)
    print(result.stdout.strip())


if __name__ == "__main__":
    test_ota_metadata()
    print("test_ota_metadata: OK")
