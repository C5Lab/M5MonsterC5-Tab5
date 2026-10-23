#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char saved_policy[48];
    char boot_policy[48];
    char effective_mac[18];
    char current_mac[18];
    bool current_mac_is_null;
    char nvs_state[32];
    bool identity_ready;
    bool restart_required;
} sta_mac_setup_status_t;

/* Returns true only for a complete, valid JanOS STA MAC status response. */
bool sta_mac_setup_parse_status_line(const char *line, sta_mac_setup_status_t *out);

/* Returns true only for a recognized set response. `ok` is false on rejection. */
bool sta_mac_setup_parse_set_line(const char *line, const char *expected_command,
                                  bool *ok,
                                  char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
