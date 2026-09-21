#ifndef HS_CAPTURE_ANALYZER_H
#define HS_CAPTURE_ANALYZER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HS_CAPTURE_VALIDATOR_VERSION 1U
#define HCCAPX_SIGNATURE 0x58504348u
#define HCCAPX_RECORD_SIZE 393

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t version;
    uint8_t message_pair;
    uint8_t essid_len;
    uint8_t essid[32];
    uint8_t keyver;
    uint8_t keymic[16];
    uint8_t mac_ap[6];
    uint8_t nonce_ap[32];
    uint8_t mac_sta[6];
    uint8_t nonce_sta[32];
    uint16_t eapol_len;
    uint8_t eapol[256];
} hccapx_record_t;

_Static_assert(sizeof(hccapx_record_t) == HCCAPX_RECORD_SIZE,
               "hccapx record must be exactly 393 packed bytes");

/* Existing verifier's structural rules; capture qualification additionally
 * requires a known SSID. Keep binary SSIDs valid for the verifier. */
static inline bool hs_capture_record_valid(const hccapx_record_t *r)
{
    return r && r->signature == HCCAPX_SIGNATURE && r->version == 4 &&
           (r->message_pair & 0x7f) <= 5 && r->essid_len <= 32 &&
           (r->keyver == 1 || r->keyver == 2) &&
           r->eapol_len >= 99 && r->eapol_len <= sizeof(r->eapol) &&
           r->eapol[1] == 3 &&
           (((unsigned)r->eapol[2] << 8) | r->eapol[3]) + 4 == r->eapol_len &&
           (r->eapol[6] & 7) == r->keyver;
}

typedef enum {
    HS_CAPTURE_READY = 0,
    HS_CAPTURE_INVALID = 1,
    HS_CAPTURE_UNSUPPORTED = 2,
    HS_CAPTURE_UNAVAILABLE = 3,
    HS_CAPTURE_CANCELLED = 4,
} hs_capture_state_t;

/* Stable persisted values; append new reasons instead of reordering. */
typedef enum {
    HS_CAPTURE_REASON_OK = 0,
    HS_CAPTURE_REASON_IO_ERROR = 1,
    HS_CAPTURE_REASON_BAD_HEADER = 2,
    HS_CAPTURE_REASON_TRUNCATED = 3,
    HS_CAPTURE_REASON_UNSUPPORTED_LINKTYPE = 4,
    HS_CAPTURE_REASON_NO_EAPOL = 5,
    HS_CAPTURE_REASON_NO_AP_NONCE = 6,
    HS_CAPTURE_REASON_NO_STA_RESPONSE = 7,
    HS_CAPTURE_REASON_NO_MATCHING_PAIR = 8,
    HS_CAPTURE_REASON_MISSING_SSID = 9,
    HS_CAPTURE_REASON_UNSUPPORTED_KEYVER = 10,
    HS_CAPTURE_REASON_MALFORMED_EAPOL = 11,
    HS_CAPTURE_REASON_PMKID_UNSUPPORTED = 12,
    HS_CAPTURE_REASON_SAE_UNSUPPORTED = 13,
    HS_CAPTURE_REASON_LIMIT_REACHED = 14,
    HS_CAPTURE_REASON_CANCELLED = 15,
    HS_CAPTURE_REASON_UNSUPPORTED_FORMAT = 16,
} hs_capture_reason_t;

typedef struct {
    hs_capture_state_t state;
    hs_capture_reason_t reason;
    uint32_t packet_count;
    uint32_t eapol_count;
    uint32_t ap_nonce_count;
    uint32_t sta_response_count;
    uint32_t malformed_count;
    uint32_t unsupported_keyver_count;
    uint32_t record_count;
} hs_capture_report_t;

/* Read-only, transport/UI-independent inspection of a complete local file.
 * Output is zeroed unless READY; record_count never exceeds record_capacity.
 * A full sequential scan is required even after output capacity is reached.
 * file_name may supply a legacy <ssid>_<six hex digits>_<sequence>.pcap SSID.
 * A resource limit with no usable record is UNAVAILABLE, never INVALID.
 * PMKID/SAE reasons are reserved: this parser does not claim to identify them. */
hs_capture_report_t hs_capture_analyze_pcap(const char *path, const char *file_name,
                                           hccapx_record_t *records, size_t record_capacity,
                                           const volatile bool *cancel_requested);
const char *hs_capture_reason_name(hs_capture_reason_t reason);

#endif
