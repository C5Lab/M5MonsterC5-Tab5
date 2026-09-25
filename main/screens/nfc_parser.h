#ifndef NFC_PARSER_H
#define NFC_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char type[32];
    char uid[48];
    uint8_t atqa[2];
    uint8_t sak;
    bool have_atqa_sak;
    char idm[32];
    bool have_idm;
    unsigned data_len;
    unsigned num_blocks;
    unsigned block_size;
    bool have_data;
    bool no_card;
    bool detected;
    bool not_detected;
    bool not_initialized;
    bool nothing_to_save;
    bool emulate_failed;
    bool no_card_loaded;
    bool load_failed;
    char saved_path[96];
    bool have_saved;
    char loaded_path[96];
    bool have_loaded;
    char emulating_summary[96];
    bool have_emulating;
    bool emulate_full_ul;
    bool emulate_uid_only;
} nfc_ui_card_t;

typedef struct {
    int idx;
    char name[64];
} nfc_list_entry_t;

void nfc_card_reset(nfc_ui_card_t *c);

/** Parse one UART line into card fields. Returns true if the line was NFC-related. */
bool nfc_parse_card_line(const char *line, nfc_ui_card_t *c);

/** True if line is exactly "[NFC] END" (optional trailing CR). */
bool nfc_line_is_end(const char *line);

/**
 * Parse a library row "N filename.nfc" (no [NFC] tag).
 * Returns true on match; idx is 0-based.
 */
bool nfc_parse_list_entry(const char *line, nfc_list_entry_t *out);

/** Parse "[NFC] N card(s)"; returns true and writes N. */
bool nfc_parse_card_count(const char *line, int *out_n);

/** True if name uses only [A-Za-z0-9_-] (and optionally '.'). */
bool nfc_name_is_valid(const char *name);

/**
 * Format type into `status` and UID / ATQA / SAK / IDm / data / basename
 * into `detail`. Does not handle error states (no_card, load_failed, …).
 */
void nfc_format_card_detail(const nfc_ui_card_t *c,
                            char *status, size_t status_len,
                            char *detail, size_t detail_len);

/** Copy the last path component of `path` into `out`, optionally stripping .nfc. */
void nfc_path_basename(const char *path, char *out, size_t out_len, bool strip_nfc);

bool nfc_type_is_classic(const char *type);
bool nfc_type_is_ultralight(const char *type);

#endif
