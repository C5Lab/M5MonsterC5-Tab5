#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PIXIE_STARTING,
    PIXIE_CAPTURE,
    PIXIE_SEARCH,
    PIXIE_VERIFY,
    PIXIE_DONE,
    PIXIE_ERROR,
    PIXIE_UNKNOWN,
    PIXIE_STOPPING,
    PIXIE_QUARANTINED,
} pixie_phase_t;

typedef enum {
    PIXIE_CMD_NONE,
    PIXIE_CMD_START,
    PIXIE_CMD_STOP,
    PIXIE_CMD_STATUS,
    PIXIE_CMD_REVEAL,
} pixie_command_t;

typedef struct {
    pixie_command_t kind;
    int tab;
    int port;
    char text[40];
} pixie_outgoing_t;

typedef struct {
    int tab;
    int port;
    char bssid[18];
    bool confirmed_stop_supported;
    pixie_phase_t phase;
    uint32_t run_id;
    bool accepted;
    bool exit_requested;
    bool start_refused;
    bool stop_confirmed;
    bool close_ready;
    bool terminal_seen;
    bool has_secret;
    char stage[32];
    char result[64];
    char pin[10];
    char network_key[65];

    bool start_queued;
    bool stop_queued;
    bool status_queued;
    bool reveal_queued;
    bool stop_waiting;
    bool status_waiting;
    bool reveal_sent;
    bool reveal_waiting;
    bool stop_pending;
    bool retry_after_status;
    uint64_t start_deadline_ms;
    uint64_t reply_deadline_ms;
    uint64_t stop_started_ms;
    uint64_t next_status_ms;
    uint64_t reveal_deadline_ms;
    char line[512];
    size_t line_length;
    bool line_overflow;
} pixie_session_t;

bool pixie_session_init(pixie_session_t *session, int tab, int port,
                        const char *bssid, bool confirmed_stop_supported);
void pixie_session_request_exit(pixie_session_t *session, uint64_t now_ms);
void pixie_session_request_retry(pixie_session_t *session, uint64_t now_ms);
void pixie_session_tick(pixie_session_t *session, uint64_t now_ms);
void pixie_session_feed(pixie_session_t *session, const char *bytes,
                        size_t length, uint64_t now_ms);
pixie_command_t pixie_session_take_command(pixie_session_t *session,
                                           pixie_outgoing_t *out,
                                           uint64_t now_ms);
void pixie_session_write_result(pixie_session_t *session,
                                pixie_command_t command, bool complete,
                                uint64_t now_ms);
void pixie_session_clear_secret(pixie_session_t *session);
