#include "hs_capture_analyzer.h"
#include "pcap_reader.h"

#include <stdio.h>
#include <string.h>

#define HS_CAPTURE_LINKTYPE_RADIOTAP 127U
#define HS_CAPTURE_PEERS 4

typedef struct {
    uint8_t ap[6], sta[6], anonce[32];
    uint8_t replay_counter[8];
    bool valid;
} pending_t;

typedef struct {
    uint8_t bssid[6], ssid[32], length;
} ssid_t;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static bool nonzero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) if (p[i]) return true;
    return false;
}

static size_t filename_ssid(const char *name, uint8_t out[32])
{
    if (!name) return 0;
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    const char *dot = strrchr(base, '.');
    if (!dot || strcmp(dot, ".pcap") != 0) return 0;
    const char *seq = dot;
    while (seq > base && seq[-1] >= '0' && seq[-1] <= '9') --seq;
    if (seq == dot || seq == base || seq[-1] != '_') return 0;
    const char *mac_end = seq - 1;
    if ((size_t)(mac_end - base) < 8) return 0;
    const char *mac = mac_end - 6;
    if (mac[-1] != '_') return 0;
    for (const char *p = mac; p < mac_end; ++p) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
              (*p >= 'A' && *p <= 'F'))) return 0;
    }
    size_t n = (size_t)(mac - 1 - base);
    if (!n || n > 32) return 0;
    memcpy(out, base, n);
    return n;
}

static void reader_failure(hs_capture_report_t *r, pcap_reader_status_t status)
{
    r->state = HS_CAPTURE_INVALID;
    switch (status) {
    case PCAP_READER_TRUNCATED: r->reason = HS_CAPTURE_REASON_TRUNCATED; break;
    case PCAP_READER_INVALID_FORMAT: r->reason = HS_CAPTURE_REASON_BAD_HEADER; break;
    case PCAP_READER_UNSUPPORTED_FORMAT:
        r->state = HS_CAPTURE_UNSUPPORTED;
        r->reason = HS_CAPTURE_REASON_UNSUPPORTED_FORMAT;
        break;
    default:
        r->state = HS_CAPTURE_UNAVAILABLE;
        r->reason = HS_CAPTURE_REASON_IO_ERROR;
        break;
    }
}

hs_capture_report_t hs_capture_analyze_pcap(const char *path, const char *file_name,
                                           hccapx_record_t *records, size_t capacity,
                                           const volatile bool *cancel)
{
    hs_capture_report_t r = {.state = HS_CAPTURE_UNAVAILABLE,
                             .reason = HS_CAPTURE_REASON_IO_ERROR};
    pcap_reader_t *reader = NULL;
    pcap_capture_info_t info = {0};
    pending_t pending[HS_CAPTURE_PEERS] = {0};
    ssid_t ssids[HS_CAPTURE_PEERS] = {0};
    size_t ssid_count = 0, count = 0;
    bool limited = false;
    if (!records || !capacity || capacity > UINT32_MAX ||
        capacity > SIZE_MAX / sizeof(*records)) {
        r.reason = HS_CAPTURE_REASON_LIMIT_REACHED;
        return r;
    }
    memset(records, 0, capacity * sizeof(*records));
    if (cancel && *cancel) goto cancelled;
    pcap_reader_status_t status = pcap_reader_open(path, &reader, &info);
    if (status != PCAP_READER_OK) {
        reader_failure(&r, status);
        goto done;
    }
    if (info.link_type != PCAP_LINKTYPE_IEEE802_11 &&
        info.link_type != HS_CAPTURE_LINKTYPE_RADIOTAP) {
        r.state = HS_CAPTURE_UNSUPPORTED;
        r.reason = HS_CAPTURE_REASON_UNSUPPORTED_LINKTYPE;
        goto done;
    }
    status = pcap_reader_iterate_begin(reader);
    if (status != PCAP_READER_OK) {
        reader_failure(&r, status);
        goto done;
    }
    for (;;) {
        if (cancel && *cancel) goto cancelled;
        uint8_t buffer[512];
        pcap_packet_index_t packet;
        size_t got = 0;
        bool have = false;
        status = pcap_reader_iterate_next(reader, &packet, buffer, sizeof(buffer), &got, &have);
        if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) {
            reader_failure(&r, status);
            goto done;
        }
        if (!have) break;
        if (r.packet_count == UINT32_MAX) {
            r.reason = HS_CAPTURE_REASON_LIMIT_REACHED;
            goto done;
        }
        ++r.packet_count;
        if (status == PCAP_READER_LIMIT_REACHED) limited = true;
        const uint8_t *f = buffer;
        size_t length = got;
        if (info.link_type == HS_CAPTURE_LINKTYPE_RADIOTAP) {
            if (length < 8 || le16(f + 2) < 8 || le16(f + 2) > length) {
                limited = true;
                continue;
            }
            uint16_t rt = le16(f + 2);
            f += rt;
            length -= rt;
        }
        if (length < 24) continue;
        uint16_t fc = le16(f);
        unsigned type = (fc >> 2) & 3, subtype = (fc >> 4) & 15;
        bool to_ds = (fc & 0x0100) != 0, from_ds = (fc & 0x0200) != 0;
        if (type == 0) {
            size_t fixed;
            if (subtype == 8 || subtype == 5) fixed = 12;
            else if (subtype == 0) fixed = 4;
            else if (subtype == 2) fixed = 10;
            else continue;
            for (size_t offset = 24 + fixed; offset + 2 <= length;) {
                uint8_t tag = f[offset], n = f[offset + 1];
                if (offset + 2 + n > length) break;
                if (tag == 0) {
                    if (n && n <= 32 && nonzero(f + offset + 2, n)) {
                        size_t slot = 0;
                        while (slot < ssid_count && memcmp(ssids[slot].bssid, f + 16, 6)) ++slot;
                        if (slot < HS_CAPTURE_PEERS) {
                            if (slot == ssid_count) ++ssid_count;
                            memcpy(ssids[slot].bssid, f + 16, 6);
                            memcpy(ssids[slot].ssid, f + offset + 2, n);
                            ssids[slot].length = n;
                        } else limited = true;
                    }
                    break;
                }
                offset += 2 + n;
            }
            continue;
        }
        if (type != 2) continue;
        size_t header = 24 + ((to_ds && from_ds) ? 6 : 0) + ((subtype & 8) ? 2 : 0);
        if ((subtype & 8) && (fc & 0x8000)) header += 4;
        if (header + 8 > length) continue;
        const uint8_t *llc = f + header;
        if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 3 || be16(llc + 6) != 0x888e) continue;
        ++r.eapol_count;
        const uint8_t *e = llc + 8;
        size_t available = length - header - 8;
        if (available < 4) { ++r.malformed_count; continue; }
        if (e[1] != 3) continue;
        size_t frame_length = (size_t)be16(e + 2) + 4;
        if (frame_length < 99 || frame_length > 256 || frame_length > available) {
            ++r.malformed_count;
            continue;
        }
        uint16_t kinfo = be16(e + 5);
        uint8_t keyver = kinfo & 7;
        if (keyver != 1 && keyver != 2) { ++r.unsupported_keyver_count; continue; }
        if (!nonzero(e + 17, 32)) continue;
        const uint8_t *ap, *sta;
        if (from_ds && !to_ds) { ap = f + 10; sta = f + 4; }
        else if (to_ds && !from_ds) { ap = f + 4; sta = f + 10; }
        else continue;
        size_t slot = 0;
        while (slot < HS_CAPTURE_PEERS && !(pending[slot].valid &&
               !memcmp(pending[slot].ap, ap, 6) && !memcmp(pending[slot].sta, sta, 6))) ++slot;
        if (kinfo & 0x0080) {
            ++r.ap_nonce_count;
            if (slot == HS_CAPTURE_PEERS) {
                for (slot = 0; slot < HS_CAPTURE_PEERS && pending[slot].valid; ++slot) {}
                if (slot == HS_CAPTURE_PEERS) { slot = 0; limited = true; }
            }
            memcpy(pending[slot].ap, ap, 6);
            memcpy(pending[slot].sta, sta, 6);
            memcpy(pending[slot].anonce, e + 17, 32);
            memcpy(pending[slot].replay_counter, e + 9, 8);
            pending[slot].valid = true;
        } else if (kinfo & 0x0100) {
            ++r.sta_response_count;
            if (slot == HS_CAPTURE_PEERS ||
                memcmp(pending[slot].replay_counter, e + 9, 8) != 0 ||
                count >= capacity) continue;
            hccapx_record_t *record = &records[count++];
            record->signature = HCCAPX_SIGNATURE;
            record->version = 4;
            record->keyver = keyver;
            memcpy(record->mac_ap, ap, 6);
            memcpy(record->mac_sta, sta, 6);
            memcpy(record->nonce_ap, pending[slot].anonce, 32);
            memcpy(record->nonce_sta, e + 17, 32);
            memcpy(record->keymic, e + 81, 16);
            memcpy(record->eapol, e, frame_length);
            record->eapol_len = (uint16_t)frame_length;
        }
    }
    if (cancel && *cancel) goto cancelled;
    uint8_t fallback[32];
    size_t fallback_length = filename_ssid(file_name, fallback);
    size_t valid = 0;
    bool missing_ssid = false;
    for (size_t i = 0; i < count; ++i) {
        size_t slot = 0;
        while (slot < ssid_count && memcmp(ssids[slot].bssid, records[i].mac_ap, 6)) ++slot;
        size_t n = slot < ssid_count ? ssids[slot].length : fallback_length;
        if (!n) { missing_ssid = true; continue; }
        records[i].essid_len = (uint8_t)n;
        memcpy(records[i].essid, slot < ssid_count ? ssids[slot].ssid : fallback, n);
        if (hs_capture_record_valid(&records[i])) records[valid++] = records[i];
        else ++r.malformed_count;
    }
    if (valid) {
        r.state = HS_CAPTURE_READY;
        r.reason = HS_CAPTURE_REASON_OK;
        r.record_count = (uint32_t)valid;
        memset(records + valid, 0, (capacity - valid) * sizeof(*records));
        goto done;
    }
    r.state = HS_CAPTURE_INVALID;
    if (limited) { r.state = HS_CAPTURE_UNAVAILABLE; r.reason = HS_CAPTURE_REASON_LIMIT_REACHED; }
    else if (missing_ssid) r.reason = HS_CAPTURE_REASON_MISSING_SSID;
    else if (r.unsupported_keyver_count) {
        r.state = HS_CAPTURE_UNSUPPORTED;
        r.reason = HS_CAPTURE_REASON_UNSUPPORTED_KEYVER;
    } else if (r.malformed_count) r.reason = HS_CAPTURE_REASON_MALFORMED_EAPOL;
    else if (!r.eapol_count) r.reason = HS_CAPTURE_REASON_NO_EAPOL;
    else if (!r.ap_nonce_count) r.reason = HS_CAPTURE_REASON_NO_AP_NONCE;
    else if (!r.sta_response_count) r.reason = HS_CAPTURE_REASON_NO_STA_RESPONSE;
    else r.reason = HS_CAPTURE_REASON_NO_MATCHING_PAIR;
    goto done;

cancelled:
    r.state = HS_CAPTURE_CANCELLED;
    r.reason = HS_CAPTURE_REASON_CANCELLED;
done:
    pcap_reader_close(reader);
    if (r.state != HS_CAPTURE_READY) {
        memset(records, 0, capacity * sizeof(*records));
        r.record_count = 0;
    }
    return r;
}

hs_capture_report_t hs_capture_analyze_hccapx(
    const char *path, hccapx_record_t *records, size_t capacity,
    const volatile bool *cancel)
{
    hs_capture_report_t r = {.state = HS_CAPTURE_UNAVAILABLE,
                             .reason = HS_CAPTURE_REASON_IO_ERROR};
    FILE *file = NULL;
    size_t count = 0;
    if (!path || !records || capacity == 0U ||
        capacity > HS_CAPTURE_HCCAPX_MAX_RECORDS) {
        r.reason = HS_CAPTURE_REASON_LIMIT_REACHED;
        return r;
    }
    memset(records, 0, capacity * sizeof(*records));
    if (cancel && *cancel) goto cancelled;
    file = fopen(path, "rb");
    if (!file) goto done;

    for (;;) {
        if (cancel && *cancel) goto cancelled;
        hccapx_record_t record;
        size_t got = fread(&record, 1, sizeof(record), file);
        if (got == 0U) {
            if (ferror(file)) goto done;
            break;
        }
        if (got != sizeof(record)) {
            r.state = HS_CAPTURE_INVALID;
            r.reason = HS_CAPTURE_REASON_TRUNCATED;
            goto done;
        }
        if (count >= capacity) {
            r.state = HS_CAPTURE_UNAVAILABLE;
            r.reason = HS_CAPTURE_REASON_LIMIT_REACHED;
            goto done;
        }
        if (record.signature != HCCAPX_SIGNATURE || record.version != 4U) {
            r.state = HS_CAPTURE_UNSUPPORTED;
            r.reason = HS_CAPTURE_REASON_UNSUPPORTED_FORMAT;
            goto done;
        }
        if (record.essid_len == 0U || record.essid_len > sizeof(record.essid)) {
            r.state = HS_CAPTURE_INVALID;
            r.reason = HS_CAPTURE_REASON_MISSING_SSID;
            goto done;
        }
        if (record.keyver != 1U && record.keyver != 2U) {
            r.state = HS_CAPTURE_UNSUPPORTED;
            r.reason = HS_CAPTURE_REASON_UNSUPPORTED_KEYVER;
            goto done;
        }
        if (!hs_capture_record_valid(&record)) {
            r.state = HS_CAPTURE_INVALID;
            r.reason = HS_CAPTURE_REASON_MALFORMED_EAPOL;
            goto done;
        }
        records[count++] = record;
    }
    if (count == 0U) {
        r.state = HS_CAPTURE_INVALID;
        r.reason = HS_CAPTURE_REASON_BAD_HEADER;
        goto done;
    }
    r.state = HS_CAPTURE_READY;
    r.reason = HS_CAPTURE_REASON_OK;
    r.record_count = (uint32_t)count;
    goto done;

cancelled:
    r.state = HS_CAPTURE_CANCELLED;
    r.reason = HS_CAPTURE_REASON_CANCELLED;
done:
    if (file) fclose(file);
    if (r.state != HS_CAPTURE_READY) {
        memset(records, 0, capacity * sizeof(*records));
        r.record_count = 0U;
    }
    return r;
}

const char *hs_capture_reason_name(hs_capture_reason_t reason)
{
    switch (reason) {
    case HS_CAPTURE_REASON_OK: return "ok";
    case HS_CAPTURE_REASON_IO_ERROR: return "io_error";
    case HS_CAPTURE_REASON_BAD_HEADER: return "bad_header";
    case HS_CAPTURE_REASON_TRUNCATED: return "truncated";
    case HS_CAPTURE_REASON_UNSUPPORTED_LINKTYPE: return "unsupported_linktype";
    case HS_CAPTURE_REASON_NO_EAPOL: return "no_eapol";
    case HS_CAPTURE_REASON_NO_AP_NONCE: return "no_ap_nonce";
    case HS_CAPTURE_REASON_NO_STA_RESPONSE: return "no_sta_response";
    case HS_CAPTURE_REASON_NO_MATCHING_PAIR: return "no_matching_pair";
    case HS_CAPTURE_REASON_MISSING_SSID: return "missing_ssid";
    case HS_CAPTURE_REASON_UNSUPPORTED_KEYVER: return "unsupported_keyver";
    case HS_CAPTURE_REASON_MALFORMED_EAPOL: return "malformed_eapol";
    case HS_CAPTURE_REASON_PMKID_UNSUPPORTED: return "pmkid_unsupported";
    case HS_CAPTURE_REASON_SAE_UNSUPPORTED: return "sae_unsupported";
    case HS_CAPTURE_REASON_LIMIT_REACHED: return "limit_reached";
    case HS_CAPTURE_REASON_CANCELLED: return "cancelled";
    case HS_CAPTURE_REASON_UNSUPPORTED_FORMAT: return "unsupported_format";
    default: return "unknown";
    }
}
