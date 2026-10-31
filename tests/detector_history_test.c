#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* MODEL */

static deauth_entry_t deauth(const char *bssid, const char *ssid,
                             int channel, int rssi)
{
    deauth_entry_t entry = {0};
    snprintf(entry.bssid, sizeof(entry.bssid), "%s", bssid);
    snprintf(entry.ap_name, sizeof(entry.ap_name), "%s", ssid);
    entry.channel = channel;
    entry.rssi = rssi;
    return entry;
}

static deauth_entry_t csa(const char *bssid, int rx, int to,
                          const char *tx, const char *via,
                          int announced_count, int mode, int operating_class)
{
    deauth_entry_t entry = deauth(bssid, "Lab (east)", rx, -60);
    entry.is_csa = true;
    entry.target_channel = to;
    snprintf(entry.claimed_tx, sizeof(entry.claimed_tx), "%s", tx);
    snprintf(entry.csa_via, sizeof(entry.csa_via), "%s", via);
    entry.csa_count = announced_count;
    entry.csa_mode = mode;
    entry.csa_class = operating_class;
    return entry;
}

static deauth_entry_t deauth_v2(const char *bssid, const char *tx,
                                const char *da, int reason)
{
    deauth_entry_t entry = deauth(bssid, "Lab (east)", 6, -42);
    entry.is_deauth_v2 = true;
    snprintf(entry.claimed_tx, sizeof(entry.claimed_tx), "%s", tx);
    snprintf(entry.frame_da, sizeof(entry.frame_da), "%s", da);
    entry.reason_code = reason;
    entry.reason_known = true;
    return entry;
}

static deauth_entry_t deauth_v2_unknown_reason(const char *bssid, const char *tx,
                                                const char *da)
{
    deauth_entry_t entry = deauth_v2(bssid, tx, da, 0);
    entry.reason_known = false;
    return entry;
}

static void test_deauth_counts_and_latest_fields(void)
{
    deauth_entry_t rows[4] = {0};
    int count = 0;
    deauth_entry_t a = deauth("00:11:22:33:44:55", "Lab", 1, -70);
    deauth_entry_t b = deauth("AA:BB:CC:DD:EE:FF", "Guest", 6, -80);
    assert(detector_history_add(rows, &count, 4, &a, 1700000000, 10));
    assert(count == 1 && rows[0].occurrences == 1);
    assert(rows[0].first_seen_epoch_s == 1700000000);
    assert(rows[0].last_seen_epoch_s == 1700000000);
    assert(rows[0].first_seen_uptime_s == 10);
    assert(rows[0].last_seen_uptime_s == 10);

    assert(detector_history_add(rows, &count, 4, &b, 1700000005, 15));
    assert(count == 2 && !strcmp(rows[0].bssid, b.bssid));

    a = deauth("00:11:22:33:44:55", "Lab (renamed)", 11, -42);
    assert(detector_history_add(rows, &count, 4, &a, 1700000020, 30));
    assert(count == 2);
    assert(!strcmp(rows[0].bssid, a.bssid));
    assert(rows[0].occurrences == 2);
    assert(rows[0].channel == 11 && rows[0].rssi == -42);
    assert(!strcmp(rows[0].ap_name, "Lab (renamed)"));
    assert(rows[0].first_seen_epoch_s == 1700000000);
    assert(rows[0].last_seen_epoch_s == 1700000020);
    assert(rows[0].first_seen_uptime_s == 10);
    assert(rows[0].last_seen_uptime_s == 30);
    assert(!strcmp(rows[1].bssid, b.bssid) && rows[1].occurrences == 1);
}

static void test_csa_fingerprint_and_count_field(void)
{
    deauth_entry_t rows[8] = {0};
    int count = 0;
    const char *ap = "00:11:22:33:44:55";
    const char *tx = "AA:BB:CC:DD:EE:FF";
    deauth_entry_t first = csa(ap, 2, 1, tx, "beacon", 0, 1, 81);
    deauth_entry_t repeat = csa(ap, 2, 1, tx, "beacon", 3, 1, 81);
    repeat.rssi = -39;
    assert(detector_history_add(rows, &count, 8, &first, 1700000100, 100));
    assert(detector_history_add(rows, &count, 8, &repeat, 1700000103, 103));
    assert(count == 1 && rows[0].occurrences == 2);
    assert(rows[0].csa_count == 3 && rows[0].rssi == -39);
    assert(rows[0].first_seen_epoch_s == 1700000100);
    assert(rows[0].last_seen_epoch_s == 1700000103);

    deauth_entry_t other_to = csa(ap, 2, 11, tx, "beacon", 1, 1, 81);
    assert(detector_history_add(rows, &count, 8, &other_to, 1700000104, 104));
    assert(count == 2 && rows[0].target_channel == 11);

    deauth_entry_t other_via = csa(ap, 2, 1, tx, "action", 1, 1, 81);
    assert(detector_history_add(rows, &count, 8, &other_via, 1700000105, 105));
    assert(count == 3 && !strcmp(rows[0].csa_via, "action"));

    deauth_entry_t other_tx = csa(ap, 2, 1, ap, "beacon", 1, 1, 81);
    assert(detector_history_add(rows, &count, 8, &other_tx, 1700000106, 106));
    assert(count == 4 && !strcmp(rows[0].claimed_tx, ap));

    deauth_entry_t other_mode = csa(ap, 2, 1, tx, "beacon", 1, 0, 81);
    assert(detector_history_add(rows, &count, 8, &other_mode, 1700000107, 107));
    assert(count == 5 && rows[0].csa_mode == 0);

    deauth_entry_t other_class = csa(ap, 2, 1, tx, "beacon", 1, 1, 82);
    assert(detector_history_add(rows, &count, 8, &other_class, 1700000108, 108));
    assert(count == 6 && rows[0].csa_class == 82);

    deauth_entry_t deauth_event = deauth(ap, "Lab (east)", 2, -37);
    assert(detector_history_add(rows, &count, 8, &deauth_event, 1700000109, 109));
    assert(count == 7 && !rows[0].is_csa);
    assert(rows[0].occurrences == 1);
}

static void test_capacity_evicts_oldest_group(void)
{
    deauth_entry_t rows[3] = {0};
    int count = 0;
    deauth_entry_t a = deauth("00:00:00:00:00:01", "A", 1, -50);
    deauth_entry_t b = deauth("00:00:00:00:00:02", "B", 2, -51);
    deauth_entry_t c = deauth("00:00:00:00:00:03", "C", 3, -52);
    snprintf(rows[2].ap_name, sizeof(rows[2].ap_name), "guard");
    assert(detector_history_add(rows, &count, 2, &a, 1700000001, 1));
    assert(detector_history_add(rows, &count, 2, &b, 1700000002, 2));
    assert(detector_history_add(rows, &count, 2, &c, 1700000003, 3));
    assert(count == 2);
    assert(!strcmp(rows[0].bssid, c.bssid));
    assert(!strcmp(rows[1].bssid, b.bssid));
    assert(!strcmp(rows[2].ap_name, "guard"));

    assert(detector_history_add(rows, &count, 2, &b, 1700000004, 4));
    assert(count == 2 && !strcmp(rows[0].bssid, b.bssid));
    assert(rows[0].occurrences == 2);
    assert(rows[0].first_seen_uptime_s == 2 && rows[0].last_seen_uptime_s == 4);

    assert(detector_history_add(rows, &count, 2, &a, 1700000005, 5));
    assert(count == 2 && !strcmp(rows[0].bssid, a.bssid));
    assert(rows[0].occurrences == 1); /* An evicted group starts afresh. */
    assert(!strcmp(rows[1].bssid, b.bssid));
    assert(!strcmp(rows[2].ap_name, "guard"));
}

static void test_missing_clock_keeps_monotonic_detection_time(void)
{
    deauth_entry_t rows[2] = {0};
    int count = 0;
    deauth_entry_t event = deauth("10:20:30:40:50:60", "No RTC", 1, -60);
    assert(detector_history_add(rows, &count, 2, &event, 0, 40));
    assert(detector_history_add(rows, &count, 2, &event, 0, 45));
    assert(count == 1 && rows[0].occurrences == 2);
    assert(rows[0].first_seen_epoch_s == 0 && rows[0].last_seen_epoch_s == 0);
    assert(rows[0].first_seen_uptime_s == 40 && rows[0].last_seen_uptime_s == 45);
}

static void test_mac_case_does_not_split_groups(void)
{
    deauth_entry_t rows[4] = {0};
    int count = 0;
    deauth_entry_t upper = deauth("AA:BB:CC:DD:EE:FF", "Lab", 2, -55);
    deauth_entry_t lower = deauth("aa:bb:cc:dd:ee:ff", "Lab", 2, -54);
    assert(detector_history_add(rows, &count, 4, &upper, 1700000200, 200));
    assert(detector_history_add(rows, &count, 4, &lower, 1700000201, 201));
    assert(count == 1 && rows[0].occurrences == 2);

    deauth_entry_t csa_upper = csa("AA:BB:CC:DD:EE:FF", 2, 1,
                                   "10:20:30:AA:BB:CC", "beacon", 0, 1, 81);
    deauth_entry_t csa_lower = csa("aa:bb:cc:dd:ee:ff", 2, 1,
                                   "10:20:30:aa:bb:cc", "beacon", 1, 1, 81);
    assert(detector_history_add(rows, &count, 4, &csa_upper, 1700000202, 202));
    assert(detector_history_add(rows, &count, 4, &csa_lower, 1700000203, 203));
    assert(count == 2 && rows[0].is_csa && rows[0].occurrences == 2);
}

static void test_v2_groups_keep_declared_source_destination_and_reason(void)
{
    deauth_entry_t rows[8] = {0};
    int count = 0;
    const char *bssid = "00:11:22:33:44:55";
    deauth_entry_t first = deauth_v2(bssid, "AA:BB:CC:DD:EE:FF",
                                     "11:22:33:44:55:66", 7);
    deauth_entry_t repeat = deauth_v2(bssid, "aa:bb:cc:dd:ee:ff",
                                      "11:22:33:44:55:66", 7);
    assert(detector_history_add(rows, &count, 8, &first, 1700000100, 100));
    assert(detector_history_add(rows, &count, 8, &repeat, 1700000101, 101));
    assert(count == 1 && rows[0].occurrences == 2);
    deauth_entry_t other_sender = deauth_v2(bssid, "AA:BB:CC:DD:EE:00",
                                            "11:22:33:44:55:66", 7);
    deauth_entry_t other_destination = deauth_v2(bssid, "AA:BB:CC:DD:EE:FF",
                                                 "FF:FF:FF:FF:FF:FF", 7);
    deauth_entry_t other_reason = deauth_v2(bssid, "AA:BB:CC:DD:EE:FF",
                                            "11:22:33:44:55:66", 8);
    assert(detector_history_add(rows, &count, 8, &other_sender, 1700000102, 102));
    assert(detector_history_add(rows, &count, 8, &other_destination, 1700000103, 103));
    assert(detector_history_add(rows, &count, 8, &other_reason, 1700000104, 104));
    assert(count == 4);
    deauth_entry_t unknown_reason = deauth_v2_unknown_reason(
        bssid, "AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66");
    assert(detector_history_add(rows, &count, 8, &unknown_reason, 1700000105, 105));
    assert(detector_history_add(rows, &count, 8, &unknown_reason, 1700000106, 106));
    assert(count == 5 && !rows[0].reason_known && rows[0].occurrences == 2);
    deauth_entry_t legacy = deauth(bssid, "Lab (east)", 6, -42);
    assert(detector_history_add(rows, &count, 8, &legacy, 1700000107, 107));
    assert(count == 6 && !rows[0].is_deauth_v2 && !rows[0].is_csa);
}

static void read_csv(FILE *file, char *out, size_t out_size)
{
    assert(file && out && out_size > 1);
    assert(fflush(file) == 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    size_t bytes = fread(out, 1, out_size - 1, file);
    assert(!ferror(file));
    out[bytes] = '\0';
    assert(fclose(file) == 0);
}

static void local_timestamp(int64_t epoch_s, char out[32])
{
    time_t epoch = (time_t)epoch_s;
    struct tm local;
    assert(localtime_r(&epoch, &local));
    assert(strftime(out, 32, "%Y-%m-%d %H:%M:%S", &local) > 0);
}

static void test_csv_has_escaped_identity_and_both_event_types(void)
{
    const char *header = "type,ssid,bssid,rx_channel,to_channel,claimed_tx,"
                         "rssi_dbm,via,janos_csa_count,mode,operating_class,"
                         "observations,first_seen_local,last_seen_local,"
                         "first_seen_uptime_s,last_seen_uptime_s,frame_da,reason_code,reason_status";
    deauth_entry_t rows[4] = {0};
    int count = 0;
    deauth_entry_t de = deauth("00:11:22:33:44:55", "Lab, \"east\"", 2, -45);
    deauth_entry_t announcement = csa("00:11:22:33:44:55", 2, 1,
                                      "AA:BB:CC:DD:EE:FF", "beacon", 0, 1, 81);
    snprintf(announcement.ap_name, sizeof(announcement.ap_name), "Lab, \"east\"");
    assert(detector_history_add(rows, &count, 4, &de, 1700000000, 10));
    assert(detector_history_add(rows, &count, 4, &de, 1700000001, 11));
    assert(detector_history_add(rows, &count, 4, &announcement, 1700000010, 20));
    announcement.csa_count = 3;
    assert(detector_history_add(rows, &count, 4, &announcement, 1700000011, 21));
    assert(count == 2);

    FILE *file = tmpfile();
    assert(file);
    assert(detector_history_write_csv(file, rows, count));
    char csv[4096];
    read_csv(file, csv, sizeof(csv));
    assert(strncmp(csv, header, strlen(header)) == 0);
    char *csa_row = strchr(csv, '\n');
    assert(csa_row);
    csa_row++;
    char *deauth_row = strchr(csa_row, '\n');
    assert(deauth_row);
    *deauth_row++ = '\0';
    char *end = strchr(deauth_row, '\n');
    assert(end);
    *end = '\0';
    const char *csa_prefix = "CSA,\"Lab, \"\"east\"\"\",00:11:22:33:44:55,2,1,"
                             "AA:BB:CC:DD:EE:FF,-60,beacon,3,1,81,2,";
    assert(strncmp(csa_row, csa_prefix, strlen(csa_prefix)) == 0);
    assert(strncmp(deauth_row, "DEAUTH,\"Lab, \"\"east\"\"\",00:11:22:33:44:55,2,",
                   strlen("DEAUTH,\"Lab, \"\"east\"\"\",00:11:22:33:44:55,2,")) == 0);
    char first[32], last[32], expected[128];
    local_timestamp(1700000010, first);
    local_timestamp(1700000011, last);
    snprintf(expected, sizeof(expected), ",2,%s,%s,20,21", first, last);
    assert(strstr(csa_row, expected));
    local_timestamp(1700000000, first);
    local_timestamp(1700000001, last);
    snprintf(expected, sizeof(expected), ",2,%s,%s,10,11", first, last);
    assert(strstr(deauth_row, expected));
    assert(end[1] == '\0');
}

static void test_csv_missing_clock_leaves_wall_time_blank(void)
{
    deauth_entry_t rows[1] = {0};
    int count = 0;
    deauth_entry_t de = deauth("10:20:30:40:50:60", "No RTC", 1, -60);
    assert(detector_history_add(rows, &count, 1, &de, 0, 40));
    assert(detector_history_add(rows, &count, 1, &de, 0, 45));
    FILE *file = tmpfile();
    assert(file);
    assert(detector_history_write_csv(file, rows, count));
    char csv[1024];
    read_csv(file, csv, sizeof(csv));
    assert(strstr(csv, ",2,,,40,45"));
}

static void test_csv_preserves_v2_claimed_addresses_and_reason(void)
{
    deauth_entry_t rows[4] = {0};
    int count = 0;
    deauth_entry_t v2 = deauth_v2("00:11:22:33:44:55", "AA:BB:CC:DD:EE:FF",
                                   "FF:FF:FF:FF:FF:FF", 65535);
    deauth_entry_t legacy = deauth("00:11:22:33:44:55", "Lab", 6, -50);
    deauth_entry_t unknown = deauth_v2_unknown_reason(
        "00:11:22:33:44:55", "AA:BB:CC:DD:EE:00", "11:22:33:44:55:66");
    assert(detector_history_add(rows, &count, 4, &v2, 1700000100, 100));
    assert(detector_history_add(rows, &count, 4, &unknown, 1700000101, 101));
    assert(detector_history_add(rows, &count, 4, &legacy, 1700000102, 102));
    assert(count == 3);
    FILE *file = tmpfile();
    assert(file);
    assert(detector_history_write_csv(file, rows, count));
    char csv[2048];
    read_csv(file, csv, sizeof(csv));
    assert(strstr(csv, "AA:BB:CC:DD:EE:FF,-42"));
    assert(strstr(csv, ",100,100,FF:FF:FF:FF:FF:FF,65535,known\n"));
    assert(strstr(csv, ",101,101,11:22:33:44:55:66,,unknown_encrypted\n"));
    assert(strstr(csv, ",102,102,,,\n"));
}

int main(void)
{
    test_deauth_counts_and_latest_fields();
    test_csa_fingerprint_and_count_field();
    test_capacity_evicts_oldest_group();
    test_missing_clock_keeps_monotonic_detection_time();
    test_mac_case_does_not_split_groups();
    test_v2_groups_keep_declared_source_destination_and_reason();
    test_csv_has_escaped_identity_and_both_event_types();
    test_csv_missing_clock_leaves_wall_time_blank();
    test_csv_preserves_v2_claimed_addresses_and_reason();
    puts("detector history tests passed");
    return 0;
}
