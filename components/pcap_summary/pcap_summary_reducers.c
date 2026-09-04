#include "pcap_summary_reducers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- keys --- */

static char ascii_lower(char value)
{
    return (value >= 'A' && value <= 'Z') ? (char)(value + ('a' - 'A')) : value;
}

static bool is_blank(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool is_printable(char value)
{
    unsigned char raw = (unsigned char)value;
    return raw >= 0x21U && raw <= 0x7EU;
}

static bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

static bool is_hex(char value)
{
    return is_digit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

/* strnlen() is POSIX rather than C11, and this file is also built in strict C
 * mode by the host tests. */
static size_t bounded_length(const char *text, size_t maximum)
{
    size_t length = 0;
    while (length < maximum && text[length] != '\0') length++;
    return length;
}

static void key_clear(char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
}

/* Copies [begin, end) lowercased. Fails instead of truncating: a clipped key
 * would silently merge two different observations. */
static bool key_emit(const char *begin, const char *end, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    key_clear(out, out_size);
    if (!begin || end <= begin) return false;
    size_t length = (size_t)(end - begin);
    if (length >= out_size) return false;
    for (size_t i = 0; i < length; i++) {
        out[i] = ascii_lower(begin[i]);
    }
    out[length] = '\0';
    return true;
}

static void key_trim(const char *input, const char **begin_out, const char **end_out)
{
    const char *begin = input ? input : "";
    const char *end = begin + strlen(begin);
    while (begin < end && is_blank(*begin)) begin++;
    while (end > begin && is_blank(end[-1])) end--;
    *begin_out = begin;
    *end_out = end;
}

static bool key_all_printable(const char *begin, const char *end)
{
    for (const char *cursor = begin; cursor < end; cursor++) {
        if (!is_printable(*cursor)) return false;
    }
    return true;
}

bool pcap_key_domain(const char *input, char *out, size_t out_size)
{
    key_clear(out, out_size);
    const char *begin = NULL;
    const char *end = NULL;
    key_trim(input, &begin, &end);
    while (end > begin && end[-1] == '.') end--;   /* the root dot is not a label */
    if (end <= begin || !key_all_printable(begin, end)) return false;
    if (end - begin == 1 && *begin == '.') return false;
    return key_emit(begin, end, out, out_size);
}

/* Strips one "[...]" wrapper and/or a trailing ":port" from [begin, end). */
static void key_strip_port(const char **begin_io, const char **end_io)
{
    const char *begin = *begin_io;
    const char *end = *end_io;
    if (end > begin && *begin == '[') {
        const char *closing = begin + 1;
        while (closing < end && *closing != ']') closing++;
        if (closing < end) {
            *begin_io = begin + 1;
            *end_io = closing;
            return;
        }
    }
    const char *colon = NULL;
    size_t colons = 0;
    for (const char *cursor = begin; cursor < end; cursor++) {
        if (*cursor == ':') {
            colon = cursor;
            colons++;
        }
    }
    if (colons != 1 || !colon || colon + 1 >= end) return;   /* bare IPv6 keeps its colons */
    for (const char *cursor = colon + 1; cursor < end; cursor++) {
        if (!is_digit(*cursor)) return;
    }
    *end_io = colon;
}

bool pcap_key_sni(const char *input, char *out, size_t out_size)
{
    key_clear(out, out_size);
    const char *begin = NULL;
    const char *end = NULL;
    key_trim(input, &begin, &end);
    key_strip_port(&begin, &end);
    char trimmed[PCAP_KEY_TEXT_MAX];
    if (end <= begin || (size_t)(end - begin) >= sizeof(trimmed)) return false;
    memcpy(trimmed, begin, (size_t)(end - begin));
    trimmed[end - begin] = '\0';
    return pcap_key_domain(trimmed, out, out_size);
}

/* aa:bb:cc:dd:ee:ff or aa-bb-cc-dd-ee-ff */
static bool key_is_mac(const char *begin, const char *end)
{
    if (end - begin != 17) return false;
    for (int i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (begin[i] != ':' && begin[i] != '-') return false;
        } else if (!is_hex(begin[i])) {
            return false;
        }
    }
    return true;
}

bool pcap_key_host(const char *endpoint, char *out, size_t out_size)
{
    key_clear(out, out_size);
    const char *begin = NULL;
    const char *end = NULL;
    key_trim(endpoint, &begin, &end);
    if (end <= begin) return false;
    if (key_is_mac(begin, end)) {
        if (out_size < 18U) return false;
        for (int i = 0; i < 17; i++) {
            out[i] = ((i % 3) == 2) ? ':' : ascii_lower(begin[i]);
        }
        out[17] = '\0';
        return true;
    }
    key_strip_port(&begin, &end);
    if (end <= begin || !key_all_printable(begin, end)) return false;
    return key_emit(begin, end, out, out_size);
}

bool pcap_key_host_pair(const char *first, const char *second,
                        char *out, size_t out_size)
{
    key_clear(out, out_size);
    char left[PCAP_KEY_TEXT_MAX];
    char right[PCAP_KEY_TEXT_MAX];
    if (!pcap_key_host(first, left, sizeof(left)) ||
        !pcap_key_host(second, right, sizeof(right))) {
        return false;
    }
    const char *low = left;
    const char *high = right;
    if (strcmp(left, right) > 0) {
        low = right;
        high = left;
    }
    int written = snprintf(out, out_size, "%s <-> %s", low, high);
    if (written < 0 || (size_t)written >= out_size) {
        key_clear(out, out_size);
        return false;
    }
    return true;
}

/* One name per port for both TCP and UDP. Keeping a single table means the key
 * stays stable no matter which transport carried the observation, and the
 * transport is already part of the rendered key. */
static const char *service_name(uint16_t port)
{
    switch (port) {
        case 20U: return "ftp-data";
        case 21U: return "ftp";
        case 22U: return "ssh";
        case 23U: return "telnet";
        case 25U: return "smtp";
        case 53U: return "dns";
        case 67U: case 68U: return "dhcp";
        case 69U: return "tftp";
        case 80U: return "http";
        case 110U: return "pop3";
        case 123U: return "ntp";
        case 135U: return "msrpc";
        case 137U: case 138U: case 139U: return "netbios";
        case 143U: return "imap";
        case 161U: case 162U: return "snmp";
        case 389U: return "ldap";
        case 443U: return "https";
        case 445U: return "smb";
        case 465U: return "smtps";
        case 500U: return "isakmp";
        case 514U: return "syslog";
        case 546U: case 547U: return "dhcpv6";
        case 554U: return "rtsp";
        case 587U: return "submission";
        case 631U: return "ipp";
        case 636U: return "ldaps";
        case 853U: return "dns-over-tls";
        case 993U: return "imaps";
        case 995U: return "pop3s";
        case 1080U: return "socks";
        case 1433U: return "mssql";
        case 1521U: return "oracle";
        case 1723U: return "pptp";
        case 1883U: return "mqtt";
        case 1900U: return "ssdp";
        case 3306U: return "mysql";
        case 3389U: return "rdp";
        case 3478U: return "stun";
        case 4500U: return "ipsec-nat-t";
        case 5060U: case 5061U: return "sip";
        case 5222U: return "xmpp";
        case 5353U: return "mdns";
        case 5432U: return "postgresql";
        case 5683U: return "coap";
        case 5900U: return "vnc";
        case 6379U: return "redis";
        case 8080U: return "http-alt";
        case 8443U: return "https-alt";
        case 8883U: return "mqtts";
        case 9100U: return "jetdirect";
        case 27017U: return "mongodb";
        case 51820U: return "wireguard";
        default: return NULL;
    }
}

static const char *transport_name(uint8_t ip_protocol, char *scratch, size_t scratch_size)
{
    switch (ip_protocol) {
        case 1U: return "icmp";
        case 6U: return "tcp";
        case 17U: return "udp";
        case 58U: return "icmpv6";
        case 132U: return "sctp";
        default: break;
    }
    int written = snprintf(scratch, scratch_size, "ip%u", (unsigned)ip_protocol);
    if (written < 0 || (size_t)written >= scratch_size) return "ip";
    return scratch;
}

bool pcap_key_service(uint16_t port, uint8_t ip_protocol, char *out, size_t out_size)
{
    key_clear(out, out_size);
    if (port == 0U) return false;
    char scratch[8];
    const char *transport = transport_name(ip_protocol, scratch, sizeof(scratch));
    const char *name = service_name(port);
    int written;
    if (name) {
        written = snprintf(out, out_size, "%s/%s", name, transport);
    } else {
        written = snprintf(out, out_size, "%u/%s", (unsigned)port, transport);
    }
    if (written < 0 || (size_t)written >= out_size) {
        key_clear(out, out_size);
        return false;
    }
    return true;
}

bool pcap_key_filename(const char *input, char *out, size_t out_size)
{
    key_clear(out, out_size);
    const char *begin = NULL;
    const char *end = NULL;
    key_trim(input, &begin, &end);
    for (const char *cursor = begin; cursor < end; cursor++) {
        if (*cursor == '?' || *cursor == '#') {
            end = cursor;
            break;
        }
    }
    for (const char *cursor = end; cursor > begin; cursor--) {
        if (cursor[-1] == '/' || cursor[-1] == '\\') {
            begin = cursor;
            break;
        }
    }
    while (end > begin && (is_blank(end[-1]) || end[-1] == '.')) end--;
    if (end <= begin || !key_all_printable(begin, end)) return false;
    if (!out || out_size == 0) return false;
    size_t length = (size_t)(end - begin);
    if (length >= out_size) return false;
    memcpy(out, begin, length);        /* case is part of a file name */
    out[length] = '\0';
    return true;
}

bool pcap_key_hash(const char *input, char *out, size_t out_size)
{
    key_clear(out, out_size);
    const char *begin = NULL;
    const char *end = NULL;
    key_trim(input, &begin, &end);
    size_t length = (size_t)(end - begin);
    if (length != 32U && length != 40U && length != 64U) return false;
    for (const char *cursor = begin; cursor < end; cursor++) {
        if (!is_hex(*cursor)) return false;
    }
    return key_emit(begin, end, out, out_size);
}

/* --------------------------------------------------------------- top-k --- */

uint64_t pcap_sat_add_u64(uint64_t value, uint64_t increment)
{
    return (value > UINT64_MAX - increment) ? UINT64_MAX : value + increment;
}

pcap_topk_result_t pcap_topk_add_text(pcap_summary_text_counter_t *entries,
                                      size_t capacity, uint16_t *used,
                                      const char *label, uint64_t bytes,
                                      bool *approximate)
{
    if (!entries || !used || !label || !label[0] || capacity == 0) {
        return PCAP_TOPK_EXISTING;
    }
    for (uint16_t i = 0; i < *used; i++) {
        if (strcmp(entries[i].label, label) == 0) {
            entries[i].count = pcap_sat_add_u64(entries[i].count, 1U);
            entries[i].bytes = pcap_sat_add_u64(entries[i].bytes, bytes);
            return PCAP_TOPK_EXISTING;
        }
    }
    if (*used < capacity) {
        pcap_summary_text_counter_t *entry = &entries[(*used)++];
        size_t length = bounded_length(label, sizeof(entry->label) - 1U);
        memcpy(entry->label, label, length);
        entry->label[length] = '\0';
        entry->count = 1;
        entry->bytes = bytes;
        return PCAP_TOPK_INSERTED;
    }

    size_t minimum = 0;
    for (size_t i = 1; i < capacity; i++) {
        if (entries[i].count < entries[minimum].count) {
            minimum = i;
        }
    }
    /* Space-Saving: the newcomer inherits the evicted count so a dominant key
     * cannot be displaced by a stream of one-off labels. Bytes start over,
     * because the evicted label's bytes did not belong to this key. */
    uint64_t previous_count = entries[minimum].count;
    size_t length = bounded_length(label, sizeof(entries[minimum].label) - 1U);
    memcpy(entries[minimum].label, label, length);
    entries[minimum].label[length] = '\0';
    entries[minimum].count = pcap_sat_add_u64(previous_count, 1U);
    entries[minimum].bytes = bytes;
    if (approximate) {
        *approximate = true;
    }
    return PCAP_TOPK_REPLACED;
}

void pcap_topk_add_port(pcap_summary_port_counter_t *entries, size_t capacity,
                        uint16_t *used, uint16_t port, uint8_t ip_protocol,
                        bool *approximate)
{
    if (!entries || !used || port == 0 || capacity == 0) {
        return;
    }
    for (uint16_t i = 0; i < *used; i++) {
        if (entries[i].port == port && entries[i].ip_protocol == ip_protocol) {
            entries[i].count = pcap_sat_add_u64(entries[i].count, 1U);
            return;
        }
    }
    if (*used < capacity) {
        pcap_summary_port_counter_t *entry = &entries[(*used)++];
        entry->port = port;
        entry->ip_protocol = ip_protocol;
        entry->count = 1;
        return;
    }
    size_t minimum = 0;
    for (size_t i = 1; i < capacity; i++) {
        if (entries[i].count < entries[minimum].count) {
            minimum = i;
        }
    }
    uint64_t previous_count = entries[minimum].count;
    entries[minimum].port = port;
    entries[minimum].ip_protocol = ip_protocol;
    entries[minimum].count = pcap_sat_add_u64(previous_count, 1U);
    if (approximate) {
        *approximate = true;
    }
}

static int compare_text_counter(const void *left, const void *right)
{
    const pcap_summary_text_counter_t *a = left;
    const pcap_summary_text_counter_t *b = right;
    if (a->count < b->count) return 1;
    if (a->count > b->count) return -1;
    return strcmp(a->label, b->label);
}

static int compare_port_counter(const void *left, const void *right)
{
    const pcap_summary_port_counter_t *a = left;
    const pcap_summary_port_counter_t *b = right;
    if (a->count < b->count) return 1;
    if (a->count > b->count) return -1;
    if (a->ip_protocol != b->ip_protocol) {
        return (int)a->ip_protocol - (int)b->ip_protocol;
    }
    return (int)a->port - (int)b->port;
}

void pcap_topk_sort_text(pcap_summary_text_counter_t *entries, size_t count)
{
    if (!entries || count < 2U) return;
    qsort(entries, count, sizeof(entries[0]), compare_text_counter);
}

void pcap_topk_sort_port(pcap_summary_port_counter_t *entries, size_t count)
{
    if (!entries || count < 2U) return;
    qsort(entries, count, sizeof(entries[0]), compare_port_counter);
}

/* --------------------------------------------------------------- ratio --- */

pcap_ratio_t pcap_reduce_ratio(uint64_t numerator, uint64_t denominator,
                               uint64_t min_samples)
{
    pcap_ratio_t ratio = {
        .numerator = numerator,
        .denominator = denominator,
        .value = 0.0,
        .valid = denominator > 0U,
        .low_sample = false,
    };
    if (ratio.valid) {
        ratio.value = (double)numerator / (double)denominator;
        ratio.low_sample = denominator < min_samples;
    }
    return ratio;
}

size_t pcap_ratio_format(const pcap_ratio_t *ratio, char *out, size_t out_size)
{
    if (!out || out_size == 0) return 0;
    int written;
    if (!ratio || !ratio->valid) {
        written = snprintf(out, out_size, "n/a (no samples)");
    } else {
        written = snprintf(out, out_size, "%.1f%% (%llu/%llu%s)",
                           ratio->value * 100.0,
                           (unsigned long long)ratio->numerator,
                           (unsigned long long)ratio->denominator,
                           ratio->low_sample ? ", low sample" : "");
    }
    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    return ((size_t)written >= out_size) ? out_size - 1U : (size_t)written;
}

/* --------------------------------------------------------- time window --- */

void pcap_window_init(pcap_window_t *window, uint64_t start_us, uint64_t end_us)
{
    if (!window) return;
    memset(window, 0, sizeof(*window));
    window->start_us = start_us;
    if (end_us <= start_us) {
        window->bucket_us = 1U;
        window->bucket_count = 1U;
    } else {
        uint64_t span = end_us - start_us;
        /* +1 guarantees span / bucket_us < PCAP_WINDOW_BUCKETS, so the last
         * sample of the capture still lands inside the array. */
        window->bucket_us = span / PCAP_WINDOW_BUCKETS + 1U;
        window->bucket_count = (uint16_t)(span / window->bucket_us + 1U);
    }
    window->configured = true;
}

void pcap_window_add(pcap_window_t *window, uint64_t timestamp_us)
{
    if (!window || !window->configured) return;
    if (timestamp_us < window->start_us) {
        window->out_of_range++;
        return;
    }
    uint64_t index = (timestamp_us - window->start_us) / window->bucket_us;
    if (index >= window->bucket_count) {
        window->out_of_range++;
        return;
    }
    if (window->counts[index] < UINT32_MAX) window->counts[index]++;
    window->total = pcap_sat_add_u64(window->total, 1U);
}

uint32_t pcap_window_peak(const pcap_window_t *window, uint16_t *index_out)
{
    if (index_out) *index_out = 0;
    if (!window || !window->configured) return 0;
    uint32_t peak = 0;
    uint16_t peak_index = 0;
    for (uint16_t i = 0; i < window->bucket_count; i++) {
        if (window->counts[i] > peak) {
            peak = window->counts[i];
            peak_index = i;
        }
    }
    if (index_out) *index_out = peak_index;
    return peak;
}

double pcap_window_burst_score(const pcap_window_t *window)
{
    if (!window || !window->configured || window->total == 0U ||
        window->bucket_count == 0U) {
        return 0.0;
    }
    double mean = (double)window->total / (double)window->bucket_count;
    if (mean <= 0.0) return 0.0;
    return (double)pcap_window_peak(window, NULL) / mean;
}

double pcap_window_bucket_seconds(const pcap_window_t *window)
{
    if (!window || !window->configured) return 0.0;
    return (double)window->bucket_us / 1000000.0;
}

/* -------------------------------------------------------------- unique --- */

#define PCAP_UNIQUE_SKETCH_BITS (PCAP_UNIQUE_SKETCH_WORDS * 32U)
/* Past this load the two-hash sketch starts to merge keys often enough that the
 * count deserves the approximate label: at 256 distinct keys in 4096 bits the
 * false-merge probability is still near one percent. */
#define PCAP_UNIQUE_RELIABLE_DISTINCT (PCAP_UNIQUE_SKETCH_BITS / 16U)

static uint64_t fnv1a64(const char *key)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char *cursor = (const unsigned char *)key; *cursor; cursor++) {
        hash ^= (uint64_t)*cursor;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void pcap_unique_reset(pcap_unique_t *unique)
{
    if (!unique) return;
    memset(unique, 0, sizeof(*unique));
}

bool pcap_unique_add(pcap_unique_t *unique, const char *key)
{
    if (!unique || !key || !key[0]) return false;
    unique->observations = pcap_sat_add_u64(unique->observations, 1U);
    uint64_t hash = fnv1a64(key);
    uint32_t primary = (uint32_t)(hash & 0xFFFFFFFFU);
    uint32_t secondary = (uint32_t)(hash >> 32) | 1U;   /* odd, so it strides the whole bitmap */
    bool fresh = false;
    for (unsigned probe = 0; probe < 2U; probe++) {
        uint32_t position = (primary + probe * secondary) % PCAP_UNIQUE_SKETCH_BITS;
        uint32_t *word = &unique->bits[position / 32U];
        uint32_t mask = 1UL << (position % 32U);
        if ((*word & mask) == 0U) {
            *word |= mask;
            fresh = true;
        }
    }
    if (fresh) {
        unique->distinct = pcap_sat_add_u64(unique->distinct, 1U);
        if (unique->distinct > PCAP_UNIQUE_RELIABLE_DISTINCT) {
            unique->approximate = true;
        }
    }
    return fresh;
}
