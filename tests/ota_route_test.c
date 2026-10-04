#include "ota_rf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ota_info_report_t r;
    ota_info_report_reset(&r);
    r.have_offset = true; r.table_offset = 0x8000;
    r.layout = OTA_RF_LAYOUT_INCOMPATIBLE;
    /* Manual variant, Force and stale Sub-GHz detection cannot override this. */
    assert(ota_install_route_allowed(&r, false, false));
    strcpy(r.running, "ota_0");
    r.slots[0].present = r.slots[0].have_ver = true;
    strcpy(r.slots[0].label, "ota_0"); strcpy(r.slots[0].ver, "1.6.9");
    assert(!strcmp(ota_effective_running_version("1.7.4", &r), "1.7.4"));
    assert(!strcmp(ota_effective_running_version("unknown", &r), "1.6.9"));
    assert(!ota_install_route_allowed(&r, true, false));
    assert(!ota_install_route_allowed(&r, true, true));
    r.layout = OTA_RF_LAYOUT_UNKNOWN;
    assert(!ota_install_route_allowed(&r, true, true));
    r.table_offset = 0x10000; r.layout = OTA_RF_LAYOUT_COMPATIBLE;
    r.has_rf_source = true;
    assert(ota_install_route_allowed(&r, true, false));
    assert(!ota_install_route_allowed(&r, false, false));
    assert(!ota_install_route_allowed(&r, false, true));
    /* Older single-source RF builds omit layout/source but are Sub-GHz proven. */
    ota_info_report_reset(&r);
    assert(!ota_install_route_allowed(&r, true, true));
    assert(!ota_install_route_allowed(&r, true, false));
    assert(!ota_install_route_allowed(&r, false, true));
    assert(!ota_install_route_allowed(&r, false, false));
    ota_info_report_feed_line(&r, "OTA running: ota_0 state=2");
    ota_info_report_feed_line(&r, "APP[0]: ota_0 state=2 ver=1.6.9");
    assert(ota_install_route_allowed(&r, true, true));
    assert(ota_install_route_allowed(&r, false, false));
    ota_release_t release;
    assert(!ota_release_parse_line("OTA[0]: v1.7.5-abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz (main) date title", &release));
    /* Command tags are release identifiers, not image metadata or CLI input. */
    char command[96];
    assert(ota_classic_build_check_cmd(command, sizeof(command), "v1.7.4"));
    assert(!strcmp(command, "ota_check v1.7.4"));
    assert(ota_classic_build_check_cmd(command, sizeof(command), "dev-build.123"));
    assert(!ota_classic_build_check_cmd(command, sizeof(command), "v1.7.4\r\nota_check rf"));
    assert(!ota_rf_build_check_tag_cmd(command, sizeof(command), "v1.7.5 rf"));
    assert(!ota_classic_build_check_cmd(command, sizeof(command), "RF"));
    assert(!ota_release_tag_valid("Latest"));
    puts("ota route: cross-variant gates and exact release tags passed");
}
