#include "sta_mac_setup_response.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* JanOS schema v1 uses one flat JSON object per console line. Keep this
 * decoder independent of ESP-IDF so the exact wire contract is host tested. */
#define FIELD_COUNT_MAX 16U
#define KEY_SIZE 32U
#define VALUE_SIZE 64U
#define LINE_SIZE_MAX 1024U

typedef enum { JSON_STRING, JSON_NUMBER, JSON_BOOL, JSON_NULL } json_kind_t;
typedef struct {
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    json_kind_t kind;
} json_field_t;
typedef struct {
    json_field_t fields[FIELD_COUNT_MAX];
    size_t count;
} json_fields_t;

static const char *skip_space(const char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

static bool parse_string(const char **cursor, char *out, size_t out_size)
{
    const char *p = *cursor;
    if (*p++ != '"' || !out || out_size == 0) return false;
    size_t n = 0;
    while (*p && *p != '"') {
        /* No schema-v1 field emits escapes. Refuse them, rather than treating
         * a visually similar escaped key or value as a protocol match. */
        unsigned char c = (unsigned char)*p++;
        if (c < 0x20 || c > 0x7e || c == '\\' || n + 1 >= out_size)
            return false;
        out[n++] = (char)c;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    *cursor = p + 1;
    return true;
}

static bool parse_value(const char **cursor, json_field_t *field)
{
    const char *p = *cursor;
    if (*p == '"') {
        field->kind = JSON_STRING;
        if (!parse_string(&p, field->value, sizeof(field->value))) return false;
    } else if (strncmp(p, "true", 4) == 0 || strncmp(p, "false", 5) == 0) {
        bool truth = *p == 't';
        field->kind = JSON_BOOL;
        strcpy(field->value, truth ? "true" : "false");
        p += truth ? 4 : 5;
    } else if (strncmp(p, "null", 4) == 0) {
        field->kind = JSON_NULL;
        field->value[0] = '\0';
        p += 4;
    } else if (*p >= '0' && *p <= '9') {
        field->kind = JSON_NUMBER;
        size_t n = 0;
        do {
            if (n + 1 >= sizeof(field->value)) return false;
            field->value[n++] = *p++;
        } while (*p >= '0' && *p <= '9');
        field->value[n] = '\0';
    } else {
        return false;
    }
    if (*p != ',' && *p != '}' && !isspace((unsigned char)*p)) return false;
    *cursor = p;
    return true;
}

static bool parse_object(const char *line, json_fields_t *out)
{
    if (!line || !out || strlen(line) > LINE_SIZE_MAX) return false;
    memset(out, 0, sizeof(*out));
    const char *p = skip_space(line);
    if (*p++ != '{') return false;
    p = skip_space(p);
    if (*p == '}') return false;
    for (;;) {
        if (out->count >= FIELD_COUNT_MAX) return false;
        json_field_t *field = &out->fields[out->count];
        if (!parse_string(&p, field->key, sizeof(field->key))) return false;
        for (size_t i = 0; i < out->count; ++i)
            if (strcmp(out->fields[i].key, field->key) == 0) return false;
        p = skip_space(p);
        if (*p++ != ':') return false;
        p = skip_space(p);
        if (!parse_value(&p, field)) return false;
        ++out->count;
        p = skip_space(p);
        if (*p == '}') {
            p = skip_space(p + 1);
            if (*p != '\0') return false;
            return true;
        }
        if (*p++ != ',') return false;
        p = skip_space(p);
    }
}

static const json_field_t *field(const json_fields_t *obj, const char *name)
{
    for (size_t i = 0; i < obj->count; ++i)
        if (strcmp(obj->fields[i].key, name) == 0) return &obj->fields[i];
    return NULL;
}

static const char *string_field(const json_fields_t *obj, const char *name)
{
    const json_field_t *item = field(obj, name);
    return item && item->kind == JSON_STRING ? item->value : NULL;
}

static bool bool_field(const json_fields_t *obj, const char *name, bool *out)
{
    const json_field_t *item = field(obj, name);
    if (!item || item->kind != JSON_BOOL) return false;
    *out = strcmp(item->value, "true") == 0;
    return true;
}

static bool envelope(const json_fields_t *obj, const char *record_type)
{
    const json_field_t *version = field(obj, "schema_version");
    const char *type = string_field(obj, "record_type");
    return version && version->kind == JSON_NUMBER &&
        strcmp(version->value, "1") == 0 && type &&
        strcmp(type, record_type) == 0;
}

static bool policy_mode(const char *mode)
{
    return mode && (strcmp(mode, "factory") == 0 ||
                    strcmp(mode, "random") == 0 ||
                    strcmp(mode, "fixed") == 0);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool mac_text(const char *text, char out[18], bool require_local)
{
    if (!text || strlen(text) != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        bool separator = i == 2 || i == 5 || i == 8 || i == 11 || i == 14;
        if (separator) {
            if (text[i] != ':') return false;
            out[i] = ':';
        } else {
            int digit = hex_digit(text[i]);
            if (digit < 0) return false;
            out[i] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        }
    }
    out[17] = '\0';
    int first = hex_digit(text[0]) * 16 + hex_digit(text[1]);
    return !require_local || (first & 3) == 2;
}

static bool health_value(const char *text)
{
    return text && (strcmp(text, "valid") == 0 ||
                    strcmp(text, "missing") == 0 ||
                    strcmp(text, "invalid_record") == 0 ||
                    strcmp(text, "unsupported_version") == 0 ||
                    strcmp(text, "read_error") == 0);
}

bool sta_mac_setup_parse_status_line(const char *line, sta_mac_setup_status_t *out)
{
    json_fields_t obj;
    if (!out || !parse_object(line, &obj) || !envelope(&obj, "sta_mac_status"))
        return false;
    const json_field_t *configured = field(&obj, "configured_mode");
    const json_field_t *configured_mac = field(&obj, "configured_mac");
    const json_field_t *active = field(&obj, "active_sta_mac");
    const char *boot = string_field(&obj, "boot_mode");
    const char *effective = string_field(&obj, "effective_boot_mac");
    const char *health = string_field(&obj, "record_health");
    bool ready, pending;
    if (!configured || !configured_mac || !active || !policy_mode(boot) ||
        !health_value(health) || !bool_field(&obj, "identity_ready", &ready) ||
        !bool_field(&obj, "pending_reboot", &pending)) return false;

    sta_mac_setup_status_t status = {0};
    if (!mac_text(effective, status.effective_mac, false)) return false;
    if (active->kind == JSON_NULL) {
        status.current_mac_is_null = true;
    } else if (active->kind != JSON_STRING ||
               !mac_text(active->value, status.current_mac, false)) {
        return false;
    }
    const char *mode = NULL;
    if (configured->kind == JSON_NULL) {
        if (strcmp(health, "read_error") != 0) return false;
        mode = "unknown";
    } else if (configured->kind == JSON_STRING && policy_mode(configured->value)) {
        mode = configured->value;
    } else {
        return false;
    }
    if (strcmp(mode, "fixed") == 0) {
        char fixed[18];
        if (configured_mac->kind != JSON_STRING ||
            !mac_text(configured_mac->value, fixed, true)) return false;
        if (snprintf(status.saved_policy, sizeof(status.saved_policy),
                     "fixed %s", fixed) >= (int)sizeof(status.saved_policy)) return false;
    } else {
        if (configured_mac->kind != JSON_NULL) return false;
        if (strlen(mode) >= sizeof(status.saved_policy)) return false;
        strcpy(status.saved_policy, mode);
    }
    if (strlen(boot) >= sizeof(status.boot_policy) ||
        strlen(health) >= sizeof(status.nvs_state)) return false;
    strcpy(status.boot_policy, boot);
    strcpy(status.nvs_state, health);
    status.identity_ready = ready;
    status.restart_required = pending;
    *out = status;
    return true;
}

static void copy_error(char *output, size_t size, const char *message)
{
    snprintf(output, size, "%s", message);
}

bool sta_mac_setup_parse_set_line(const char *line, const char *expected_command,
                                  bool *ok,
                                  char *error, size_t error_size)
{
    if (!line || !expected_command || !ok || !error || error_size == 0) return false;
    const char *p = skip_space(line);
    bool admission = strncmp(p, "[WFACTL1] ", 10) == 0;
    if (admission) p += 10;
    json_fields_t obj;
    if (!parse_object(p, &obj)) return false;
    if (admission) {
        const json_field_t *version = field(&obj, "v");
        const char *type = string_field(&obj, "type");
        const char *command = string_field(&obj, "command");
        const char *code = string_field(&obj, "code");
        const char *reason = string_field(&obj, "reason");
        if (!version || version->kind != JSON_NUMBER ||
            strcmp(version->value, "1") != 0 || !type ||
            strcmp(type, "error") != 0 || !command ||
            strcmp(command, "console") != 0 || !code ||
            strcmp(code, "busy") != 0 || !reason || !*reason) return false;
        *ok = false;
        copy_error(error, error_size, reason);
        return true;
    }
    if (!envelope(&obj, "sta_mac_result")) return false;
    const char *result = string_field(&obj, "result");
    if (!result) return false;
    if (strcmp(result, "set") == 0) {
        bool after_reboot;
        const char *mode = string_field(&obj, "configured_mode");
        const json_field_t *mac = field(&obj, "configured_mac");
        if (!policy_mode(mode) || !mac ||
            !bool_field(&obj, "applies_after_reboot", &after_reboot) ||
            !after_reboot) return false;
        char normalized[18];
        char acknowledged_command[64];
        if (strcmp(mode, "fixed") == 0) {
            if (mac->kind != JSON_STRING ||
                !mac_text(mac->value, normalized, true)) return false;
            if (snprintf(acknowledged_command, sizeof(acknowledged_command),
                         "sta_mac set fixed %s", normalized) >=
                (int)sizeof(acknowledged_command)) return false;
        } else if (mac->kind != JSON_NULL) {
            return false;
        } else if (snprintf(acknowledged_command, sizeof(acknowledged_command),
                            "sta_mac set %s", mode) >=
                   (int)sizeof(acknowledged_command)) {
            return false;
        }
        if (strcmp(expected_command, acknowledged_command) != 0) return false;
        *ok = true;
        error[0] = '\0';
        return true;
    }
    if (strcmp(result, "store_error") == 0) {
        const char *detail = string_field(&obj, "error");
        if (!detail || !*detail) return false;
        *ok = false;
        copy_error(error, error_size, detail);
        return true;
    }
    if (strcmp(result, "invalid_mac") == 0 || strcmp(result, "usage") == 0) {
        *ok = false;
        copy_error(error, error_size, result);
        return true;
    }
    return false;
}
