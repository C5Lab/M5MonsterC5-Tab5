#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_CRACK_CACHE_PATH_MAX 160
#define HS_CRACK_CACHE_PASSWORD_MAX 64
#define HS_CRACK_GENERIC_VERSION 1U
#define HS_CRACK_CACHE_SCHEMA_VERSION 2U

typedef enum {
    HS_CRACK_CACHE_OK = 0,
    HS_CRACK_CACHE_NOT_FOUND,
    HS_CRACK_CACHE_STALE,
    HS_CRACK_CACHE_INVALID,
    HS_CRACK_CACHE_IO_ERROR,
} hs_crack_cache_result_t;

typedef enum {
    HS_CRACK_ATTEMPT_FOUND = 0,
    HS_CRACK_ATTEMPT_NOTFOUND,
    HS_CRACK_ATTEMPT_ERROR,
} hs_crack_attempt_status_t;

typedef struct {
    uint64_t size;
    uint32_t crc32;
    char ssid[33];
    char bssid[18];
} hs_crack_capture_id_t;

typedef struct {
    char path[HS_CRACK_CACHE_PATH_MAX];
    uint64_t size;
    int64_t mtime;
    uint32_t head_crc32;
    uint32_t tail_crc32;
} hs_crack_wordlist_id_t;

typedef struct {
    hs_crack_capture_id_t capture;
    bool generic;
    uint32_t generic_version;
    hs_crack_wordlist_id_t wordlist;
    hs_crack_attempt_status_t status;
    uint64_t tried;
    uint32_t match_index;
    char password[HS_CRACK_CACHE_PASSWORD_MAX];
} hs_crack_attempt_t;

typedef struct {
    hs_crack_capture_id_t capture;
    hs_crack_wordlist_id_t wordlist;
    uint64_t safe_offset;
    uint64_t tried;
    bool all_mode;
    uint32_t selected_list_index;
    uint64_t updated_sequence;
} hs_crack_resume_t;

typedef struct {
    char path[HS_CRACK_CACHE_PATH_MAX];
    char name[48];
    hs_crack_wordlist_id_t id;
} hs_crack_wordlist_entry_t;

typedef enum {
    HS_CRACK_SOURCE_NONE = 0,
    HS_CRACK_SOURCE_INTERNAL,
    HS_CRACK_SOURCE_WORDLIST,
    HS_CRACK_SOURCE_ALL,
} hs_crack_source_kind_t;

typedef bool (*hs_crack_cache_checkpoint_fn)(void *context);

uint32_t hs_crack_cache_crc32_update(uint32_t crc, const void *data, size_t len);

hs_crack_cache_result_t hs_crack_cache_capture_id(
    const char *path, const char *ssid, const char *bssid,
    hs_crack_capture_id_t *out);
hs_crack_cache_result_t hs_crack_cache_wordlist_id(
    const char *path, hs_crack_wordlist_id_t *out);
hs_crack_cache_result_t hs_crack_cache_file_crc32(
    const char *path, uint32_t *out);
hs_crack_cache_result_t hs_crack_cache_file_crc32_checked(
    const char *path, hs_crack_cache_checkpoint_fn checkpoint,
    void *context, uint32_t *out);
hs_crack_cache_result_t hs_crack_cache_find_full_crc(
    const char *cache_path, const hs_crack_wordlist_id_t *wordlist,
    uint32_t *out);
hs_crack_cache_result_t hs_crack_cache_store_full_crc(
    const char *cache_path, const hs_crack_wordlist_id_t *wordlist,
    uint32_t crc32);

bool hs_crack_cache_capture_equal(
    const hs_crack_capture_id_t *a, const hs_crack_capture_id_t *b);
bool hs_crack_cache_wordlist_equal(
    const hs_crack_wordlist_id_t *a, const hs_crack_wordlist_id_t *b);

hs_crack_cache_result_t hs_crack_cache_find_attempt(
    const char *path, const hs_crack_capture_id_t *capture,
    bool generic, uint32_t generic_version,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_attempt_t *out);
hs_crack_cache_result_t hs_crack_cache_upsert_attempt(
    const char *path, const hs_crack_attempt_t *attempt);

hs_crack_cache_result_t hs_crack_cache_load_resume(
    const char *path, const hs_crack_capture_id_t *capture,
    const hs_crack_wordlist_id_t *wordlist, hs_crack_resume_t *out);
hs_crack_cache_result_t hs_crack_cache_save_resume(
    const char *path, const hs_crack_resume_t *resume);
hs_crack_cache_result_t hs_crack_cache_remove_resume(const char *path);

void hs_crack_cache_sort_wordlists(
    hs_crack_wordlist_entry_t *items, size_t count);
bool hs_crack_cache_wordlist_name_allowed(const char *name);
bool hs_crack_cache_decode_source(unsigned dropdown_index,
                                  size_t wordlist_count,
                                  hs_crack_source_kind_t *kind,
                                  size_t *wordlist_index);
unsigned hs_crack_recommended_worker_count(bool unicore);

unsigned hs_crack_cache_byte_percent(uint64_t offset, uint64_t size);
uint64_t hs_crack_cache_eta_seconds(
    uint64_t offset, uint64_t size, uint64_t processed,
    uint64_t elapsed_us);
bool hs_crack_checkpoint_due(uint64_t completed_since_checkpoint,
                             int64_t elapsed_since_checkpoint_us);

#ifdef __cplusplus
}
#endif
