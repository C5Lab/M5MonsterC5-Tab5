#define _POSIX_C_SOURCE 200809L
#include "hs_session_catalog.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void session_id_hex(const uint8_t session_id[HS_SESSION_ID_BYTES],
                           char output[33])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < HS_SESSION_ID_BYTES; ++i) {
        output[i * 2U] = digits[session_id[i] >> 4U];
        output[i * 2U + 1U] = digits[session_id[i] & 0x0fU];
    }
    output[32] = '\0';
}

static bool valid_session_dir_name(const char *name)
{
    if (!name || strlen(name) != 32U) return false;
    for (size_t i = 0; i < 32U; ++i) {
        char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static hs_session_result_t build_paths(
    const char *root, const uint8_t session_id[HS_SESSION_ID_BYTES],
    char directory[HS_SESSION_CATALOG_PATH_MAX],
    char slot_a[HS_SESSION_CATALOG_PATH_MAX],
    char slot_b[HS_SESSION_CATALOG_PATH_MAX])
{
    if (!root || !*root || !session_id) return HS_SESSION_RANGE;
    char id[33];
    session_id_hex(session_id, id);
    int dir_length = snprintf(directory, HS_SESSION_CATALOG_PATH_MAX,
                              "%s/%s", root, id);
    int a_length = snprintf(slot_a, HS_SESSION_CATALOG_PATH_MAX,
                            "%s/active.a", directory);
    int b_length = snprintf(slot_b, HS_SESSION_CATALOG_PATH_MAX,
                            "%s/active.b", directory);
    return dir_length >= 0 && a_length >= 0 && b_length >= 0 &&
                   (size_t)dir_length < HS_SESSION_CATALOG_PATH_MAX &&
                   (size_t)a_length < HS_SESSION_CATALOG_PATH_MAX &&
                   (size_t)b_length < HS_SESSION_CATALOG_PATH_MAX
               ? HS_SESSION_OK : HS_SESSION_RANGE;
}

static int64_t path_mtime(const char *path)
{
    struct stat state;
    return path && stat(path, &state) == 0 ? (int64_t)state.st_mtime : 0;
}

static int64_t newest_mtime(const char *slot_a, const char *slot_b)
{
    int64_t a = path_mtime(slot_a);
    int64_t b = path_mtime(slot_b);
    return a > b ? a : b;
}

static bool same_id(const uint8_t left[HS_SESSION_ID_BYTES],
                    const uint8_t right[HS_SESSION_ID_BYTES])
{
    return memcmp(left, right, HS_SESSION_ID_BYTES) == 0;
}

static bool directory_matches_session(const char *directory_name,
                                      const hs_session_t *session)
{
    char expected[33];
    if (!directory_name || !session) return false;
    session_id_hex(session->session_id, expected);
    return strcmp(directory_name, expected) == 0;
}

static void insert_entry(hs_session_catalog_entry_t *out, size_t capacity,
                         size_t *count, const hs_session_t *session,
                         int64_t updated_at, bool legacy)
{
    if (!out || capacity == 0 || !count || !session) return;
    for (size_t i = 0; i < *count; ++i) {
        if (!same_id(out[i].session.session_id, session->session_id)) continue;
        if (!legacy && out[i].legacy) {
            out[i].session = *session;
            out[i].updated_at = updated_at;
            out[i].legacy = false;
        }
        return;
    }

    size_t position = 0;
    while (position < *count && out[position].updated_at >= updated_at) ++position;
    if (position >= capacity) return;
    size_t previous = *count;
    if (*count < capacity) ++*count;
    size_t move = previous < capacity ? previous - position
                                      : capacity - position - 1U;
    if (move)
        memmove(&out[position + 1U], &out[position], move * sizeof(*out));
    out[position].session = *session;
    out[position].updated_at = updated_at;
    out[position].legacy = legacy;
}

hs_session_result_t hs_session_catalog_save(const char *root,
                                            hs_session_t *session)
{
    if (!root || !*root || !session || session->state != HS_SESSION_ACTIVE)
        return HS_SESSION_RANGE;
    if (mkdir(root, 0775) != 0 && errno != EEXIST) return HS_SESSION_IO_ERROR;
    char directory[HS_SESSION_CATALOG_PATH_MAX];
    char slot_a[HS_SESSION_CATALOG_PATH_MAX];
    char slot_b[HS_SESSION_CATALOG_PATH_MAX];
    hs_session_result_t result = build_paths(root, session->session_id,
                                             directory, slot_a, slot_b);
    if (result != HS_SESSION_OK) return result;
    if (mkdir(directory, 0775) != 0 && errno != EEXIST)
        return HS_SESSION_IO_ERROR;
    return hs_session_save_next(slot_a, slot_b, session);
}

hs_session_result_t hs_session_catalog_list(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    hs_session_catalog_entry_t *out, size_t capacity, size_t *count_out)
{
    if (!root || !*root || !count_out || (capacity && !out))
        return HS_SESSION_RANGE;
    *count_out = 0;
    hs_session_t *session = calloc(1, sizeof(*session));
    if (!session) return HS_SESSION_IO_ERROR;
    bool corrupt = false;
    DIR *directory = opendir(root);
    if (directory) {
        for (;;) {
            errno = 0;
            struct dirent *entry = readdir(directory);
            if (!entry) {
                if (errno != 0) corrupt = true;
                break;
            }
            if (!valid_session_dir_name(entry->d_name)) continue;
            char slot_a[HS_SESSION_CATALOG_PATH_MAX];
            char slot_b[HS_SESSION_CATALOG_PATH_MAX];
            int a_length = snprintf(slot_a, sizeof(slot_a), "%s/%s/active.a",
                                    root, entry->d_name);
            int b_length = snprintf(slot_b, sizeof(slot_b), "%s/%s/active.b",
                                    root, entry->d_name);
            if (a_length < 0 || b_length < 0 ||
                (size_t)a_length >= sizeof(slot_a) ||
                (size_t)b_length >= sizeof(slot_b)) {
                corrupt = true;
                continue;
            }
            hs_session_result_t loaded = hs_session_load_latest(
                slot_a, slot_b, session, NULL);
            if (loaded == HS_SESSION_OK &&
                !directory_matches_session(entry->d_name, session)) {
                corrupt = true;
            } else if (loaded == HS_SESSION_OK &&
                       session->state == HS_SESSION_ACTIVE) {
                insert_entry(out, capacity, count_out, session,
                             newest_mtime(slot_a, slot_b), false);
            } else if (loaded != HS_SESSION_NOT_FOUND &&
                       !(loaded == HS_SESSION_OK &&
                         session->state == HS_SESSION_TOMBSTONE)) {
                corrupt = true;
            }
        }
        closedir(directory);
    } else if (errno != ENOENT) {
        free(session);
        return HS_SESSION_IO_ERROR;
    }

    if (legacy_slot_a && legacy_slot_b) {
        hs_session_result_t loaded = hs_session_load_latest(
            legacy_slot_a, legacy_slot_b, session, NULL);
        if (loaded == HS_SESSION_OK && session->state == HS_SESSION_ACTIVE) {
            char catalog_dir[HS_SESSION_CATALOG_PATH_MAX];
            char catalog_a[HS_SESSION_CATALOG_PATH_MAX];
            char catalog_b[HS_SESSION_CATALOG_PATH_MAX];
            bool catalog_copy =
                build_paths(root, session->session_id, catalog_dir,
                            catalog_a, catalog_b) == HS_SESSION_OK &&
                (path_mtime(catalog_a) != 0 || path_mtime(catalog_b) != 0);
            if (!catalog_copy) {
                insert_entry(out, capacity, count_out, session,
                             newest_mtime(legacy_slot_a, legacy_slot_b), true);
            }
        } else if (loaded != HS_SESSION_NOT_FOUND &&
                   !(loaded == HS_SESSION_OK &&
                     session->state == HS_SESSION_TOMBSTONE)) {
            corrupt = true;
        }
    }
    free(session);
    return corrupt && *count_out == 0 ? HS_SESSION_CORRUPT : HS_SESSION_OK;
}

hs_session_result_t hs_session_catalog_find_id(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    const uint8_t session_id[HS_SESSION_ID_BYTES], hs_session_t *session_out)
{
    if (!root || !session_id || !session_out) return HS_SESSION_RANGE;
    char directory[HS_SESSION_CATALOG_PATH_MAX];
    char slot_a[HS_SESSION_CATALOG_PATH_MAX];
    char slot_b[HS_SESSION_CATALOG_PATH_MAX];
    hs_session_result_t result = build_paths(root, session_id, directory,
                                             slot_a, slot_b);
    if (result != HS_SESSION_OK) return result;
    result = hs_session_load_latest(slot_a, slot_b, session_out, NULL);
    if (result == HS_SESSION_OK) {
        if (!same_id(session_out->session_id, session_id))
            return HS_SESSION_CORRUPT;
        return session_out->state == HS_SESSION_ACTIVE
                   ? HS_SESSION_OK : HS_SESSION_NOT_FOUND;
    }
    if (result != HS_SESSION_NOT_FOUND) return result;
    if (!legacy_slot_a || !legacy_slot_b) return HS_SESSION_NOT_FOUND;
    result = hs_session_load_latest(legacy_slot_a, legacy_slot_b,
                                    session_out, NULL);
    return result == HS_SESSION_OK && session_out->state == HS_SESSION_ACTIVE &&
                   same_id(session_out->session_id, session_id)
               ? HS_SESSION_OK : HS_SESSION_NOT_FOUND;
}

static void consider_wordlist_match(
    const hs_session_t *session, int64_t updated_at,
    uint64_t capture_size, uint32_t capture_crc32,
    const hs_session_wordlist_t *wordlists, size_t wordlist_count,
    hs_session_t *best, int *best_index, int64_t *best_updated,
    bool *matching_capture)
{
    if (!session || session->state != HS_SESSION_ACTIVE) return;
    if (session->capture.size != capture_size ||
        session->capture.crc32 != capture_crc32) {
        return;
    }
    *matching_capture = true;
    int index = hs_session_find_active_wordlist(
        session, capture_size, capture_crc32, wordlists, wordlist_count);
    if (index < 0 || (*best_index >= 0 && updated_at <= *best_updated)) return;
    *best = *session;
    *best_index = index;
    *best_updated = updated_at;
}

hs_session_result_t hs_session_catalog_find_newest_wordlist(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    uint64_t capture_size, uint32_t capture_crc32,
    const hs_session_wordlist_t *wordlists, size_t wordlist_count,
    hs_session_t *session_out, int *wordlist_index_out,
    bool *matching_capture_out)
{
    if (!root || !*root || (!wordlists && wordlist_count) || !session_out ||
        !wordlist_index_out || !matching_capture_out) {
        return HS_SESSION_RANGE;
    }
    *wordlist_index_out = -1;
    *matching_capture_out = false;
    int64_t best_updated = INT64_MIN;
    bool read_failed = false;
    hs_session_t *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return HS_SESSION_IO_ERROR;

    DIR *directory = opendir(root);
    if (directory) {
        for (;;) {
            errno = 0;
            struct dirent *entry = readdir(directory);
            if (!entry) {
                if (errno != 0) read_failed = true;
                break;
            }
            if (!valid_session_dir_name(entry->d_name)) continue;
            char slot_a[HS_SESSION_CATALOG_PATH_MAX];
            char slot_b[HS_SESSION_CATALOG_PATH_MAX];
            int a_length = snprintf(slot_a, sizeof(slot_a), "%s/%s/active.a",
                                    root, entry->d_name);
            int b_length = snprintf(slot_b, sizeof(slot_b), "%s/%s/active.b",
                                    root, entry->d_name);
            if (a_length < 0 || b_length < 0 ||
                (size_t)a_length >= sizeof(slot_a) ||
                (size_t)b_length >= sizeof(slot_b)) {
                continue;
            }
            hs_session_result_t loaded = hs_session_load_latest(
                slot_a, slot_b, candidate, NULL);
            if (loaded == HS_SESSION_OK) {
                if (!directory_matches_session(entry->d_name, candidate)) {
                    continue;
                } else if (candidate->state == HS_SESSION_ACTIVE) {
                    consider_wordlist_match(
                        candidate, newest_mtime(slot_a, slot_b), capture_size,
                        capture_crc32, wordlists, wordlist_count, session_out,
                        wordlist_index_out, &best_updated,
                        matching_capture_out);
                } else if (candidate->state != HS_SESSION_TOMBSTONE) {
                    continue;
                }
            }
        }
        closedir(directory);
    } else if (errno != ENOENT) {
        free(candidate);
        return HS_SESSION_IO_ERROR;
    }

    if (legacy_slot_a && legacy_slot_b) {
        hs_session_result_t loaded = hs_session_load_latest(
            legacy_slot_a, legacy_slot_b, candidate, NULL);
        bool catalog_copy = false;
        if (loaded == HS_SESSION_OK) {
            char catalog_dir[HS_SESSION_CATALOG_PATH_MAX];
            char catalog_a[HS_SESSION_CATALOG_PATH_MAX];
            char catalog_b[HS_SESSION_CATALOG_PATH_MAX];
            if (build_paths(root, candidate->session_id, catalog_dir,
                            catalog_a, catalog_b) == HS_SESSION_OK &&
                (path_mtime(catalog_a) != 0 || path_mtime(catalog_b) != 0)) {
                catalog_copy = true;
            }
        }
        if (loaded == HS_SESSION_OK && !catalog_copy) {
            consider_wordlist_match(
                candidate, newest_mtime(legacy_slot_a, legacy_slot_b),
                capture_size, capture_crc32, wordlists, wordlist_count,
                session_out, wordlist_index_out, &best_updated,
                matching_capture_out);
        }
    }
    free(candidate);
    if (*wordlist_index_out >= 0) return HS_SESSION_OK;
    return read_failed ? HS_SESSION_IO_ERROR : HS_SESSION_NOT_FOUND;
}

hs_session_result_t hs_session_catalog_tombstone(
    const char *root, const char *legacy_slot_a, const char *legacy_slot_b,
    const uint8_t session_id[HS_SESSION_ID_BYTES])
{
    if (!root || !session_id) return HS_SESSION_RANGE;
    char directory[HS_SESSION_CATALOG_PATH_MAX];
    char slot_a[HS_SESSION_CATALOG_PATH_MAX];
    char slot_b[HS_SESSION_CATALOG_PATH_MAX];
    hs_session_result_t result = build_paths(root, session_id, directory,
                                             slot_a, slot_b);
    if (result != HS_SESSION_OK) return result;
    hs_session_t *session = calloc(1, sizeof(*session));
    if (!session) return HS_SESSION_IO_ERROR;
    result = hs_session_load_latest(slot_a, slot_b, session, NULL);
    if (result == HS_SESSION_OK && !same_id(session->session_id, session_id)) {
        free(session);
        return HS_SESSION_CORRUPT;
    }
    if (result == HS_SESSION_OK && session->state == HS_SESSION_ACTIVE) {
        free(session);
        return hs_session_tombstone(slot_a, slot_b, session_id);
    }
    if (result != HS_SESSION_NOT_FOUND) {
        free(session);
        return result;
    }
    if (!legacy_slot_a || !legacy_slot_b) {
        free(session);
        return HS_SESSION_NOT_FOUND;
    }
    result = hs_session_load_latest(legacy_slot_a, legacy_slot_b, session, NULL);
    bool matches = result == HS_SESSION_OK &&
                   session->state == HS_SESSION_ACTIVE &&
                   same_id(session->session_id, session_id);
    free(session);
    return matches ? hs_session_tombstone(
                         legacy_slot_a, legacy_slot_b, session_id)
                   : HS_SESSION_NOT_FOUND;
}
