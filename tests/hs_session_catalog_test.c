#define _POSIX_C_SOURCE 200809L
#include "hs_session_catalog.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void fill_session(hs_session_t *session, uint8_t id, const char *capture,
                         const char *wordlist)
{
    memset(session, 0, sizeof(*session));
    session->schema_version = HS_SESSION_SCHEMA_VERSION;
    session->state = HS_SESSION_ACTIVE;
    memset(session->session_id, id, sizeof(session->session_id));
    session->capture.size = 1000U + id;
    session->capture.crc32 = 0x12340000U + id;
    session->origin.source = HS_SESSION_SOURCE_USB;
    snprintf(session->origin.remote_path, sizeof(session->origin.remote_path),
             "/remote/%s", capture);
    session->wordlist_count = 1;
    snprintf(session->wordlists[0].path, sizeof(session->wordlists[0].path),
             "/lists/%s", wordlist);
    session->wordlists[0].size = 5000U + id;
    session->phase.kind = HS_SESSION_PHASE_WORDLIST;
    session->shard_count = 1;
    session->shards[0].kind = HS_SESSION_SHARD_LOCAL;
    session->shards[0].state = HS_SESSION_SHARD_LEASED;
    session->shards[0].range_end = session->wordlists[0].size;
    session->shards[0].safe_offset = id;
}

static void session_hex(uint8_t id, char output[33])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < HS_SESSION_ID_BYTES; ++i) {
        output[i * 2U] = digits[id >> 4U];
        output[i * 2U + 1U] = digits[id & 0x0fU];
    }
    output[32] = '\0';
}

static void set_slot_time(const char *root, uint8_t id, time_t seconds)
{
    char hex[33], path[512];
    session_hex(id, hex);
    snprintf(path, sizeof(path), "%s/%s/active.a", root, hex);
    struct timespec times[2] = {{.tv_sec = seconds}, {.tv_sec = seconds}};
    assert(utimensat(AT_FDCWD, path, times, 0) == 0);
}

static void copy_file(const char *source, const char *target)
{
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    assert(input && output);
    unsigned char buffer[512];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0)
        assert(fwrite(buffer, 1, count, output) == count);
    assert(!ferror(input));
    assert(fclose(input) == 0 && fclose(output) == 0);
}

static void multiple_sessions_are_independent(void)
{
    char base[] = "/tmp/hs-session-catalog-XXXXXX";
    assert(mkdtemp(base));
    char root[512];
    snprintf(root, sizeof(root), "%s/active", base);

    hs_session_t first, second, loaded;
    fill_session(&first, 0x11, "first.pcap", "first.txt");
    fill_session(&second, 0x22, "second.pcap", "second.txt");
    assert(hs_session_catalog_save(root, &first) == HS_SESSION_OK);
    assert(hs_session_catalog_save(root, &second) == HS_SESSION_OK);
    set_slot_time(root, 0x11, 100);
    set_slot_time(root, 0x22, 200);

    hs_session_catalog_entry_t entries[8] = {0};
    size_t count = 0;
    assert(hs_session_catalog_list(root, NULL, NULL, entries, 8, &count) ==
           HS_SESSION_OK);
    assert(count == 2);
    assert(entries[0].session.session_id[0] == 0x22);
    assert(entries[1].session.session_id[0] == 0x11);
    assert(entries[0].updated_at >= entries[1].updated_at);

    assert(hs_session_catalog_find_id(root, NULL, NULL, first.session_id,
                                      &loaded) == HS_SESSION_OK);
    assert(strcmp(loaded.origin.remote_path, "/remote/first.pcap") == 0);
    assert(hs_session_catalog_tombstone(root, NULL, NULL, first.session_id) ==
           HS_SESSION_OK);
    assert(hs_session_catalog_find_id(root, NULL, NULL, first.session_id,
                                      &loaded) == HS_SESSION_NOT_FOUND);
    assert(hs_session_catalog_find_id(root, NULL, NULL, second.session_id,
                                      &loaded) == HS_SESSION_OK);
    assert(hs_session_catalog_list(root, NULL, NULL, entries, 8, &count) ==
           HS_SESSION_OK);
    assert(count == 1 && entries[0].session.session_id[0] == 0x22);
}

static void legacy_slot_is_visible_without_duplicates(void)
{
    char base[] = "/tmp/hs-session-legacy-XXXXXX";
    assert(mkdtemp(base));
    char root[512], legacy_a[512], legacy_b[512];
    snprintf(root, sizeof(root), "%s/active", base);
    snprintf(legacy_a, sizeof(legacy_a), "%s/active.a", base);
    snprintf(legacy_b, sizeof(legacy_b), "%s/active.b", base);

    hs_session_t legacy;
    fill_session(&legacy, 0x33, "legacy.pcap", "legacy.txt");
    assert(hs_session_save_next(legacy_a, legacy_b, &legacy) == HS_SESSION_OK);

    hs_session_catalog_entry_t entries[8] = {0};
    size_t count = 0;
    assert(hs_session_catalog_list(root, legacy_a, legacy_b, entries, 8,
                                   &count) == HS_SESSION_OK);
    assert(count == 1 && entries[0].legacy);

    assert(hs_session_catalog_save(root, &legacy) == HS_SESSION_OK);
    memset(entries, 0, sizeof(entries));
    assert(hs_session_catalog_list(root, legacy_a, legacy_b, entries, 8,
                                   &count) == HS_SESSION_OK);
    assert(count == 1 && !entries[0].legacy);

    assert(hs_session_catalog_tombstone(root, legacy_a, legacy_b,
                                        legacy.session_id) == HS_SESSION_OK);
    memset(entries, 0, sizeof(entries));
    assert(hs_session_catalog_list(root, legacy_a, legacy_b, entries, 8,
                                   &count) == HS_SESSION_OK);
    assert(count == 0);
}

static void mismatched_directory_identity_is_rejected(void)
{
    char base[] = "/tmp/hs-session-identity-XXXXXX";
    assert(mkdtemp(base));
    char root[512];
    snprintf(root, sizeof(root), "%s/active", base);
    hs_session_t session, loaded;
    fill_session(&session, 0x44, "identity.pcap", "identity.txt");
    assert(hs_session_catalog_save(root, &session) == HS_SESSION_OK);

    char real_hex[33], fake_hex[33], fake_dir[512], source[512], target[512];
    session_hex(0x44, real_hex);
    session_hex(0x55, fake_hex);
    snprintf(fake_dir, sizeof(fake_dir), "%s/%s", root, fake_hex);
    assert(mkdir(fake_dir, 0775) == 0);
    snprintf(source, sizeof(source), "%s/%s/active.a", root, real_hex);
    snprintf(target, sizeof(target), "%s/active.a", fake_dir);
    copy_file(source, target);

    uint8_t fake_id[HS_SESSION_ID_BYTES];
    memset(fake_id, 0x55, sizeof(fake_id));
    assert(hs_session_catalog_find_id(root, NULL, NULL, fake_id, &loaded) !=
           HS_SESSION_OK);
}

static void exact_match_is_found_beyond_the_ui_limit(void)
{
    char base[] = "/tmp/hs-session-deep-match-XXXXXX";
    assert(mkdtemp(base));
    char root[512];
    snprintf(root, sizeof(root), "%s/active", base);

    hs_session_t target;
    fill_session(&target, 0x60, "target.pcap", "target.txt");
    assert(hs_session_catalog_save(root, &target) == HS_SESSION_OK);
    set_slot_time(root, 0x60, 100);
    for (uint8_t i = 1; i <= HS_SESSION_CATALOG_UI_LIMIT + 1U; ++i) {
        hs_session_t unrelated;
        char capture[32], wordlist[32];
        snprintf(capture, sizeof(capture), "newer-%u.pcap", (unsigned)i);
        snprintf(wordlist, sizeof(wordlist), "newer-%u.txt", (unsigned)i);
        fill_session(&unrelated, (uint8_t)(0x60U + i), capture, wordlist);
        unrelated.capture.crc32 += i;
        assert(hs_session_catalog_save(root, &unrelated) == HS_SESSION_OK);
        set_slot_time(root, (uint8_t)(0x60U + i), (time_t)(200 + i));
    }

    hs_session_catalog_entry_t visible[HS_SESSION_CATALOG_UI_LIMIT] = {0};
    size_t visible_count = 0;
    assert(hs_session_catalog_list(root, NULL, NULL, visible,
                                   HS_SESSION_CATALOG_UI_LIMIT,
                                   &visible_count) == HS_SESSION_OK);
    assert(visible_count == HS_SESSION_CATALOG_UI_LIMIT);
    for (size_t i = 0; i < visible_count; ++i)
        assert(visible[i].session.session_id[0] != 0x60);

    hs_session_wordlist_t wordlist = target.wordlists[0];
    hs_session_t loaded;
    int index = -1;
    bool matching_capture = false;
    assert(hs_session_catalog_find_newest_wordlist(
               root, NULL, NULL, target.capture.size, target.capture.crc32,
               &wordlist, 1, &loaded, &index, &matching_capture) ==
           HS_SESSION_OK);
    assert(matching_capture);
    assert(index == 0);
    assert(loaded.session_id[0] == 0x60);
}

int main(void)
{
    multiple_sessions_are_independent();
    legacy_slot_is_visible_without_duplicates();
    mismatched_directory_identity_is_rejected();
    exact_match_is_found_beyond_the_ui_limit();
    puts("hs_session_catalog_test: PASS");
    return 0;
}
