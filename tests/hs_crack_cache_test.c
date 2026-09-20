#define _POSIX_C_SOURCE 200809L

#include "hs_crack_cache.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int crc_checkpoint_calls;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static bool cancel_crc(void *context)
{
    (void)context;
    crc_checkpoint_calls++;
    return false;
}

static void join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int written = snprintf(out, out_size, "%s/%s", dir, name);
    CHECK(written > 0 && (size_t)written < out_size);
}

static void write_bytes(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (!file) return;
    CHECK(fwrite(data, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void test_fingerprints(const char *dir)
{
    char pcap[256];
    char list_a[256];
    char list_b[256];
    join_path(pcap, sizeof(pcap), dir, "capture.pcap");
    join_path(list_a, sizeof(list_a), dir, "alpha.txt");
    join_path(list_b, sizeof(list_b), dir, "beta.txt");

    const unsigned char pcap_data[] = {0xD4, 0xC3, 0xB2, 0xA1, 1, 2, 3, 4};
    write_bytes(pcap, pcap_data, sizeof(pcap_data));
    write_bytes(list_a, "password-one\npassword-two\n", 26);
    write_bytes(list_b, "password-one\npassword-two\n", 26);

    hs_crack_capture_id_t capture_a;
    hs_crack_capture_id_t capture_b;
    CHECK(hs_crack_cache_capture_id(pcap, "Cafe,Guest", "02:00:00:00:00:01",
                                    &capture_a) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_capture_id(pcap, "Cafe,Guest", "02:00:00:00:00:01",
                                    &capture_b) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_capture_equal(&capture_a, &capture_b));

    const unsigned char changed_pcap[] = {0xD4, 0xC3, 0xB2, 0xA1, 1, 2, 3, 5};
    write_bytes(pcap, changed_pcap, sizeof(changed_pcap));
    CHECK(hs_crack_cache_capture_id(pcap, "Cafe,Guest", "02:00:00:00:00:01",
                                    &capture_b) == HS_CRACK_CACHE_OK);
    CHECK(!hs_crack_cache_capture_equal(&capture_a, &capture_b));

    hs_crack_wordlist_id_t wordlist_a;
    hs_crack_wordlist_id_t wordlist_a_again;
    hs_crack_wordlist_id_t wordlist_b;
    CHECK(hs_crack_cache_wordlist_id(list_a, &wordlist_a) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_wordlist_id(list_a, &wordlist_a_again) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_wordlist_equal(&wordlist_a, &wordlist_a_again));
    CHECK(hs_crack_cache_wordlist_id(list_b, &wordlist_b) == HS_CRACK_CACHE_OK);
    CHECK(!hs_crack_cache_wordlist_equal(&wordlist_a, &wordlist_b));

    write_bytes(list_a, "Xassword-one\npassword-two\n", 26);
    CHECK(hs_crack_cache_wordlist_id(list_a, &wordlist_a_again) == HS_CRACK_CACHE_OK);
    CHECK(!hs_crack_cache_wordlist_equal(&wordlist_a, &wordlist_a_again));
}

static void test_attempt_csv_round_trip(const char *dir)
{
    char pcap[256];
    char list[256];
    char attempts[256];
    join_path(pcap, sizeof(pcap), dir, "roundtrip.pcap");
    join_path(list, sizeof(list), dir, "roundtrip.txt");
    join_path(attempts, sizeof(attempts), dir, "attempts.csv");
    write_bytes(pcap, "pcap-fixture", 12);
    write_bytes(list, "wrongpass\nrightpass\n", 20);

    hs_crack_attempt_t input = {0};
    CHECK(hs_crack_cache_capture_id(pcap, "Cafe,Guest", "02:00:00:00:00:02",
                                    &input.capture) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_wordlist_id(list, &input.wordlist) == HS_CRACK_CACHE_OK);
    input.status = HS_CRACK_ATTEMPT_FOUND;
    input.tried = 17;
    input.match_index = 3;
    snprintf(input.password, sizeof(input.password), "%s", "say\"hello,world");

    CHECK(hs_crack_cache_upsert_attempt(attempts, &input) == HS_CRACK_CACHE_OK);

    hs_crack_attempt_t output = {0};
    CHECK(hs_crack_cache_find_attempt(attempts, &input.capture, false, 0,
                                      &input.wordlist, &output) == HS_CRACK_CACHE_OK);
    CHECK(output.status == HS_CRACK_ATTEMPT_FOUND);
    CHECK(output.tried == 17);
    CHECK(output.match_index == 3);
    CHECK(strcmp(output.capture.ssid, "Cafe,Guest") == 0);
    CHECK(strcmp(output.password, "say\"hello,world") == 0);

    FILE *raw = fopen(attempts, "rb");
    CHECK(raw != NULL);
    if (raw) {
        char text[2048] = {0};
        size_t got = fread(text, 1, sizeof(text) - 1, raw);
        fclose(raw);
        text[got] = '\0';
        CHECK(strstr(text, "\"Cafe,Guest\"") != NULL);
        CHECK(strstr(text, "\"say\"\"hello,world\"") != NULL);
    }

    input.status = HS_CRACK_ATTEMPT_NOTFOUND;
    input.tried = 99;
    input.password[0] = '\0';
    CHECK(hs_crack_cache_upsert_attempt(attempts, &input) == HS_CRACK_CACHE_OK);
    memset(&output, 0, sizeof(output));
    CHECK(hs_crack_cache_find_attempt(attempts, &input.capture, false, 0,
                                      &input.wordlist, &output) == HS_CRACK_CACHE_OK);
    CHECK(output.status == HS_CRACK_ATTEMPT_NOTFOUND);
    CHECK(output.tried == 99);
    CHECK(output.password[0] == '\0');

    hs_crack_attempt_t generic = {0};
    generic.capture = input.capture;
    generic.generic = true;
    generic.generic_version = HS_CRACK_GENERIC_VERSION;
    generic.status = HS_CRACK_ATTEMPT_NOTFOUND;
    generic.tried = 48;
    CHECK(hs_crack_cache_upsert_attempt(attempts, &generic) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_find_attempt(attempts, &generic.capture, true,
                                      HS_CRACK_GENERIC_VERSION, NULL,
                                      &output) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_find_attempt(attempts, &generic.capture, true,
                                      HS_CRACK_GENERIC_VERSION + 1U, NULL,
                                      &output) == HS_CRACK_CACHE_NOT_FOUND);

}

static void test_resume_round_trip(const char *dir)
{
    char pcap[256];
    char list[256];
    char resume_path[256];
    join_path(pcap, sizeof(pcap), dir, "resume.pcap");
    join_path(list, sizeof(list), dir, "resume.lst");
    join_path(resume_path, sizeof(resume_path), dir, ".crack_resume");
    write_bytes(pcap, "pcap-resume", 11);
    write_bytes(list, "password-one\npassword-two\n", 26);

    hs_crack_resume_t input = {0};
    CHECK(hs_crack_cache_capture_id(pcap, "Resume SSID", "02:00:00:00:00:03",
                                    &input.capture) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_wordlist_id(list, &input.wordlist) == HS_CRACK_CACHE_OK);
    input.safe_offset = 13;
    input.tried = 1;
    input.all_mode = true;
    input.selected_list_index = 2;
    input.updated_sequence = 7;
    CHECK(hs_crack_cache_save_resume(resume_path, &input) == HS_CRACK_CACHE_OK);

    hs_crack_resume_t output = {0};
    CHECK(hs_crack_cache_load_resume(resume_path, &input.capture, &input.wordlist,
                                     &output) == HS_CRACK_CACHE_OK);
    CHECK(output.safe_offset == 13);
    CHECK(output.tried == 1);
    CHECK(output.all_mode);
    CHECK(output.selected_list_index == 2);
    CHECK(output.updated_sequence == 7);

    hs_crack_wordlist_id_t stale = input.wordlist;
    stale.tail_crc32 ^= 1U;
    CHECK(hs_crack_cache_load_resume(resume_path, &input.capture, &stale,
                                     &output) == HS_CRACK_CACHE_STALE);
    CHECK(hs_crack_cache_remove_resume(resume_path) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_load_resume(resume_path, &input.capture, &input.wordlist,
                                     &output) == HS_CRACK_CACHE_NOT_FOUND);
}

static void test_full_crc_cache(const char *dir)
{
    char list[256];
    char cache[256];
    join_path(list, sizeof(list), dir, "crc-list.txt");
    join_path(cache, sizeof(cache), dir, "wordlist_crc.csv");
    write_bytes(list, "password-one\npassword-two\n", 26);

    hs_crack_wordlist_id_t id = {0};
    CHECK(hs_crack_cache_wordlist_id(list, &id) == HS_CRACK_CACHE_OK);

    uint32_t calculated = 0;
    uint32_t cached = 0;
    CHECK(hs_crack_cache_find_full_crc(cache, &id, &cached) ==
          HS_CRACK_CACHE_NOT_FOUND);
    CHECK(hs_crack_cache_file_crc32(list, &calculated) == HS_CRACK_CACHE_OK);
    crc_checkpoint_calls = 0;
    CHECK(hs_crack_cache_file_crc32_checked(list, cancel_crc, NULL, &cached) ==
          HS_CRACK_CACHE_IO_ERROR);
    CHECK(crc_checkpoint_calls == 1);
    CHECK(hs_crack_cache_store_full_crc(cache, &id, calculated) ==
          HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_find_full_crc(cache, &id, &cached) ==
          HS_CRACK_CACHE_OK);
    CHECK(cached == calculated);

    hs_crack_wordlist_id_t stale = id;
    stale.head_crc32 ^= 1U;
    CHECK(hs_crack_cache_find_full_crc(cache, &stale, &cached) ==
          HS_CRACK_CACHE_NOT_FOUND);

    write_bytes(list, "Xassword-one\npassword-two\n", 26);
    hs_crack_wordlist_id_t changed = {0};
    CHECK(hs_crack_cache_wordlist_id(list, &changed) == HS_CRACK_CACHE_OK);
    CHECK(hs_crack_cache_find_full_crc(cache, &changed, &cached) ==
          HS_CRACK_CACHE_NOT_FOUND);
}

static void test_catalog_and_progress(void)
{
    hs_crack_wordlist_entry_t entries[3] = {0};
    snprintf(entries[0].name, sizeof(entries[0].name), "%s", "z.lst");
    snprintf(entries[1].name, sizeof(entries[1].name), "%s", "B.txt");
    snprintf(entries[2].name, sizeof(entries[2].name), "%s", "a.txt");
    hs_crack_cache_sort_wordlists(entries, 3);
    CHECK(strcmp(entries[0].name, "a.txt") == 0);
    CHECK(strcmp(entries[1].name, "B.txt") == 0);
    CHECK(strcmp(entries[2].name, "z.lst") == 0);

    CHECK(hs_crack_cache_wordlist_name_allowed("words.txt"));
    CHECK(hs_crack_cache_wordlist_name_allowed("polish.LST"));
    CHECK(hs_crack_cache_wordlist_name_allowed("common.dic"));
    CHECK(!hs_crack_cache_wordlist_name_allowed(".cache.txt"));
    CHECK(!hs_crack_cache_wordlist_name_allowed("notes.csv"));
    CHECK(!hs_crack_cache_wordlist_name_allowed("words.txt.tmp"));

    hs_crack_source_kind_t source = HS_CRACK_SOURCE_ALL;
    size_t source_index = 99;
    CHECK(hs_crack_cache_decode_source(0, 3, &source, &source_index));
    CHECK(source == HS_CRACK_SOURCE_NONE);
    CHECK(hs_crack_cache_decode_source(1, 3, &source, &source_index));
    CHECK(source == HS_CRACK_SOURCE_INTERNAL);
    CHECK(hs_crack_cache_decode_source(2, 3, &source, &source_index));
    CHECK(source == HS_CRACK_SOURCE_WORDLIST && source_index == 0);
    CHECK(hs_crack_cache_decode_source(4, 3, &source, &source_index));
    CHECK(source == HS_CRACK_SOURCE_WORDLIST && source_index == 2);
    CHECK(hs_crack_cache_decode_source(5, 3, &source, &source_index));
    CHECK(source == HS_CRACK_SOURCE_ALL);
    CHECK(!hs_crack_cache_decode_source(2, 0, &source, &source_index));
    CHECK(!hs_crack_cache_decode_source(3, 1, &source, &source_index));
    CHECK(hs_crack_recommended_worker_count(false) == 2);
    CHECK(hs_crack_recommended_worker_count(true) == 1);

    CHECK(hs_crack_cache_byte_percent(0, 1000) == 0);
    CHECK(hs_crack_cache_byte_percent(421, 1000) == 42);
    CHECK(hs_crack_cache_byte_percent(1001, 1000) == 100);
    CHECK(hs_crack_cache_byte_percent(1, 0) == 0);
    CHECK(hs_crack_cache_eta_seconds(0, 1000, 1, 1000000) == UINT64_MAX);
    CHECK(hs_crack_cache_eta_seconds(500, 1000, 10, 10000000) == 10);
    CHECK(hs_crack_cache_eta_seconds(1000, 1000, 10, 10000000) == 0);

    CHECK(!hs_crack_checkpoint_due(63, 299000000));
    CHECK(hs_crack_checkpoint_due(64, 0));
    CHECK(hs_crack_checkpoint_due(0, 300000000));
    CHECK(!hs_crack_checkpoint_due(0, 0));
}

int main(void)
{
    char temp[] = "/tmp/hs-crack-cache-XXXXXX";
    char *dir = mkdtemp(temp);
    if (!dir) {
        fprintf(stderr, "mkdtemp failed: %s\n", strerror(errno));
        return 2;
    }

    test_fingerprints(dir);
    test_attempt_csv_round_trip(dir);
    test_resume_round_trip(dir);
    test_full_crc_cache(dir);
    test_catalog_and_progress();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("hs_crack_cache_test: PASS\n");
    return 0;
}
