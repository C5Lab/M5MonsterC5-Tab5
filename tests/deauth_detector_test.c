#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* MODEL */
/* PARSER */

int main(void)
{
    deauth_entry_t entry = {0};
    const char *v2_record =
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: FF:FF:FF:FF:FF:FF | RSSI: -61 | "
        "REASON: 7 | SSID: Lab (east)";
    char prefixed_v2[512];
    assert(!parse_deauth_line(
        "Output: [DEAUTH] CH: <ch> | AP: <name> (<bssid>) | RSSI: <rssi>", &entry));
    assert(!parse_deauth_line("[DETECTOR_INFO] CSA/ECSA announcements enabled.", &entry));
    assert(!parse_deauth_line(
        "[DETECTOR_WATCH] count=1 ap=1/1 CH=6 BSSID=00:11:22:33:44:55 SSID=Lab", &entry));
    assert(parse_deauth_line(
        "I JanOS: [DEAUTH] CH: 6 | AP: Lab (east) (00:11:22:33:44:55) | RSSI: -61", &entry));
    assert(!entry.is_csa && entry.channel == 6 && entry.rssi == -61);
    assert(!strcmp(entry.ap_name, "Lab (east)"));
    assert(!strcmp(entry.bssid, "00:11:22:33:44:55"));
    assert(!parse_deauth_line(
        "[DEAUTH] CH: 6 | AP: Lab (east) (bad) | RSSI: -61", &entry));
    assert(!parse_deauth_line(
        "[DEAUTH] CH: 6 | AP: Lab (east) (00:11:22:33:44:55) | RSSI: -61 | TX: AA:BB:CC:DD:EE:FF",
        &entry));

    assert(parse_deauth_v2_line(v2_record, &entry));
    assert(snprintf(prefixed_v2, sizeof(prefixed_v2), "> %s", v2_record) > 0);
    assert(parse_deauth_v2_line(prefixed_v2, &entry));
    assert(snprintf(prefixed_v2, sizeof(prefixed_v2),
                    "I JanOS: %s", v2_record) > 0);
    assert(parse_deauth_v2_line(prefixed_v2, &entry));
    assert(entry.is_deauth_v2 && !entry.is_csa);
    assert(entry.channel == 6 && entry.rssi == -61 && entry.reason_code == 7 &&
           entry.reason_known);
    assert(!strcmp(entry.ap_name, "Lab (east)"));
    assert(!strcmp(entry.bssid, "00:11:22:33:44:55"));
    assert(!strcmp(entry.claimed_tx, "AA:BB:CC:DD:EE:FF"));
    assert(!strcmp(entry.frame_da, "FF:FF:FF:FF:FF:FF"));
    assert(parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: 11:22:33:44:55:66 | RSSI: -61 | "
        "REASON: - | SSID: Protected unicast", &entry));
    assert(entry.is_deauth_v2 && !entry.reason_known && entry.reason_code == 0);
    assert(!strcmp(entry.ap_name, "Protected unicast"));
    assert(!parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: 11:22:33:44:55:66 | RSSI: -61 | "
        "REASON: -- | SSID: Invalid", &entry));
    assert(snprintf(prefixed_v2, sizeof(prefixed_v2),
                    "[DETECTOR_INFO] Echo: %s", v2_record) > 0);
    assert(!parse_deauth_v2_line(prefixed_v2, &entry));
    assert(snprintf(prefixed_v2, sizeof(prefixed_v2),
                    "arbitrary preamble %s", v2_record) > 0);
    assert(!parse_deauth_v2_line(prefixed_v2, &entry));
    assert(!parse_deauth_v2_line(
        "Output: [DEAUTH_V2] CH: <ch> | BSSID: <bssid> | TX: <tx> | "
        "DA: <da> | RSSI: <rssi> | REASON: <reason> | SSID: <name>", &entry));
    assert(!parse_deauth_v2_line(
        "[DETECTOR_INFO] format=deauth_v2", &entry));
    assert(!parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: bad | TX: AA:BB:CC:DD:EE:FF | "
        "DA: FF:FF:FF:FF:FF:FF | RSSI: -61 | REASON: 7 | SSID: Lab", &entry));
    assert(!parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: FF:FF:FF:FF:FF:FF | RSSI: -61 | "
        "REASON: 65536 | SSID: Lab", &entry));
    assert(!parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: FF:FF:FF:FF:FF:FF | RSSI: -61 | "
        "REASON: 7 | SSID: ", &entry));
    assert(!parse_deauth_v2_line(
        "[DEAUTH_V2] CH: 6 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | DA: FF:FF:FF:FF:FF:FF | RSSI: -61 | "
        "REASON: 7 | SSID: Lab\x1b[31m", &entry));

    assert(parse_csa_line(
        "[CSA] RX: 6 | TO: 11 | BSSID: 00:11:22:33:44:55 | "
        "TX: AA:BB:CC:DD:EE:FF | RSSI: -67 | VIA: beacon | COUNT: 3 | "
        "MODE: 1 | CLASS: - | SSID: Lab (east)", &entry));
    assert(entry.is_csa && entry.channel == 6 && entry.target_channel == 11);
    assert(entry.rssi == -67 && entry.csa_count == 3 && entry.csa_mode == 1);
    assert(entry.csa_class == -1 && !strcmp(entry.csa_via, "beacon"));
    assert(!strcmp(entry.ap_name, "Lab (east)"));
    assert(!strcmp(entry.bssid, "00:11:22:33:44:55"));
    assert(!strcmp(entry.claimed_tx, "AA:BB:CC:DD:EE:FF"));
    assert(parse_csa_line(
        "> [CSA] RX: 11 | TO: 1 | BSSID: 84:A1:D1:C9:83:E0 | "
        "TX: 84:A1:D1:C9:83:E0 | RSSI: -87 | VIA: probe | COUNT: 3 | "
        "MODE: 0 | CLASS: 4 | SSID: Orange_Swiatlowod_83E0", &entry));
    assert(entry.is_csa && entry.channel == 11 && entry.target_channel == 1);
    assert(entry.csa_count == 3 && entry.csa_mode == 0 && entry.csa_class == 4);
    assert(!strcmp(entry.bssid, "84:A1:D1:C9:83:E0"));
    assert(!parse_csa_line("[DETECTOR_INFO] CSA/ECSA announcements enabled.", &entry));
    assert(!parse_csa_line(
        "[CSA] RX: 6 | TO: 11 | BSSID: bad | TX: AA:BB:CC:DD:EE:FF | "
        "RSSI: -67 | VIA: beacon | COUNT: 3 | MODE: 1 | CLASS: - | SSID: Lab", &entry));
    puts("detector event parser tests passed");
    return 0;
}
