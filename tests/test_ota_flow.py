"""Contract: the Monster OTA / JanOS RF operation flow on the Tab5 side.

Pins the behaviour the implementation plan requires (main/ota_rf.c):
  - the three facts stay independent: firmware variant, command support, and
    partition-layout compatibility; none alone proves another;
  - RF listing and install always carry the rf argument and never fall back to
    the classic repo on error;
  - explicit tags parse with or without a leading v and are preserved verbatim;
  - reinstall (same version) and downgrade (older) are surfaced as confirmations,
    never started automatically; a newer tag or "latest" may proceed;
  - an incompatible or unknown layout blocks install with distinct reasons;
  - changing the connected device clears cached detection, and only one OTA
    operation may run at a time;
  - after a reboot, re-reading ota_info re-establishes capability/layout.

Exercises the real logic in main/ota_rf.c.

    python3 tests/test_ota_flow.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _ota_rf_harness import HARNESS_PRELUDE, assert_ok, run_harness  # noqa: E402


HARNESS = HARNESS_PRELUDE + r"""
static ota_info_report_t report(ota_rf_layout_t layout, bool has_rf_source) {
    ota_info_report_t r; ota_info_report_reset(&r);
    r.has_rf_source = has_rf_source; r.layout = layout; return r;
}

int main(void) {
    char cmd[64];

    // --- independence of the three facts ----------------------------------
    // classic firmware + new updater: capability yes, layout incompatible,
    // variant not RF. Capability does not imply compatibility or RF hardware.
    {
        ota_info_report_t r = report(OTA_RF_LAYOUT_INCOMPATIBLE, true);
        EXPECT(ota_info_supports_rf_commands(&r));
        EXPECT(!ota_rf_auto_should_use_rf(&r, false));
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false)
               == OTA_DECIDE_BLOCK_INCOMPATIBLE);
    }
    // RF firmware confirmed live via SUB-GHz, but ota_info shows no RF lines:
    // auto still routes RF (subghz is sufficient), and the override lets the
    // install proceed - the C5 firmware performs the final verification.
    {
        ota_info_report_t r = report(OTA_RF_LAYOUT_UNKNOWN, false);
        EXPECT(ota_rf_auto_should_use_rf(&r, /*subghz_seen=*/true));
        // Auto path treats a subghz-confirmed device as an override.
        EXPECT(ota_rf_decide_install(&r, false, "1.7.5", NULL, false, true)
               == OTA_DECIDE_ALLOW_LATEST);
        // Without subghz and without capability, auto must not route/guess RF.
        EXPECT(!ota_rf_auto_should_use_rf(&r, false));
        EXPECT(ota_rf_decide_install(&r, false, "1.7.5", "1.7.5", false, false)
               == OTA_DECIDE_BLOCK_NO_CAPABILITY);
    }

    // --- manual RF override (field case: RF fw with no RF lines in ota_info) --
    // ota_info shows neither capability nor layout, yet the user knows it is RF.
    // The override proceeds (the C5 firmware verifies), and a force-reinstall of
    // the same version is a confirmation, not a silent install.
    {
        ota_info_report_t r = report(OTA_RF_LAYOUT_UNKNOWN, false);
        EXPECT(ota_rf_decide_install(&r, false, "1.7.5", NULL, false, true)
               == OTA_DECIDE_ALLOW_LATEST);
        EXPECT(ota_rf_decide_install(&r, false, "1.7.5", "1.7.5", false, true)
               == OTA_DECIDE_CONFIRM_REINSTALL);
        // But an incompatible classic layout is never overridable.
        ota_info_report_t bad = report(OTA_RF_LAYOUT_INCOMPATIBLE, true);
        EXPECT(ota_rf_decide_install(&bad, true, "1.7.5", NULL, false, true)
               == OTA_DECIDE_BLOCK_INCOMPATIBLE);
    }

    // --- command routing (rf argument, no classic fallback) ---------------
    EXPECT(ota_rf_build_list_cmd(cmd, sizeof(cmd)) && strcmp(cmd, "ota_list rf") == 0);
    EXPECT(ota_rf_build_check_latest_cmd(cmd, sizeof(cmd), false)
           && strcmp(cmd, "ota_check rf") == 0);
    EXPECT(ota_rf_build_check_latest_cmd(cmd, sizeof(cmd), true)
           && strcmp(cmd, "ota_check rf latest") == 0);
    // classic builders never carry rf; the two paths are distinct.
    EXPECT(ota_classic_build_list_cmd(cmd, sizeof(cmd)) && strcmp(cmd, "ota_list") == 0);
    EXPECT(ota_classic_build_check_cmd(cmd, sizeof(cmd), NULL) && strcmp(cmd, "ota_check") == 0);

    // --- tags with/without v, preserved verbatim --------------------------
    EXPECT(ota_rf_build_check_tag_cmd(cmd, sizeof(cmd), "1.7.5")
           && strcmp(cmd, "ota_check rf 1.7.5") == 0);
    EXPECT(ota_rf_build_check_tag_cmd(cmd, sizeof(cmd), "v1.7.5")
           && strcmp(cmd, "ota_check rf v1.7.5") == 0);

    // --- decision: newer / same / older, latest --------------------------
    {
        ota_info_report_t r = report(OTA_RF_LAYOUT_COMPATIBLE, true);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false) == OTA_DECIDE_ALLOW_LATEST);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", NULL, true, false)  == OTA_DECIDE_ALLOW_LATEST);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", "1.7.6", false, false) == OTA_DECIDE_ALLOW_TAG);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", "1.7.5", false, false) == OTA_DECIDE_CONFIRM_REINSTALL);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", "v1.7.5", false, false) == OTA_DECIDE_CONFIRM_REINSTALL);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", "1.6.8", false, false) == OTA_DECIDE_CONFIRM_DOWNGRADE);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", "dev", false, false) == OTA_DECIDE_BLOCK_BAD_TAG);
    }
    // unknown layout blocks distinctly from incompatible (automatic only)
    {
        ota_info_report_t r = report(OTA_RF_LAYOUT_UNKNOWN, true);
        EXPECT(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false) == OTA_DECIDE_BLOCK_UNKNOWN_LAYOUT);
    }

    // --- device change clears cache; concurrency guard --------------------
    {
        ota_rf_state_t s; ota_rf_state_reset(&s);
        ota_rf_state_bind_device(&s, 0);
        s.updater_supports_rf = true;
        s.layout = OTA_RF_LAYOUT_COMPATIBLE;
        s.variant = OTA_VARIANT_RF;
        ota_rf_state_bind_device(&s, 0);   // same device keeps cache
        EXPECT(s.updater_supports_rf && s.layout == OTA_RF_LAYOUT_COMPATIBLE);
        ota_rf_state_bind_device(&s, 2);   // swap clears detection
        EXPECT(!s.updater_supports_rf && s.layout == OTA_RF_LAYOUT_UNKNOWN
               && s.variant == OTA_VARIANT_UNKNOWN);
        EXPECT(ota_rf_state_begin_op(&s));
        EXPECT(!ota_rf_state_begin_op(&s));   // no parallel operation
        ota_rf_state_end_op(&s);
        EXPECT(ota_rf_state_begin_op(&s));
        ota_rf_state_end_op(&s);
    }

    // --- re-detect after reboot ------------------------------------------
    // Before reboot: RF firmware, compatible. After a fresh ota_info read the
    // report is rebuilt from scratch; stale state must not persist implicitly.
    {
        ota_info_report_t r;
        const char *before[] = {
            "OTA RF source: elpadrino26/janosrf-web-flasher",
            "OTA RF layout: compatible",
        };
        feed_lines(&r, before, 2);
        EXPECT(ota_rf_auto_should_use_rf(&r, true));
        // Reboot into a build whose ota_info no longer reports RF lines.
        const char *after[] = {
            "OTA boot: ota_0",
            "OTA running: ota_0 state=1",
        };
        feed_lines(&r, after, 2);            // reset happens inside feed_lines
        EXPECT(!ota_info_supports_rf_commands(&r));
        EXPECT(r.layout == OTA_RF_LAYOUT_UNKNOWN);
        EXPECT(!ota_rf_auto_should_use_rf(&r, false));
    }

    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
"""


def test_ota_flow():
    result = run_harness(HARNESS)
    assert_ok(result)
    print(result.stdout.strip())


if __name__ == "__main__":
    test_ota_flow()
    print("test_ota_flow: OK")
