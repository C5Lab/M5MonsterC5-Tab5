#define _POSIX_C_SOURCE 200809L

#include "hs_audit_history.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int written = snprintf(out, out_size, "%s/%s", dir, name);
    CHECK(written > 0 && (size_t)written < out_size);
}

static void fill_record(hs_audit_history_record_t *record,
                        uint8_t first_id_byte, uint64_t episode)
{
    memset(record, 0, sizeof(*record));
    record->schema_version = HS_AUDIT_HISTORY_SCHEMA_VERSION;
    for (size_t i = 0; i < HS_SESSION_ID_BYTES; ++i)
        record->session_id[i] = (uint8_t)(first_id_byte + i);
    record->episode_sequence = episode;
    record->started_at_available = true;
    record->started_at_unix_seconds = 1700000000;
    record->finished_at_available = true;
    record->finished_at_unix_seconds = 1700000042;
    record->origin_source = HS_SESSION_SOURCE_USB;
    snprintf(record->origin_local_path, sizeof(record->origin_local_path),
             "%s", "/sdcard/input, \"quoted\".pcap");
    snprintf(record->origin_remote_path, sizeof(record->origin_remote_path),
             "%s", "remote path\nwith spaces.pcap");
    record->selected_list_count = 2;
    snprintf(record->selected_lists[0], sizeof(record->selected_lists[0]),
             "%s", "alpha words.txt");
    snprintf(record->selected_lists[1], sizeof(record->selected_lists[1]),
             "%s", "beta,\"quoted\".lst");
    record->requested_workers = 3;
    record->effective_workers = 2;
    record->elapsed_time_ms = 42000;
    record->rate_milli_per_second = 1234567;
    record->last_eta_seconds = 987;
    record->tried_count = 12345;
    record->resume_count = 4;
    record->outcome = HS_SESSION_OUTCOME_INTERRUPTED;
    record->reason = -71;
}

static void test_round_trip_and_idempotence(const char *dir)
{
    hs_audit_history_record_t input;
    fill_record(&input, 0x80, 9);

    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_OK);
    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_OK);

    hs_audit_history_record_t output[2] = {0};
    size_t count = 0;
    CHECK(hs_audit_history_list(dir, output, 2, &count) == HS_AUDIT_HISTORY_OK);
    CHECK(count == 1);
    CHECK(memcmp(output[0].session_id, input.session_id, HS_SESSION_ID_BYTES) == 0);
    CHECK(output[0].episode_sequence == 9);
    CHECK(strcmp(output[0].origin_local_path, input.origin_local_path) == 0);
    CHECK(strcmp(output[0].origin_remote_path, input.origin_remote_path) == 0);
    CHECK(strcmp(output[0].selected_lists[1], input.selected_lists[1]) == 0);
    CHECK(output[0].selected_list_count == 2);
    CHECK(output[0].requested_workers == 3);
    CHECK(output[0].effective_workers == 2);
    CHECK(output[0].elapsed_time_ms == 42000);
    CHECK(output[0].rate_milli_per_second == 1234567);
    CHECK(output[0].last_eta_seconds == 987);
    CHECK(output[0].tried_count == 12345);
    CHECK(output[0].resume_count == 4);
    CHECK(output[0].outcome == HS_SESSION_OUTCOME_INTERRUPTED);
    CHECK(output[0].reason == -71);

    hs_audit_history_record_t conflict = input;
    conflict.tried_count++;
    CHECK(hs_audit_history_append(dir, &conflict) == HS_AUDIT_HISTORY_INVALID);

    input.origin_source = (hs_session_source_t)-1;
    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_INVALID);

    char path[512];
    join_path(path, sizeof(path), dir,
              "episode-808182838485868788898a8b8c8d8e8f-00000000000000000009.csv");
    FILE *file = fopen(path, "r+b");
    CHECK(file != NULL);
    if (file) {
        CHECK(fseek(file, -1, SEEK_END) == 0);
        CHECK(fputs(",unexpected\n", file) >= 0);
        CHECK(fclose(file) == 0);
    }
    size_t after_corruption = 99;
    CHECK(hs_audit_history_list(dir, output, 2, &after_corruption) == HS_AUDIT_HISTORY_OK);
    CHECK(after_corruption == 0);

    input.origin_source = HS_SESSION_SOURCE_USB;
    CHECK(remove(path) == 0);
    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_OK);
    file = fopen(path, "ab");
    CHECK(file != NULL);
    if (file) {
        const unsigned char hidden[] = {0, 'x'};
        CHECK(fwrite(hidden, 1, sizeof(hidden), file) == sizeof(hidden));
        CHECK(fclose(file) == 0);
    }
    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_INVALID);
}

static void test_missing_rtc_and_newest_first_limit(const char *dir)
{
    hs_audit_history_record_t first;
    hs_audit_history_record_t second;
    hs_audit_history_record_t third;
    fill_record(&first, 0x10, 2);
    fill_record(&second, 0x20, 7);
    fill_record(&third, 0x30, 5);
    first.started_at_available = false;
    first.finished_at_available = false;
    first.started_at_unix_seconds = 99;
    first.finished_at_unix_seconds = 99;

    CHECK(hs_audit_history_append(dir, &first) == HS_AUDIT_HISTORY_OK);
    CHECK(hs_audit_history_append(dir, &second) == HS_AUDIT_HISTORY_OK);
    CHECK(hs_audit_history_append(dir, &third) == HS_AUDIT_HISTORY_OK);

    hs_audit_history_record_t output[2] = {0};
    size_t count = 0;
    CHECK(hs_audit_history_list(dir, output, 2, &count) == HS_AUDIT_HISTORY_OK);
    CHECK(count == 2);
    CHECK(output[0].episode_sequence == 7);
    CHECK(output[1].episode_sequence == 5);

    hs_audit_history_record_t all[4] = {0};
    CHECK(hs_audit_history_list(dir, all, 4, &count) == HS_AUDIT_HISTORY_OK);
    CHECK(count == 3);
    CHECK(!all[2].started_at_available);
    CHECK(!all[2].finished_at_available);
}

static void write_bytes(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (!file) return;
    CHECK(fputs(text, file) >= 0);
    CHECK(fclose(file) == 0);
}

static void test_corrupt_and_temporary_files_are_ignored(const char *dir)
{
    hs_audit_history_record_t input;
    fill_record(&input, 0x40, 11);
    CHECK(hs_audit_history_append(dir, &input) == HS_AUDIT_HISTORY_OK);

    char corrupt[512];
    char temporary[512];
    join_path(corrupt, sizeof(corrupt), dir,
              "episode-000102030405060708090a0b0c0d0e0f-00000000000000000012.csv");
    join_path(temporary, sizeof(temporary), dir, "episode-orphan.tmp.99");
    write_bytes(corrupt, "not a history record\\n");
    write_bytes(temporary, "must remain ignored\\n");

    hs_audit_history_record_t output = {0};
    size_t count = 0;
    CHECK(hs_audit_history_list(dir, &output, 1, &count) == HS_AUDIT_HISTORY_OK);
    CHECK(count == 1);
    CHECK(output.episode_sequence == 11);
    CHECK(access(corrupt, F_OK) == 0);
    CHECK(access(temporary, F_OK) == 0);

    hs_audit_history_record_t collision;
    fill_record(&collision, 0x00, 12);
    CHECK(hs_audit_history_append(dir, &collision) == HS_AUDIT_HISTORY_INVALID);
}

static void test_terminal_outcomes(const char *dir)
{
    hs_audit_history_record_t found;
    hs_audit_history_record_t not_found;
    hs_audit_history_record_t failed;
    fill_record(&found, 0x50, 20);
    fill_record(&not_found, 0x60, 21);
    fill_record(&failed, 0x70, 22);
    found.outcome = HS_SESSION_OUTCOME_FOUND;
    not_found.outcome = HS_SESSION_OUTCOME_NOT_FOUND;
    failed.outcome = HS_SESSION_OUTCOME_ERROR;
    CHECK(hs_audit_history_append(dir, &found) == HS_AUDIT_HISTORY_OK);
    CHECK(hs_audit_history_append(dir, &not_found) == HS_AUDIT_HISTORY_OK);
    CHECK(hs_audit_history_append(dir, &failed) == HS_AUDIT_HISTORY_OK);

    hs_audit_history_record_t output[3] = {0};
    size_t count = 0;
    CHECK(hs_audit_history_list(dir, output, 3, &count) == HS_AUDIT_HISTORY_OK);
    CHECK(count == 3);
    CHECK(output[0].outcome == HS_SESSION_OUTCOME_ERROR);
    CHECK(output[1].outcome == HS_SESSION_OUTCOME_NOT_FOUND);
    CHECK(output[2].outcome == HS_SESSION_OUTCOME_FOUND);
}

static void test_build_from_session(const char *dir)
{
    hs_session_t session = {0};
    session.schema_version = HS_SESSION_SCHEMA_VERSION;
    session.session_id[0] = 0x00;
    session.session_id[1] = 0xff;
    session.origin.source = HS_SESSION_SOURCE_GROVE;
    snprintf(session.origin.local_path, sizeof(session.origin.local_path), "%s", "local");
    snprintf(session.origin.remote_path, sizeof(session.origin.remote_path), "%s", "remote");
    session.config.requested_workers = 3;
    session.wordlist_count = 1;
    snprintf(session.wordlists[0].path, sizeof(session.wordlists[0].path), "%s", "chosen.lst");
    session.worker_count = 2;
    session.metrics.active_time_ms = 111;
    session.metrics.last_rate_milli_per_second = 222;
    session.metrics.last_eta_seconds = 333;
    session.metrics.total_tried = 444;
    session.metrics.resume_count = 5;
    session.has_result = true;
    session.result.outcome = HS_SESSION_OUTCOME_STALE;
    session.result.reason = -9;
    memcpy(session.result.password, "super-secret", 12);
    session.result.password_length = 12;

    hs_audit_history_record_t output = {0};
    CHECK(hs_audit_history_from_session(&session, 3, false, 0, false, 0, &output) ==
          HS_AUDIT_HISTORY_OK);
    CHECK(output.episode_sequence == 3);
    CHECK(output.session_id[0] == 0x00 && output.session_id[1] == 0xff);
    CHECK(output.origin_source == HS_SESSION_SOURCE_GROVE);
    CHECK(output.selected_list_count == 1);
    CHECK(strcmp(output.selected_lists[0], "chosen.lst") == 0);
    CHECK(output.effective_workers == 2);
    CHECK(output.outcome == HS_SESSION_OUTCOME_STALE);
    CHECK(!output.started_at_available && !output.finished_at_available);
    CHECK(hs_audit_history_append(dir, &output) == HS_AUDIT_HISTORY_OK);

    char path[512];
    join_path(path, sizeof(path), dir,
              "episode-00ff0000000000000000000000000000-00000000000000000003.csv");
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file) {
        char raw[4096] = {0};
        size_t got = fread(raw, 1, sizeof(raw) - 1, file);
        CHECK(fclose(file) == 0);
        raw[got] = '\0';
        CHECK(strstr(raw, "super-secret") == NULL);
    }

    memset(session.origin.local_path, 'x', sizeof(session.origin.local_path));
    CHECK(hs_audit_history_from_session(&session, 4, false, 0, false, 0, &output) ==
          HS_AUDIT_HISTORY_INVALID);
}

int main(void)
{
    char temp[] = "/tmp/hs-audit-history-XXXXXX";
    char *dir = mkdtemp(temp);
    if (!dir) {
        fprintf(stderr, "mkdtemp failed: %s\\n", strerror(errno));
        return 2;
    }

    char cases[5][512];
    for (size_t i = 0; i < 5; ++i) {
        int written = snprintf(cases[i], sizeof(cases[i]), "%s/case-%zu", dir, i);
        CHECK(written > 0 && (size_t)written < sizeof(cases[i]));
        CHECK(mkdir(cases[i], 0700) == 0);
    }

    test_round_trip_and_idempotence(cases[0]);
    test_missing_rtc_and_newest_first_limit(cases[1]);
    test_corrupt_and_temporary_files_are_ignored(cases[2]);
    test_terminal_outcomes(cases[3]);
    test_build_from_session(cases[4]);

    if (failures) {
        fprintf(stderr, "%d failure(s)\\n", failures);
        return 1;
    }
    puts("hs_audit_history_test: PASS");
    return 0;
}
