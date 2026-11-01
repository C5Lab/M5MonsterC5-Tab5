#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* MODEL */
/* LOGIC */

static uint64_t test_reason_count(const detector_forensic_session_t *session,
                                  uint16_t reason)
{
    uint64_t count = 0;
    assert(session && session->rows);
    for (size_t i = 0; i < session->count; ++i) {
        if (session->rows[i].reason_known && session->rows[i].reason == reason)
            ++count;
    }
    return count;
}

static const char *VALID =
    "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 "
    "TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 "
    "SEQ=4095 RETRY=1 PROTECTED=0 FC=0x00C0 TS_US=18446744073709551615";

static detector_forensic_row_t parsed(const char *line)
{
    detector_forensic_row_t row = {0};
    if (!detector_forensic_parse_line(line, &row)) {
        fprintf(stderr, "forensic parse rejected: %s\n", line);
        assert(false);
    }
    return row;
}

static detector_forensic_row_t row_for(const char *frame, const char *bssid,
                                       const char *tx, const char *da,
                                       uint64_t timestamp, int sequence,
                                       int retry, int rssi, int reason,
                                       int protected_value)
{
    char line[512];
    char rssi_text[8], reason_text[8], sequence_text[8];
    snprintf(rssi_text, sizeof(rssi_text), rssi < -127 ? "-" : "%d", rssi);
    snprintf(reason_text, sizeof(reason_text), reason < 0 ? "-" : "%d", reason);
    snprintf(sequence_text, sizeof(sequence_text), sequence < 0 ? "-" : "%d", sequence);
    snprintf(line, sizeof(line),
             "[DEAUTH_FORENSIC/1] FRAME=%s CH=6 BSSID=%s TX=%s DA=%s "
             "RSSI=%s REASON=%s SEQ=%s RETRY=%s PROTECTED=%s FC=0x00C0 TS_US=%llu",
             frame, bssid, tx, da, rssi_text, reason_text, sequence_text,
             retry < 0 ? "-" : retry ? "1" : "0",
             protected_value < 0 ? "-" : protected_value ? "1" : "0",
             (unsigned long long)timestamp);
    return parsed(line);
}

static void test_strict_parser(void)
{
    assert(detector_forensic_is_candidate_line(VALID));
    assert(detector_forensic_is_candidate_line(
        "> [DEAUTH_FORENSIC/1] FRAME=deauth"));
    assert(!detector_forensic_is_candidate_line(
        "prefix [DEAUTH_FORENSIC/1] FRAME=deauth"));
    assert(!detector_forensic_is_candidate_line(
        "> > [DEAUTH_FORENSIC/1] FRAME=deauth"));
    detector_forensic_row_t row = parsed(VALID);
    assert(row.frame == DETECTOR_FORENSIC_DEAUTH && row.channel_known &&
           row.channel == 6 && row.rssi_known && row.rssi == -82 &&
           row.reason_known && row.reason == 6 && row.seq_known &&
           row.seq == 4095 && row.retry_known && row.retry == 1 &&
           row.protected_known && row.protected_value == 0 && row.fc_known &&
           row.fc == 0x00C0 && row.ts_us == UINT64_MAX);
    assert(!strcmp(row.bssid, "00:11:22:33:44:55") &&
           !strcmp(row.tx, "AA:BB:CC:DD:EE:FF") &&
           !strcmp(row.da, "12:34:56:78:9A:BC"));

    row = parsed(
        "> [DEAUTH_FORENSIC/1] FRAME=disassoc CH=- BSSID=00:11:22:33:44:55 "
        "TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=- REASON=- "
        "SEQ=- RETRY=- PROTECTED=- FC=- TS_US=0");
    assert(row.frame == DETECTOR_FORENSIC_DISASSOC && !row.channel_known &&
           !row.rssi_known && !row.reason_known && !row.seq_known &&
           !row.retry_known && !row.protected_known && !row.fc_known &&
           row.ts_us == 0);
    row = parsed(
        "I JanOS: > [DEAUTH_FORENSIC/1] FRAME=deauth CH=233 BSSID=00:11:22:33:44:55 "
        "TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-127 REASON=65535 "
        "SEQ=0 RETRY=0 PROTECTED=1 FC=0xFFFF TS_US=0");
    assert(row.channel_known && row.channel == 233 && row.rssi_known &&
           row.rssi == -127 && row.reason_known && row.reason == UINT16_MAX &&
           row.seq_known && row.seq == 0 && row.fc_known && row.fc == UINT16_MAX);
    row = parsed(
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=006 BSSID=00:11:22:33:44:55 "
        "TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-082 REASON=0006 "
        "SEQ=0001 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=0001");
    assert(row.channel == 6 && row.rssi == -82 && row.reason == 6 &&
           row.seq == 1 && row.ts_us == 1);

    const char *bad[] = {
        "prefix [DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "> > [DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "I JanOS: > > [DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "I JanOS: I JanOS: [DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 TX=AA:BB:CC:DD:EE:FF BSSID=00:11:22:33:44:55 DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1 EXTRA=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:5 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=0 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=234 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-128 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-0 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=1 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=65536 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=4096 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=2 PROTECTED=0 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=2 FC=0x00C0 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x10000 TS_US=1",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=18446744073709551616",
        "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 TX=AA:BB:CC:DD:EE:FF DA=12:34:56:78:9A:BC RSSI=-82 REASON=6 SEQ=1 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        detector_forensic_row_t rejected = {0};
        assert(!detector_forensic_parse_line(bad[i], &rejected));
    }
    size_t valid_length = strlen(VALID);
    const char *timestamp_value = strstr(VALID, "TS_US=") + strlen("TS_US=");
    assert(timestamp_value > VALID && (size_t)(timestamp_value - VALID) < valid_length);
    for (size_t length = 0; length <= (size_t)(timestamp_value - VALID); ++length) {
        char truncated[512];
        memcpy(truncated, VALID, length);
        truncated[length] = '\0';
        detector_forensic_row_t rejected = {0};
        assert(!detector_forensic_parse_line(truncated, &rejected));
    }
    char oversized[600];
    memset(oversized, 'A', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\0';
    assert(!detector_forensic_parse_line(oversized, &row));
}

static void test_bursts_capacity_and_sequences(void)
{
    detector_forensic_row_t storage[DETECTOR_FORENSIC_CAPACITY] = {0};
    detector_forensic_session_t session = {0};
    detector_forensic_reset(&session, storage, DETECTOR_FORENSIC_CAPACITY);
    detector_forensic_row_t row = row_for("deauth", "00:11:22:33:44:55",
        "AA:BB:CC:DD:EE:FF", "12:34:56:78:9A:BC", 0, 4095, 0, -80, 6, 1);
    assert(detector_forensic_accept(&session, &row));
    row.ts_us = 20000000; row.seq = 0;
    assert(detector_forensic_accept(&session, &row));
    assert(row.sequence_state == DETECTOR_FORENSIC_SEQUENCE_DELTA && row.sequence_delta == 1);
    row.ts_us = 40000001; row.retry = 1;
    assert(detector_forensic_accept(&session, &row));
    assert(row.sequence_state == DETECTOR_FORENSIC_RETRANSMISSION);
    row.ts_us++; row.retry = 0;
    assert(detector_forensic_accept(&session, &row));
    assert(row.sequence_state == DETECTOR_FORENSIC_REPEATED_SEQUENCE);
    row.frame = DETECTOR_FORENSIC_DISASSOC; row.ts_us++;
    assert(detector_forensic_accept(&session, &row));
    assert(row.sequence_state == DETECTOR_FORENSIC_INSUFFICIENT_SEQUENCE);
    assert(session.burst_count == 3 && session.largest_burst == 2);

    for (int i = session.count; i < DETECTOR_FORENSIC_CAPACITY + 5; ++i) {
        row.frame = DETECTOR_FORENSIC_DEAUTH;
        row.ts_us += 1;
        row.seq = (uint16_t)i;
        assert(detector_forensic_accept(&session, &row));
    }
    assert(session.count == DETECTOR_FORENSIC_CAPACITY && session.local_dropped == 5 &&
           session.accepted == DETECTOR_FORENSIC_CAPACITY + 5);
    session.accepted = UINT64_MAX;
    session.local_dropped = UINT64_MAX;
    assert(detector_forensic_accept(&session, &row));
    assert(session.accepted == UINT64_MAX && session.local_dropped == UINT64_MAX);

    detector_forensic_row_t small_storage[2] = {0};
    detector_forensic_reset(&session, small_storage, 2);
    row = row_for("deauth", "00:11:22:33:44:55",
        "AA:BB:CC:DD:EE:FF", "12:34:56:78:9A:BC", 1, 10, 0, -80, 6, 1);
    assert(detector_forensic_accept(&session, &row));
    row.ts_us = 2; row.seq = 11;
    assert(detector_forensic_accept(&session, &row));
    row.ts_us = 3; row.seq = 12;
    assert(detector_forensic_accept(&session, &row));
    assert(session.count == 2 && session.rows[0].seq == 11 &&
           session.rows[0].sequence_state == DETECTOR_FORENSIC_INSUFFICIENT_SEQUENCE &&
           session.rows[1].seq == 12 &&
           session.rows[1].sequence_state == DETECTOR_FORENSIC_SEQUENCE_DELTA &&
           session.rows[1].sequence_delta == 1);
}

static void test_stats_and_long_run_fixture(void)
{
    detector_forensic_row_t storage[DETECTOR_FORENSIC_CAPACITY] = {0};
    detector_forensic_session_t session = {0};
    detector_forensic_reset(&session, storage, DETECTOR_FORENSIC_CAPACITY);
    detector_forensic_row_t rows[] = {
        row_for("deauth", "00:11:22:33:44:55", "AA:BB:CC:DD:EE:01", "12:34:56:78:9A:01", 1, 1, 0, -80, 6, 1),
        row_for("deauth", "00:11:22:33:44:55", "AA:BB:CC:DD:EE:01", "12:34:56:78:9A:01", 2, 2, 1, -81, 6, 0),
        row_for("disassoc", "00:11:22:33:44:66", "AA:BB:CC:DD:EE:02", "12:34:56:78:9A:02", 3, -1, -1, -200, -1, -1),
        row_for("disassoc", "00:11:22:33:44:66", "AA:BB:CC:DD:EE:02", "12:34:56:78:9A:02", 4, 3, 0, -87, 7, -1),
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        assert(detector_forensic_accept(&session, &rows[i]));
    detector_forensic_stats_t stats = {0};
    detector_forensic_compute_stats(&session, &stats);
    assert(stats.unique_bssid == 2 && stats.unique_da == 2 &&
           stats.rssi_known == 3 && stats.rssi_min == -87 &&
           stats.rssi_average == -83 && stats.rssi_max == -80 &&
           stats.protected_count == 1 && stats.unprotected_count == 1 &&
           stats.protected_unknown == 2 && stats.retry_count == 1 &&
           stats.retry_unknown == 1 &&
           test_reason_count(&session, 6) == 2 &&
           test_reason_count(&session, 7) == 1);

    detector_forensic_reset(&session, storage, DETECTOR_FORENSIC_CAPACITY);
    const int sizes[] = {2, 2, 3, 2, 3, 1, 1, 3, 3, 2, 1};
    uint64_t ts = 0;
    int emitted = 0;
    for (size_t burst = 0; burst < sizeof(sizes) / sizeof(sizes[0]); ++burst) {
        char tx[18];
        snprintf(tx, sizeof(tx), "AA:BB:CC:DD:EE:%02X", (unsigned)burst);
        if (burst) ts += 20000001;
        for (int i = 0; i < sizes[burst]; ++i) {
            detector_forensic_row_t sample = row_for(
                "deauth", "00:11:22:33:44:55", tx, "12:34:56:78:9A:BC",
                ts++, emitted & 4095, emitted % 5 == 0, -80 - (emitted % 8), 6, 1);
            assert(detector_forensic_accept(&session, &sample));
            ++emitted;
        }
    }
    detector_forensic_compute_stats(&session, &stats);
    assert(emitted == 23 && session.accepted == 23 && session.burst_count == 11 &&
           session.largest_burst == 3 && stats.unique_bssid == 1 &&
           stats.unique_da == 1 && stats.rssi_min == -87 && stats.rssi_max == -80 &&
           test_reason_count(&session, 6) == 23);
}

static void test_session_ownership_and_final_stop(void)
{
    detector_forensic_row_t storage[DETECTOR_FORENSIC_CAPACITY] = {0};
    detector_forensic_session_t session = {0};
    detector_forensic_reset(&session, storage, DETECTOR_FORENSIC_CAPACITY);
    detector_forensic_row_t row = row_for("deauth", "00:11:22:33:44:55",
        "AA:BB:CC:DD:EE:FF", "12:34:56:78:9A:BC", 1, 1, 0, -80, 6, 1);
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 7, 9, 11, true));
    detector_forensic_begin_session(&session, storage, DETECTOR_FORENSIC_CAPACITY,
                                    0, 1, 7, 9, 11);
    assert(session.active && !session.finalized && session.accepted == 0);
    assert(!detector_forensic_accept_owned(&session, &row, 2, 1, 7, 9, 11, true));
    assert(!detector_forensic_accept_owned(&session, &row, 0, 2, 7, 9, 11, true));
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 8, 9, 11, true));
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 7, 10, 11, true));
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 7, 9, 12, true));
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 7, 9, 11, false));
    assert(session.accepted == 0 && !session.marker_seen);
    assert(detector_forensic_accept_owned(&session, &row, 0, 1, 7, 9, 11, true));
    assert(session.accepted == 1 && session.marker_seen);
    detector_forensic_finalize(&session);
    assert(!session.active && session.finalized && session.accepted == 1);
    row.ts_us++;
    assert(!detector_forensic_accept_owned(&session, &row, 0, 1, 7, 9, 11, true));
    assert(session.accepted == 1);
    detector_forensic_begin_session(&session, storage, DETECTOR_FORENSIC_CAPACITY,
                                    2, 2, 0, 10, 12);
    assert(session.active && !session.finalized && !session.marker_seen &&
           session.accepted == 0 && session.count == 0);
}

int main(void)
{
    test_strict_parser();
    test_bursts_capacity_and_sequences();
    test_stats_and_long_run_fixture();
    test_session_ownership_and_final_stop();
    puts("detector forensic parser and model tests passed");
    return 0;
}
