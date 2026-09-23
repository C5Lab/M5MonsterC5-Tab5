#ifndef HS_CRACK_STATE_H
#define HS_CRACK_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Insert or replace one aggregate crack-state row.  NULL method/password
 * fields and a non-positive size preserve the value already stored for the
 * same filename.  Existing legacy unquoted rows are accepted and normalized
 * when they can be parsed safely.
 */
bool hs_crack_state_upsert_file(
    const char *csv_path, const char *temporary_path,
    const char *filename, const char *ssid, int64_t size,
    const char *generic, const char *wordlist, const char *password);

#endif
