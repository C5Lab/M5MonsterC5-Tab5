// Pure Monster OTA / JanOS RF logic for the Tab5 remote. See ota_rf.h.

#include "ota_rf.h"

#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

static void copy_token(const char *src, char *out, size_t out_sz)
{
    // Copy up to the first whitespace, bounded by out_sz.
    size_t i = 0;
    while (src[i] && src[i] != ' ' && src[i] != '\t' && i + 1 < out_sz) {
        out[i] = src[i];
        i++;
    }
    out[i] = '\0';
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// ---------------------------------------------------------------------------
// version parsing (ported verbatim from projectZero/ESP32C5/main/main.c so the
// Tab5 accepts/rejects exactly what the firmware will)
// ---------------------------------------------------------------------------

static bool ota_version_identifiers(const char **cursor, bool prerelease)
{
    const char *p = *cursor;
    do {
        const char *start = p;
        bool numeric = true;
        while ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= 'a' && *p <= 'z') || *p == '-') {
            if (*p < '0' || *p > '9') numeric = false;
            p++;
        }
        if (p == start || (prerelease && numeric && p - start > 1 && *start == '0')) {
            return false;
        }
        if (*p != '.') break;
        p++;
    } while (true);
    *cursor = p;
    return true;
}

bool ota_rf_parse_version(const char *version, int *major, int *minor, int *patch)
{
    if (!version || !major || !minor || !patch) {
        return false;
    }

    if (*version == 'v' || *version == 'V') version++;
    int parts[3] = {0};
    for (int i = 0; i < 3; i++) {
        const char *start = version;
        if (*version < '0' || *version > '9') return false;
        while (*version >= '0' && *version <= '9') {
            int digit = *version++ - '0';
            if (parts[i] > (INT_MAX - digit) / 10) return false;
            parts[i] = parts[i] * 10 + digit;
        }
        if (version - start > 1 && *start == '0') return false;
        if (i < 2 && *version++ != '.') return false;
    }
    if (*version == '-') {
        version++;
        if (!ota_version_identifiers(&version, true)) return false;
    }
    if (*version == '+') {
        version++;
        if (!ota_version_identifiers(&version, false)) return false;
    }
    if (*version) return false;
    *major = parts[0];
    *minor = parts[1];
    *patch = parts[2];
    return true;
}

// Compare the pre-release portion of two versions that have already parsed.
// A version with a pre-release ranks below one without. Returns <0, 0 or >0.
static int ota_version_prerelease_compare(const char *left, const char *right)
{
    left += strcspn(left, "-+");
    right += strcspn(right, "-+");
    bool left_pre = *left == '-', right_pre = *right == '-';
    if (!left_pre || !right_pre) return right_pre - left_pre;
    left++;
    right++;
    for (;;) {
        size_t ll = strcspn(left, ".+"), rl = strcspn(right, ".+");
        bool ln = strspn(left, "0123456789") == ll;
        bool rn = strspn(right, "0123456789") == rl;
        if (ln != rn) return ln ? -1 : 1;
        if (ln && ll != rl) return ll < rl ? -1 : 1;
        int cmp = strncmp(left, right, ll < rl ? ll : rl);
        if (cmp) return cmp < 0 ? -1 : 1;
        if (ll != rl) return ll < rl ? -1 : 1;
        left += ll;
        right += rl;
        bool more_left = *left == '.', more_right = *right == '.';
        if (!more_left || !more_right) return more_left - more_right;
        left++;
        right++;
    }
}

// Skip an optional leading 'v'/'V' so the pre-release comparator sees the same
// offsets ota_rf_parse_version validated.
static const char *strip_v(const char *s)
{
    if (s && (*s == 'v' || *s == 'V')) return s + 1;
    return s;
}

ota_ver_cmp_t ota_rf_compare_versions(const char *current, const char *candidate)
{
    int c_maj, c_min, c_pat, n_maj, n_min, n_pat;
    if (!ota_rf_parse_version(current, &c_maj, &c_min, &c_pat) ||
        !ota_rf_parse_version(candidate, &n_maj, &n_min, &n_pat)) {
        return OTA_CMP_UNPARSEABLE;
    }
    if (n_maj != c_maj) return n_maj > c_maj ? OTA_CMP_NEWER : OTA_CMP_OLDER;
    if (n_min != c_min) return n_min > c_min ? OTA_CMP_NEWER : OTA_CMP_OLDER;
    if (n_pat != c_pat) return n_pat > c_pat ? OTA_CMP_NEWER : OTA_CMP_OLDER;
    int pre = ota_version_prerelease_compare(strip_v(candidate), strip_v(current));
    if (pre > 0) return OTA_CMP_NEWER;
    if (pre < 0) return OTA_CMP_OLDER;
    return OTA_CMP_SAME;
}

// ---------------------------------------------------------------------------
// ota_info report
// ---------------------------------------------------------------------------

void ota_info_report_reset(ota_info_report_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->layout = OTA_RF_LAYOUT_UNKNOWN;
    r->running_state = -1;
    for (int i = 0; i < 2; i++) {
        r->slots[i].state = -1;
        snprintf(r->slots[i].label, sizeof(r->slots[i].label), "ota_%d", i);
    }
}

static int slot_index_from_label(const char *label)
{
    if (!label) return -1;
    if (strcmp(label, "ota_0") == 0) return 0;
    if (strcmp(label, "ota_1") == 0) return 1;
    return -1;
}

// Find "key" and copy the following whitespace-delimited token into out.
static bool copy_kv(const char *line, const char *key, char *out, size_t out_sz)
{
    const char *p = strstr(line, key);
    if (!p) return false;
    p = skip_ws(p + strlen(key));
    copy_token(p, out, out_sz);
    return out[0] != '\0';
}

bool ota_info_report_feed_line(ota_info_report_t *r, const char *line)
{
    if (!r || !line) return false;

    if (strncmp(line, "OTA default source:", 19) == 0) {
        r->has_default_source = true;
        return true;
    }
    if (strncmp(line, "OTA RF source:", 14) == 0) {
        r->has_rf_source = true;
        return true;
    }
    if (strncmp(line, "OTA partition table offset:", 27) == 0) {
        const char *p = skip_ws(line + 27);
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 0);
        if (end != p) {
            r->table_offset = (uint32_t)v;
            r->have_offset = true;
        }
        return true;
    }
    if (strncmp(line, "OTA RF layout:", 14) == 0) {
        const char *p = skip_ws(line + 14);
        if (strncmp(p, "compatible", 10) == 0) {
            r->layout = OTA_RF_LAYOUT_COMPATIBLE;
        } else if (strncmp(p, "incompatible", 12) == 0) {
            r->layout = OTA_RF_LAYOUT_INCOMPATIBLE;
        } else {
            r->layout = OTA_RF_LAYOUT_UNKNOWN;
        }
        return true;
    }
    if (strncmp(line, "OTA boot:", 9) == 0) {
        copy_token(skip_ws(line + 9), r->boot, sizeof(r->boot));
        return true;
    }
    if (strncmp(line, "OTA running:", 12) == 0) {
        const char *p = skip_ws(line + 12);
        copy_token(p, r->running, sizeof(r->running));
        char st[16] = "";
        if (copy_kv(line, "state=", st, sizeof(st))) r->running_state = atoi(st);
        return true;
    }
    if (strncmp(line, "OTA next:", 9) == 0) {
        copy_token(skip_ws(line + 9), r->next, sizeof(r->next));
        return true;
    }
    if (strncmp(line, "APP[", 4) == 0) {
        const char *colon = strchr(line, ':');
        if (!colon) return true;
        char label[16] = "";
        copy_token(skip_ws(colon + 1), label, sizeof(label));
        int idx = slot_index_from_label(label);
        if (idx < 0) return true;
        ota_info_slot_t *s = &r->slots[idx];
        s->present = true;
        snprintf(s->label, sizeof(s->label), "%s", label);
        if (strstr(line, "missing")) {
            s->present = false;
            return true;
        }
        char st[16] = "";
        if (copy_kv(line, "state=", st, sizeof(st))) s->state = atoi(st);
        s->have_ver = copy_kv(line, "ver=", s->ver, sizeof(s->ver));
        return true;
    }
    return false;
}

bool ota_info_supports_rf_commands(const ota_info_report_t *r)
{
    // The RF source line is emitted by any updater new enough to understand the
    // rf argument - including a classic build. Its presence proves capability,
    // never that the physical board is an RF board.
    return r && r->has_rf_source;
}

// ---------------------------------------------------------------------------
// release list line
// ---------------------------------------------------------------------------

bool ota_release_parse_line(const char *line, ota_release_t *out)
{
    if (!line || !out) return false;
    memset(out, 0, sizeof(*out));
    out->index = -1;

    // "OTA[<idx>]: <tag> (<channel>) <date> <title>"
    int idx = -1;
    int consumed = 0;
    if (sscanf(line, "OTA[%d]:%n", &idx, &consumed) != 1 || consumed <= 0) {
        return false;
    }
    out->index = idx;
    const char *p = skip_ws(line + consumed);

    // tag (up to the next space)
    size_t i = 0;
    while (*p && *p != ' ' && i + 1 < sizeof(out->tag)) out->tag[i++] = *p++;
    out->tag[i] = '\0';
    if (out->tag[0] == '\0' || (*p && *p != ' ')) return false; /* Refuse truncated tags. */
    p = skip_ws(p);

    // (channel)
    if (*p == '(') {
        p++;
        i = 0;
        while (*p && *p != ')' && i + 1 < sizeof(out->channel)) out->channel[i++] = *p++;
        out->channel[i] = '\0';
        if (*p == ')') p++;
        p = skip_ws(p);
    }

    // date (token)
    i = 0;
    while (*p && *p != ' ' && i + 1 < sizeof(out->date)) out->date[i++] = *p++;
    out->date[i] = '\0';
    p = skip_ws(p);

    // title (rest of line)
    snprintf(out->title, sizeof(out->title), "%s", p);
    out->ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// command builders
// ---------------------------------------------------------------------------

static bool put(char *out, size_t out_sz, const char *s)
{
    if (!out || out_sz == 0) return false;
    int n = snprintf(out, out_sz, "%s", s);
    return n >= 0 && (size_t)n < out_sz;
}

bool ota_rf_build_list_cmd(char *out, size_t out_sz)     { return put(out, out_sz, "ota_list rf"); }
bool ota_classic_build_list_cmd(char *out, size_t out_sz){ return put(out, out_sz, "ota_list"); }

bool ota_rf_build_check_latest_cmd(char *out, size_t out_sz, bool explicit_latest)
{
    return put(out, out_sz, explicit_latest ? "ota_check rf latest" : "ota_check rf");
}

bool ota_rf_build_check_tag_cmd(char *out, size_t out_sz, const char *tag)
{
    if (!out || out_sz == 0 || !ota_release_tag_valid(tag)) return false;
    int n = snprintf(out, out_sz, "ota_check rf %s", tag);
    return n >= 0 && (size_t)n < out_sz;
}

bool ota_classic_build_check_cmd(char *out, size_t out_sz, const char *tag_or_null)
{
    if (!out || out_sz == 0) return false;
    int n;
    if (tag_or_null && tag_or_null[0]) {
        if (strcmp(tag_or_null, "latest") && !ota_release_tag_valid(tag_or_null)) return false;
        n = snprintf(out, out_sz, "ota_check %s", tag_or_null);
    } else {
        n = snprintf(out, out_sz, "ota_check");
    }
    return n >= 0 && (size_t)n < out_sz;
}

bool ota_release_tag_valid(const char *tag)
{
    if (!tag || !tag[0] || strlen(tag) >= 48) return false;
    for (const unsigned char *p = (const unsigned char *)tag; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-' || *p == '+'))
            return false;
    /* The updater recognizes CLI keywords case-insensitively. A release tag
     * called RF must never silently change a classic command's source. */
    char keyword[7] = {0};
    size_t length = strlen(tag);
    if (length <= 6) {
        for (size_t i = 0; i < length; ++i) keyword[i] = (char)tolower((unsigned char)tag[i]);
        if (!strcmp(keyword, "latest") || !strcmp(keyword, "rf")) return false;
    }
    return true;
}

bool ota_install_route_allowed(const ota_info_report_t *r, bool use_rf, bool subghz_seen)
{
    // A timeout/empty response cannot be interpreted as classic firmware.
    bool received = r && (r->has_default_source || r->have_offset);
    if (r && r->running[0]) {
        for (size_t i = 0; i < 2; ++i)
            if (r->slots[i].present && !strcmp(r->slots[i].label, r->running)) received = true;
    }
    if (!received) return false;
    bool rf_offset = r && r->have_offset && r->table_offset == OTA_RF_TABLE_OFFSET_RF;
    bool rf_layout = r && r->layout == OTA_RF_LAYOUT_COMPATIBLE;
    if (!use_rf) return !subghz_seen && !rf_offset && !rf_layout;
    if (r && (r->layout == OTA_RF_LAYOUT_INCOMPATIBLE ||
              (r->have_offset && r->table_offset != OTA_RF_TABLE_OFFSET_RF))) return false;
    return subghz_seen || (r && r->has_rf_source && rf_layout);
}

const char *ota_effective_running_version(const char *detected, const ota_info_report_t *r)
{
    int major, minor, patch;
    if (ota_rf_parse_version(detected, &major, &minor, &patch)) return detected;
    if (r && r->running[0]) {
        for (size_t i = 0; i < 2; ++i)
            if (r->slots[i].present && r->slots[i].have_ver &&
                !strcmp(r->slots[i].label, r->running)) return r->slots[i].ver;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// install decision
// ---------------------------------------------------------------------------

ota_decision_t ota_rf_decide_install(const ota_info_report_t *r,
                                     bool updater_supports_rf,
                                     const char *current,
                                     const char *tag,
                                     bool explicit_latest,
                                     bool manual_override)
{
    // Capability gate first: without the rf-aware updater we cannot prove RF
    // command support, so automatic routing must not fall back to the classic
    // repo. A manual override tolerates this - some RF firmware does not report
    // its RF support in ota_info - and lets the C5 firmware answer.
    if (!manual_override && !updater_supports_rf) return OTA_DECIDE_BLOCK_NO_CAPABILITY;

    // Layout gate. A positively incompatible layout is always a hard block: a
    // classic 0x8000 table cannot take an RF image. An UNKNOWN layout is not
    // assumed compatible automatically, but a manual override may proceed (the
    // C5 firmware still verifies the layout and the release files itself).
    ota_rf_layout_t layout = r ? r->layout : OTA_RF_LAYOUT_UNKNOWN;
    if (layout == OTA_RF_LAYOUT_INCOMPATIBLE) return OTA_DECIDE_BLOCK_INCOMPATIBLE;
    if (!manual_override && layout != OTA_RF_LAYOUT_COMPATIBLE)
        return OTA_DECIDE_BLOCK_UNKNOWN_LAYOUT;

    // Latest path: firmware installs only if newer, so no confirm is needed.
    bool have_tag = tag && tag[0] != '\0';
    if (!have_tag) {
        (void)explicit_latest;
        return OTA_DECIDE_ALLOW_LATEST;
    }

    // Explicit tag: must be a valid version (matches firmware rejection of
    // non-version / dev tags for RF).
    int a, b, c;
    if (!ota_rf_parse_version(tag, &a, &b, &c)) return OTA_DECIDE_BLOCK_BAD_TAG;

    // Compare against the running version to surface reinstall/downgrade, which
    // are never started automatically.
    ota_ver_cmp_t cmp = ota_rf_compare_versions(current, tag);
    switch (cmp) {
        case OTA_CMP_SAME:  return OTA_DECIDE_CONFIRM_REINSTALL;
        case OTA_CMP_OLDER: return OTA_DECIDE_CONFIRM_DOWNGRADE;
        case OTA_CMP_NEWER: return OTA_DECIDE_ALLOW_TAG;
        case OTA_CMP_UNPARSEABLE:
        default:
            // Tag itself parsed; the running version is unknown/unparseable. We
            // cannot prove it is newer, so require explicit confirmation rather
            // than silently installing.
            return OTA_DECIDE_CONFIRM_REINSTALL;
    }
}

bool ota_rf_auto_should_use_rf(const ota_info_report_t *r, bool subghz_seen)
{
    // A live [SUBGHZ_STATUS] reply means RF firmware is actually running - the
    // subghz command exists only in RF firmware - and RF firmware can only have
    // booted from the RF partition layout. That is a sufficient positive RF
    // signal on its own, even when this firmware's ota_info omits the RF lines
    // (as some janosrf builds do). Its ABSENCE still proves nothing (an RF board
    // may carry classic firmware), so it never forces the classic path.
    if (subghz_seen) return true;
    // Otherwise route RF only on what ota_info positively proved.
    return ota_info_supports_rf_commands(r) && r && r->layout == OTA_RF_LAYOUT_COMPATIBLE;
}

// ---------------------------------------------------------------------------
// async line reassembler
// ---------------------------------------------------------------------------

void ota_line_asm_reset(ota_line_asm_t *a)
{
    if (!a) return;
    a->len = 0;
    a->buf[0] = '\0';
    a->overflow = false;
}

void ota_line_asm_feed(ota_line_asm_t *a, const char *data, int n,
                       void (*sink)(void *user, const char *line), void *user)
{
    if (!a || !data) return;
    for (int i = 0; i < n; i++) {
        char ch = data[i];
        if (ch == '\r' || ch == '\n') {
            if (a->overflow) {
                // A too-long line was discarded; the terminator just closes it.
                a->overflow = false;
                a->len = 0;
                a->buf[0] = '\0';
                continue;
            }
            if (a->len > 0) {
                a->buf[a->len] = '\0';
                if (sink) sink(user, a->buf);
                a->len = 0;
                a->buf[0] = '\0';
            }
            continue;
        }
        if (a->overflow) continue;  // still swallowing an over-long line
        if (a->len < (int)sizeof(a->buf) - 1) {
            a->buf[a->len++] = ch;
        } else {
            // Refuse to truncate: drop the whole line to avoid a false record.
            a->overflow = true;
            a->len = 0;
            a->buf[0] = '\0';
        }
    }
}

// ---------------------------------------------------------------------------
// per-device capability cache + operation guard
// ---------------------------------------------------------------------------

void ota_rf_state_reset(ota_rf_state_t *s)
{
    if (!s) return;
    s->bound_tab = -1;
    s->op_in_progress = false;
    s->updater_supports_rf = false;
    s->layout = OTA_RF_LAYOUT_UNKNOWN;
    s->variant = OTA_VARIANT_UNKNOWN;
}

void ota_rf_state_bind_device(ota_rf_state_t *s, int tab)
{
    if (!s) return;
    if (s->bound_tab != tab) {
        // Device changed: clear cached detection so it cannot leak across a swap.
        s->updater_supports_rf = false;
        s->layout = OTA_RF_LAYOUT_UNKNOWN;
        s->variant = OTA_VARIANT_UNKNOWN;
        s->bound_tab = tab;
    }
}

bool ota_rf_state_begin_op(ota_rf_state_t *s)
{
    if (!s) return false;
    if (s->op_in_progress) return false;
    s->op_in_progress = true;
    return true;
}

void ota_rf_state_end_op(ota_rf_state_t *s)
{
    if (s) s->op_in_progress = false;
}
