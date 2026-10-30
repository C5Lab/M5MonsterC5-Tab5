#include "pixie_dust_session.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define START_TIMEOUT_MS 4000U
#define REPLY_TIMEOUT_MS 7000U
#define STOP_TOTAL_MS 18000U
#define STATUS_POLL_MS 1000U

static void secure_zero(void *bytes, size_t length)
{
    volatile unsigned char *p = bytes;
    while (length--) *p++ = 0;
}

static void label(char *target, size_t capacity, const char *value)
{
    snprintf(target, capacity, "%s", value);
}

static bool valid_bssid(const char *text)
{
    if (!text || strlen(text) != 17) return false;
    for (size_t i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (text[i] != ':') return false;
        } else if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }
    char first[3] = {text[0], text[1], '\0'};
    unsigned first_byte = 0;
    if (sscanf(first, "%2x", &first_byte) != 1) return false;
    return (first_byte & 1U) == 0 && first_byte != 0xffU;
}

bool pixie_session_init(pixie_session_t *s, int tab, int port,
                        const char *bssid, bool confirmed_stop_supported)
{
    if (!s || tab < 0 || tab > 2 || port < 0 || !valid_bssid(bssid)) return false;
    memset(s, 0, sizeof(*s));
    s->tab = tab;
    s->port = port;
    memcpy(s->bssid, bssid, 18);
    s->confirmed_stop_supported = confirmed_stop_supported;
    s->phase = PIXIE_STARTING;
    s->start_queued = true;
    label(s->stage, sizeof(s->stage), "Starting");
    return true;
}

static const char *json_key(const char *line, const char *key)
{
    char pattern[48];
    if (snprintf(pattern, sizeof(pattern), "\"%s\":", key) >= (int)sizeof(pattern))
        return NULL;
    const char *cursor = strstr(line, pattern);
    if (!cursor) return NULL;
    cursor += strlen(pattern);
    while (*cursor == ' ') cursor++;
    return cursor;
}

static bool json_u32(const char *line, const char *key, uint32_t *out)
{
    const char *p = json_key(line, key);
    if (!p || !isdigit((unsigned char)*p) || !out) return false;
    uint64_t number = 0;
    do {
        number = number * 10U + (unsigned)(*p++ - '0');
        if (number > UINT32_MAX) return false;
    } while (isdigit((unsigned char)*p));
    if (*p != ',' && *p != '}' && *p != ' ') return false;
    *out = (uint32_t)number;
    return true;
}

static bool json_string(const char *line, const char *key,
                        char *out, size_t capacity)
{
    const char *p = json_key(line, key);
    if (!p || *p != '"' || !out || capacity == 0) return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p++;
        if (n + 1 >= capacity) return false;
        out[n++] = *p++;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    return true;
}

static bool json_true(const char *line, const char *key)
{
    const char *p = json_key(line, key);
    return p && strncmp(p, "true", 4) == 0 &&
           (p[4] == ',' || p[4] == '}');
}

static void uncertain(pixie_session_t *s, const char *stage, const char *result)
{
    s->phase = PIXIE_UNKNOWN;
    s->stop_pending = false;
    s->stop_waiting = false;
    s->status_waiting = false;
    s->stop_queued = false;
    s->status_queued = false;
    s->retry_after_status = false;
    label(s->stage, sizeof(s->stage), stage);
    label(s->result, sizeof(s->result), result);
}

void pixie_session_request_exit(pixie_session_t *s, uint64_t now_ms)
{
    if (!s || s->close_ready) return;
    s->exit_requested = true;
    if (s->start_queued) {
        s->start_queued = false;
        s->start_refused = true;
        s->close_ready = true;
    } else if (s->start_refused) {
        s->close_ready = true;
    } else if (!s->accepted) {
        uncertain(s, "Start unconfirmed", "Monster state unknown; Stop is global");
    } else if (s->stop_confirmed) {
        s->close_ready = true;
    } else if (!s->stop_waiting && !s->status_waiting && !s->stop_pending) {
        /* WPS stop is global and carries no run id. Check that the current
         * service run is still ours before sending it. */
        s->status_queued = true;
        s->retry_after_status = true;
        s->phase = PIXIE_STOPPING;
        label(s->stage, sizeof(s->stage), "Stopping");
        if (!s->stop_started_ms) s->stop_started_ms = now_ms ? now_ms : 1;
    }
}

void pixie_session_request_retry(pixie_session_t *s, uint64_t now_ms)
{
    if (!s || s->close_ready || s->stop_waiting || s->status_waiting) return;
    if (s->stop_confirmed && s->terminal_seen && !s->has_secret) {
        s->reveal_waiting = false;
        s->reveal_queued = true;
        return;
    }
    if (s->phase != PIXIE_UNKNOWN && s->phase != PIXIE_ERROR &&
        s->phase != PIXIE_QUARANTINED) return;
    s->exit_requested = true;
    s->stop_started_ms = now_ms ? now_ms : 1;
    s->phase = PIXIE_STOPPING;
    label(s->stage, sizeof(s->stage), "Stopping");
    if (s->accepted) {
        s->retry_after_status = true;
        s->status_queued = true;
    } else {
        /* Explicit user action is required because wps stop is global. */
        s->stop_queued = true;
    }
}

void pixie_session_tick(pixie_session_t *s, uint64_t now_ms)
{
    if (!s) return;
    if (s->phase == PIXIE_STARTING && s->start_deadline_ms &&
        now_ms >= s->start_deadline_ms && !s->accepted)
        uncertain(s, "No start ACK", "Monster state unknown; Stop is global");

    if (s->stop_waiting && now_ms >= s->reply_deadline_ms)
        uncertain(s, "No stop reply", "Stop unconfirmed");
    else if (s->status_waiting && now_ms >= s->reply_deadline_ms)
        uncertain(s, "No status reply", "Stop unconfirmed");

    if (s->reveal_waiting && now_ms >= s->reveal_deadline_ms) {
        s->reveal_waiting = false;
        s->phase = PIXIE_ERROR;
        label(s->result, sizeof(s->result), "Secret reveal unconfirmed");
    }

    if (s->stop_pending && s->stop_started_ms &&
        now_ms - s->stop_started_ms >= STOP_TOTAL_MS)
        uncertain(s, "Still busy", "Stop unconfirmed");
    else if (s->stop_pending && !s->status_waiting &&
             !s->stop_waiting && now_ms >= s->next_status_ms)
        s->status_queued = true;
}

static bool sensitive_id(const char *line, uint32_t run_id, const char **after)
{
    const char prefix[] = "SENSITIVE operation_id=";
    if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *p = line + sizeof(prefix) - 1;
    if (!isdigit((unsigned char)*p)) return false;
    uint64_t value = 0;
    do {
        value = value * 10U + (unsigned)(*p++ - '0');
        if (value > UINT32_MAX) return false;
    } while (isdigit((unsigned char)*p));
    if (*p != ' ' || value != run_id) return false;
    if (after) *after = p;
    return true;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_key(const char *line, char *out, size_t capacity)
{
    const char *p = strstr(line, "network_key=\"");
    if (!p) return false;
    p += strlen("network_key=\"");
    size_t n = 0;
    while (*p && *p != '"') {
        unsigned char byte = (unsigned char)*p++;
        if (byte == '\\') {
            if (!*p) return false;
            switch (*p) {
            case '"': byte = '"'; p++; break;
            case '\\': byte = '\\'; p++; break;
            case 'n': byte = '\n'; p++; break;
            case 'r': byte = '\r'; p++; break;
            case 't': byte = '\t'; p++; break;
            case 'u': {
                if (!p[1] || !p[2] || !p[3] || !p[4]) return false;
                int h0 = hex_digit(p[1]), h1 = hex_digit(p[2]);
                int h2 = hex_digit(p[3]), h3 = hex_digit(p[4]);
                if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0 || h0 || h1) return false;
                byte = (unsigned char)((h2 << 4) | h3);
                p += 5;
                break;
            }
            default: return false;
            }
        }
        if (n + 1 >= capacity) return false;
        out[n++] = (char)byte;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    return true;
}

static void parse_line(pixie_session_t *s, const char *line, uint64_t now_ms)
{
    char record[32] = {0};
    (void)json_string(line, "record_type", record, sizeof(record));

    if (strcmp(record, "wps_stop") == 0 && s->stop_waiting) {
        char result[24] = {0};
        if (!json_string(line, "result", result, sizeof(result))) return;
        s->stop_waiting = false;
        if (strcmp(result, "stopped") == 0) {
            s->stop_pending = false;
            if (s->phase == PIXIE_QUARANTINED) return;
            if (s->confirmed_stop_supported) {
                s->stop_confirmed = true;
                s->phase = PIXIE_DONE;
                label(s->stage, sizeof(s->stage), "Stopped");
                if (s->exit_requested && !s->reveal_queued &&
                    !s->reveal_waiting) s->close_ready = true;
            } else {
                uncertain(s, "Update JanOS", "Stop unconfirmed on this version");
            }
        } else if (strcmp(result, "stop_pending") == 0) {
            s->stop_pending = true;
            s->phase = PIXIE_STOPPING;
            label(s->stage, sizeof(s->stage), "Stopping");
            s->next_status_ms = now_ms + STATUS_POLL_MS;
        } else if (strcmp(result, "quarantined") == 0) {
            s->stop_pending = false;
            s->phase = PIXIE_QUARANTINED;
            label(s->stage, sizeof(s->stage), "Radio fault");
            label(s->result, sizeof(s->result), "Monster needs recovery");
        }
        return;
    }

    if (strcmp(record, "wps_status") == 0 && s->status_waiting) {
        char phase[24] = {0};
        uint32_t run_id = 0;
        s->status_waiting = false;
        if (!json_u32(line, "run_id", &run_id) || run_id != s->run_id ||
            !json_string(line, "phase", phase, sizeof(phase))) {
            uncertain(s, "Other WPS run", "Stop unconfirmed");
        } else if (strcmp(phase, "quarantined") == 0) {
            s->stop_pending = false;
            s->phase = PIXIE_QUARANTINED;
            label(s->stage, sizeof(s->stage), "Radio fault");
            label(s->result, sizeof(s->result), "Monster needs recovery");
        } else if (s->retry_after_status ||
                   (s->stop_pending && strcmp(phase, "idle") == 0)) {
            s->retry_after_status = false;
            s->stop_pending = false;
            s->stop_queued = true;
        } else if (s->stop_pending) {
            s->next_status_ms = now_ms + STATUS_POLL_MS;
        }
        return;
    }

    if (strcmp(record, "secret_reveal") == 0 && s->accepted &&
        s->reveal_sent && !s->has_secret) {
        char result[16] = {0};
        if (json_string(line, "result", result, sizeof(result)) &&
            (strcmp(result, "empty") == 0 || strcmp(result, "expired") == 0)) {
            s->reveal_waiting = false;
            s->reveal_queued = false;
            label(s->result, sizeof(s->result),
                  strcmp(result, "empty") == 0 ? "Secret unavailable (empty)" :
                                                   "Secret unavailable (expired)");
            if (s->exit_requested && s->stop_confirmed) s->close_ready = true;
        }
        return;
    }

    if (!s->accepted && !s->start_refused &&
        ((strstr(line, "\"code\":\"busy\"") && strstr(line, "radio_lease")) ||
         strstr(line, "Command returned non-zero"))) {
        s->start_refused = true;
        s->phase = PIXIE_ERROR;
        label(s->stage, sizeof(s->stage), "Refused");
        label(s->result, sizeof(s->result), "Radio busy or request refused");
        if (s->exit_requested) s->close_ready = true;
        return;
    }

    if (strcmp(record, "accepted") == 0 && !s->accepted && !s->start_refused) {
        uint32_t run_id = 0;
        char operation[24] = {0};
        if (json_u32(line, "run_id", &run_id) && run_id &&
            json_string(line, "operation", operation, sizeof(operation)) &&
            strcmp(operation, "wps_pixie") == 0) {
            s->accepted = true;
            s->run_id = run_id;
            if (s->exit_requested) {
                s->phase = PIXIE_STOPPING;
                s->status_queued = true;
                s->retry_after_status = true;
                label(s->stage, sizeof(s->stage), "Stopping");
                if (!s->stop_started_ms)
                    s->stop_started_ms = now_ms ? now_ms : 1;
            } else {
                s->phase = PIXIE_CAPTURE;
                label(s->stage, sizeof(s->stage), "Capture");
            }
        }
        return;
    }

    if (s->accepted && !s->exit_requested) {
        char type[24] = {0}, stage[40] = {0};
        uint32_t operation_id = 0;
        if (json_u32(line, "operation_id", &operation_id) &&
            operation_id == s->run_id &&
            json_string(line, "type", type, sizeof(type)) &&
            strcmp(type, "wps_stage") == 0 &&
            json_string(line, "stage", stage, sizeof(stage))) {
            if (strncmp(stage, "pixie_capture", 13) == 0) {
                s->phase = PIXIE_CAPTURE;
                label(s->stage, sizeof(s->stage), "Capture");
            } else if (strcmp(stage, "pixie_done") == 0) {
                s->phase = PIXIE_SEARCH;
                label(s->stage, sizeof(s->stage),
                      json_true(line, "found") ? "Search hit" : "Search");
            } else if (strncmp(stage, "pixie_verify", 12) == 0) {
                s->phase = PIXIE_VERIFY;
                label(s->stage, sizeof(s->stage), "Verify");
            }
            return;
        }
    }

    if (strcmp(record, "terminal") == 0 && s->accepted) {
        uint32_t run_id = 0;
        char operation[48] = {0}, cleanup[24] = {0}, lease[24] = {0};
        if (!json_u32(line, "run_id", &run_id) || run_id != s->run_id ||
            !json_string(line, "operation_result", operation, sizeof(operation))) return;
        s->terminal_seen = true;
        bool restored = json_string(line, "cleanup_result", cleanup, sizeof(cleanup)) &&
                        strcmp(cleanup, "restored") == 0 &&
                        json_string(line, "lease_state", lease, sizeof(lease)) &&
                        strcmp(lease, "released") == 0;
        if (!restored) {
            s->phase = PIXIE_QUARANTINED;
            label(s->stage, sizeof(s->stage), "Radio fault");
            label(s->result, sizeof(s->result), "Cleanup unconfirmed");
        } else if (strcmp(operation, "pixie_pin_and_m7_verified") == 0) {
            label(s->result, sizeof(s->result), "PIN and M7 verified");
            s->reveal_queued = !s->reveal_sent;
            if (!s->exit_requested) {
                s->phase = PIXIE_DONE;
                label(s->stage, sizeof(s->stage), "Done");
            }
        } else if (strcmp(operation, "no_match") == 0 ||
                   strcmp(operation, "partial_match") == 0) {
            label(s->result, sizeof(s->result), "No verified result");
            if (!s->exit_requested) s->phase = PIXIE_DONE;
        } else if (strcmp(operation, "cancelled") == 0) {
            label(s->result, sizeof(s->result), "Cancelled");
            if (!s->exit_requested) s->phase = PIXIE_DONE;
        } else {
            label(s->result, sizeof(s->result), operation);
            if (!s->exit_requested) s->phase = PIXIE_ERROR;
        }
        if (s->exit_requested && s->phase != PIXIE_QUARANTINED &&
            !s->stop_confirmed) {
            s->phase = PIXIE_STOPPING;
            label(s->stage, sizeof(s->stage), "Finishing");
        }
        return;
    }

    if (s->accepted && s->reveal_sent && !s->has_secret &&
        sensitive_id(line, s->run_id, NULL)) {
        const char *pin = strstr(line, "recovered_pin=");
        if (!pin || !decode_key(line, s->network_key, sizeof(s->network_key))) return;
        pin += strlen("recovered_pin=");
        size_t n = 0;
        while (isdigit((unsigned char)pin[n]) && n < 8) n++;
        if (n != 8 || pin[n] != ' ') {
            pixie_session_clear_secret(s);
            return;
        }
        memcpy(s->pin, pin, n);
        s->pin[n] = '\0';
        s->has_secret = true;
        s->reveal_waiting = false;
        s->reveal_queued = false;
        if (s->exit_requested && s->stop_confirmed) s->close_ready = true;
    }
}

void pixie_session_feed(pixie_session_t *s, const char *bytes,
                        size_t length, uint64_t now_ms)
{
    if (!s || !bytes) return;
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)bytes[i];
        if (byte == '\r' || byte == '\n') {
            if (s->line_length && !s->line_overflow) {
                s->line[s->line_length] = '\0';
                parse_line(s, s->line, now_ms);
            }
            secure_zero(s->line, sizeof(s->line));
            s->line_length = 0;
            s->line_overflow = false;
        } else if (!s->line_overflow) {
            if (s->line_length + 1 < sizeof(s->line))
                s->line[s->line_length++] = (char)byte;
            else
                s->line_overflow = true;
        }
    }
}

pixie_command_t pixie_session_take_command(pixie_session_t *s,
                                           pixie_outgoing_t *out,
                                           uint64_t now_ms)
{
    if (!s || !out || s->close_ready) return PIXIE_CMD_NONE;
    pixie_session_tick(s, now_ms);
    pixie_command_t kind = PIXIE_CMD_NONE;
    const char *text = NULL;
    if (s->start_queued) {
        s->start_queued = false;
        kind = PIXIE_CMD_START;
        snprintf(out->text, sizeof(out->text), "wps pixie %s", s->bssid);
    } else if (s->stop_queued && !s->status_waiting && !s->stop_waiting) {
        s->stop_queued = false;
        kind = PIXIE_CMD_STOP;
        text = "wps stop";
    } else if (s->status_queued && !s->status_waiting && !s->stop_waiting) {
        s->status_queued = false;
        kind = PIXIE_CMD_STATUS;
        text = "wps status";
    } else if (s->reveal_queued) {
        s->reveal_queued = false;
        kind = PIXIE_CMD_REVEAL;
        text = "secret reveal";
    }
    if (kind == PIXIE_CMD_NONE) return kind;
    if (text) label(out->text, sizeof(out->text), text);
    out->kind = kind;
    out->tab = s->tab;
    out->port = s->port;
    return kind;
}

void pixie_session_write_result(pixie_session_t *s,
                                pixie_command_t command, bool complete,
                                uint64_t now_ms)
{
    if (!s) return;
    if (!complete) {
        if (command == PIXIE_CMD_START)
            uncertain(s, "Start write failed", "Monster state unknown; Stop is global");
        else if (command == PIXIE_CMD_STOP)
            uncertain(s, "Stop write failed", "Stop unconfirmed");
        else if (command == PIXIE_CMD_STATUS)
            uncertain(s, "Status write failed", "Stop unconfirmed");
        else if (command == PIXIE_CMD_REVEAL)
        {
            /* A partial write may have included the command terminator. */
            s->reveal_sent = true;
            s->phase = PIXIE_ERROR;
            label(s->result, sizeof(s->result), "Secret reveal failed");
        }
        return;
    }
    switch (command) {
    case PIXIE_CMD_START:
        s->start_deadline_ms = now_ms + START_TIMEOUT_MS;
        break;
    case PIXIE_CMD_STOP:
        s->stop_waiting = true;
        s->stop_pending = false;
        s->reply_deadline_ms = now_ms + REPLY_TIMEOUT_MS;
        if (!s->stop_started_ms) s->stop_started_ms = now_ms ? now_ms : 1;
        break;
    case PIXIE_CMD_STATUS:
        s->status_waiting = true;
        s->reply_deadline_ms = now_ms + REPLY_TIMEOUT_MS;
        break;
    case PIXIE_CMD_REVEAL:
        s->reveal_sent = true;
        s->reveal_waiting = true;
        s->reveal_deadline_ms = now_ms + REPLY_TIMEOUT_MS;
        break;
    default: break;
    }
}

void pixie_session_clear_secret(pixie_session_t *s)
{
    if (!s) return;
    secure_zero(s->pin, sizeof(s->pin));
    secure_zero(s->network_key, sizeof(s->network_key));
    secure_zero(s->line, sizeof(s->line));
    s->has_secret = false;
}
