#define _POSIX_C_SOURCE 200809L
#include "hs_capture_analyzer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Real, little-endian classic-PCAP fixtures. These catch loss of evidence,
 * accepting incomplete captures, mismatching APs, and exposing unusable output. */
static char path[] = "/tmp/hs-capture-test-XXXXXX";
static const uint8_t ap[6] = {2, 0, 0, 0, 0, 1};
static const uint8_t sta[6] = {2, 0, 0, 0, 0, 2};
static hccapx_record_t records[4];

static void le32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8 * i));
}

static FILE *capture_open(uint32_t link)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    uint8_t h[24] = {0xd4, 0xc3, 0xb2, 0xa1, 2, 0, 4, 0};
    le32(h + 16, 65535);
    le32(h + 20, link);
    assert(fwrite(h, 1, sizeof(h), f) == sizeof(h));
    return f;
}

static void packet(FILE *f, const uint8_t *data, size_t n)
{
    uint8_t h[16] = {0};
    le32(h + 8, (uint32_t)n);
    le32(h + 12, (uint32_t)n);
    assert(fwrite(h, 1, sizeof(h), f) == sizeof(h));
    assert(fwrite(data, 1, n, f) == n);
}

static void packet_with_original_length(FILE *f, const uint8_t *data, size_t captured,
                                        size_t original)
{
    uint8_t h[16] = {0};
    le32(h + 8, (uint32_t)captured);
    le32(h + 12, (uint32_t)original);
    assert(fwrite(h, 1, sizeof(h), f) == sizeof(h));
    assert(fwrite(data, 1, captured, f) == captured);
}

static void beacon(FILE *f, const uint8_t *bssid, const uint8_t *ssid, size_t n)
{
    uint8_t b[70] = {0x80};
    assert(n <= 32);
    memcpy(b + 16, bssid, 6);
    b[37] = (uint8_t)n;
    memcpy(b + 38, ssid, n);
    packet(f, b, 38 + n);
}

static void eapol_with_replay(FILE *f, bool from_ap, uint8_t keyver, bool wrong_sta,
                              bool malformed, bool radiotap, uint8_t replay)
{
    uint8_t b[139] = {0};
    uint8_t *w = b + (radiotap ? 8 : 0);
    if (radiotap) b[2] = 8;
    w[0] = 8;
    w[1] = from_ap ? 2 : 1;
    memcpy(w + 4, from_ap ? sta : ap, 6);
    memcpy(w + 10, from_ap ? ap : sta, 6);
    memcpy(w + 16, ap, 6);
    if (wrong_sta) w[15] = 3;
    const uint8_t llc[] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e};
    memcpy(w + 24, llc, sizeof(llc));
    uint8_t *e = w + 32;
    e[0] = 2; e[1] = 3; e[3] = malformed ? 96 : 95; e[4] = 2;
    e[5] = from_ap ? 0 : 1;
    e[6] = 8 | keyver | (from_ap ? 0x80 : 0);
    e[16] = replay; /* low byte of the big-endian replay counter */
    for (unsigned i = 0; i < 32; ++i) e[17 + i] = (uint8_t)(i + (from_ap ? 0 : 32));
    if (!from_ap) memset(e + 81, 0x5a, 16);
    packet(f, b, radiotap ? 139 : 131);
}

static void eapol(FILE *f, bool from_ap, uint8_t keyver, bool wrong_sta,
                  bool malformed, bool radiotap)
{
    eapol_with_replay(f, from_ap, keyver, wrong_sta, malformed, radiotap, 1);
}

static uint32_t file_checksum(void)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    uint32_t sum = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) sum = sum * 33 + (unsigned)ch;
    assert(!ferror(f));
    assert(fclose(f) == 0);
    return sum;
}

static hs_capture_report_t analyze(const char *name, hs_capture_state_t state,
                                   hs_capture_reason_t reason)
{
    uint32_t before = file_checksum();
    memset(records, 0xa5, sizeof(records));
    hs_capture_report_t r = hs_capture_analyze_pcap(path, name, records, 4, NULL);
    assert(r.state == state);
    assert(r.reason == reason);
    assert(file_checksum() == before); /* inspection never removes/modifies input */
    if (state != HS_CAPTURE_READY) {
        const uint8_t *bytes = (const uint8_t *)records;
        assert(r.record_count == 0);
        for (size_t i = 0; i < sizeof(records); ++i) assert(bytes[i] == 0);
    }
    return r;
}

static void ready_pair(void)
{
    FILE *f = capture_open(105);
    beacon(f, ap, (const uint8_t *)"Lab", 3);
    eapol(f, true, 2, false, false, false);
    eapol(f, false, 2, false, false, false);
    assert(fclose(f) == 0);
    hs_capture_report_t r = analyze(NULL, HS_CAPTURE_READY, HS_CAPTURE_REASON_OK);
    assert(r.packet_count == 3 && r.eapol_count == 2);
    assert(r.ap_nonce_count == 1 && r.sta_response_count == 1);
    assert(r.malformed_count == 0 && r.unsupported_keyver_count == 0 && r.record_count == 1);
    assert(sizeof(records[0]) == 393 && records[0].signature == 0x58504348);
    assert(records[0].version == 4 && records[0].message_pair == 0 && records[0].keyver == 2);
    assert(records[0].essid_len == 3 && memcmp(records[0].essid, "Lab", 3) == 0);
    assert(memcmp(records[0].mac_ap, ap, 6) == 0 && memcmp(records[0].mac_sta, sta, 6) == 0);
    for (unsigned i = 0; i < 32; ++i) {
        assert(records[0].nonce_ap[i] == i);
        assert(records[0].nonce_sta[i] == i + 32);
    }
    assert(records[0].eapol_len == 99 && records[0].keymic[0] == 0x5a);
    assert(hs_capture_record_valid(&records[0]));
}

static void evidence_reasons(void)
{
    FILE *f = capture_open(105);
    beacon(f, ap, (const uint8_t *)"Lab", 3);
    assert(fclose(f) == 0);
    hs_capture_report_t r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_NO_EAPOL);
    assert(r.packet_count == 1 && r.eapol_count == 0);

    f = capture_open(105);
    eapol(f, false, 2, false, false, false);
    assert(fclose(f) == 0);
    r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_NO_AP_NONCE);
    assert(r.eapol_count == 1 && r.ap_nonce_count == 0 && r.sta_response_count == 1);

    f = capture_open(105);
    eapol(f, true, 2, false, false, false);
    assert(fclose(f) == 0);
    r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_NO_STA_RESPONSE);
    assert(r.ap_nonce_count == 1 && r.sta_response_count == 0);

    f = capture_open(105);
    eapol(f, true, 2, false, false, false);
    eapol(f, false, 2, true, false, false);
    assert(fclose(f) == 0);
    r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_NO_MATCHING_PAIR);
    assert(r.ap_nonce_count == 1 && r.sta_response_count == 1);

    f = capture_open(105);
    eapol_with_replay(f, true, 2, false, false, false, 1);
    eapol_with_replay(f, false, 2, false, false, false, 2);
    assert(fclose(f) == 0);
    r = analyze("Lab_000001_7.pcap", HS_CAPTURE_INVALID,
                HS_CAPTURE_REASON_NO_MATCHING_PAIR);
    assert(r.ap_nonce_count == 1 && r.sta_response_count == 1);

    f = capture_open(105);
    eapol(f, false, 2, false, true, false);
    assert(fclose(f) == 0);
    r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_MALFORMED_EAPOL);
    assert(r.eapol_count == 1 && r.malformed_count == 1);

    f = capture_open(105);
    eapol(f, true, 3, false, false, false);
    eapol(f, false, 3, false, false, false);
    assert(fclose(f) == 0);
    r = analyze(NULL, HS_CAPTURE_UNSUPPORTED, HS_CAPTURE_REASON_UNSUPPORTED_KEYVER);
    assert(r.eapol_count == 2 && r.unsupported_keyver_count == 2);
}

static void ssid_and_linktypes(void)
{
    FILE *f = capture_open(105);
    eapol(f, true, 2, false, false, false);
    eapol(f, false, 2, false, false, false);
    assert(fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_MISSING_SSID);
    analyze("capture.pcap", HS_CAPTURE_INVALID, HS_CAPTURE_REASON_MISSING_SSID);
    analyze("Lab_Name_000001_7.pcap", HS_CAPTURE_READY, HS_CAPTURE_REASON_OK);
    assert(records[0].essid_len == 8 && memcmp(records[0].essid, "Lab_Name", 8) == 0);

    f = capture_open(105);
    uint8_t other_ap[6] = {2, 0, 0, 0, 0, 9};
    beacon(f, other_ap, (const uint8_t *)"Other", 5);
    eapol(f, true, 2, false, false, false);
    eapol(f, false, 2, false, false, false);
    assert(fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_MISSING_SSID);

    f = capture_open(105);
    const uint8_t binary_ssid[] = {'A', 0, 'B'};
    beacon(f, ap, binary_ssid, sizeof(binary_ssid));
    eapol(f, true, 1, false, false, false);
    eapol(f, false, 1, false, false, false);
    assert(fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_READY, HS_CAPTURE_REASON_OK);
    assert(records[0].essid_len == 3 && memcmp(records[0].essid, binary_ssid, 3) == 0);

    f = capture_open(127);
    eapol(f, true, 2, false, false, true);
    eapol(f, false, 2, false, false, true);
    assert(fclose(f) == 0);
    analyze("Lab_000001_7.pcap", HS_CAPTURE_READY, HS_CAPTURE_REASON_OK);
    f = capture_open(1);
    assert(fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_UNSUPPORTED, HS_CAPTURE_REASON_UNSUPPORTED_LINKTYPE);
}

static void incomplete_capture_and_cancellation(void)
{
    ready_pair();
    FILE *f = fopen(path, "ab");
    assert(f && fputc(1, f) != EOF && fclose(f) == 0);
    hs_capture_report_t r = analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_TRUNCATED);
    assert(r.packet_count == 3 && r.eapol_count == 2);

    f = capture_open(105);
    uint8_t h[16] = {0};
    le32(h + 8, 100); le32(h + 12, 100);
    assert(fwrite(h, 1, sizeof(h), f) == sizeof(h) && fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_TRUNCATED);

    f = fopen(path, "wb");
    assert(f && fputs("not a capture header at all", f) >= 0 && fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_BAD_HEADER);

    /* incl_len smaller than orig_len is a legal snaplen capture, not a torn
     * file. The record bytes are complete when incl_len bytes are present. */
    f = capture_open(105);
    uint8_t short_frame[24] = {0};
    packet_with_original_length(f, short_frame, sizeof(short_frame), 128);
    assert(fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_NO_EAPOL);

    volatile bool cancel = true;
    r = hs_capture_analyze_pcap(path, NULL, records, 4, &cancel);
    assert(r.state == HS_CAPTURE_CANCELLED && r.reason == HS_CAPTURE_REASON_CANCELLED);
    assert(r.packet_count == 0 && r.record_count == 0);
    r = hs_capture_analyze_pcap("/nonexistent/hs-capture.pcap", NULL, records, 4, NULL);
    assert(r.state == HS_CAPTURE_UNAVAILABLE && r.reason == HS_CAPTURE_REASON_IO_ERROR);
}

static void limits_and_full_scan(void)
{
    FILE *f = capture_open(105);
    beacon(f, ap, (const uint8_t *)"Lab", 3);
    eapol(f, true, 2, false, false, false);
    for (unsigned i = 0; i < 6; ++i) eapol(f, false, 2, false, false, false);
    assert(fclose(f) == 0);
    hs_capture_report_t r = analyze(NULL, HS_CAPTURE_READY, HS_CAPTURE_REASON_OK);
    assert(r.packet_count == 8 && r.record_count == 4 && r.sta_response_count == 6);
    f = fopen(path, "ab");
    assert(f && fputc(1, f) != EOF && fclose(f) == 0);
    analyze(NULL, HS_CAPTURE_INVALID, HS_CAPTURE_REASON_TRUNCATED);
    r = hs_capture_analyze_pcap(path, NULL, NULL, 0, NULL);
    assert(r.state == HS_CAPTURE_UNAVAILABLE && r.reason == HS_CAPTURE_REASON_LIMIT_REACHED);
}

static void hccapx_files(void)
{
    ready_pair();
    hccapx_record_t valid = records[0];
    hccapx_record_t output[16];
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(&valid, 1, sizeof(valid), f) == sizeof(valid));
    assert(fclose(f) == 0);

    hs_capture_report_t r = hs_capture_analyze_hccapx(
        path, output, 16, NULL);
    assert(r.state == HS_CAPTURE_READY && r.reason == HS_CAPTURE_REASON_OK);
    assert(r.record_count == 1 && hs_capture_record_valid(&output[0]));

    f = fopen(path, "ab");
    assert(f && fputc(0, f) != EOF && fclose(f) == 0);
    r = hs_capture_analyze_hccapx(path, output, 16, NULL);
    assert(r.state == HS_CAPTURE_INVALID &&
           r.reason == HS_CAPTURE_REASON_TRUNCATED);

    valid.essid_len = 0;
    f = fopen(path, "wb");
    assert(f && fwrite(&valid, 1, sizeof(valid), f) == sizeof(valid));
    assert(fclose(f) == 0);
    r = hs_capture_analyze_hccapx(path, output, 16, NULL);
    assert(r.state == HS_CAPTURE_INVALID &&
           r.reason == HS_CAPTURE_REASON_MISSING_SSID);

    valid = records[0];
    f = fopen(path, "wb");
    assert(f);
    for (unsigned i = 0; i < 17; ++i)
        assert(fwrite(&valid, 1, sizeof(valid), f) == sizeof(valid));
    assert(fclose(f) == 0);
    r = hs_capture_analyze_hccapx(path, output, 16, NULL);
    assert(r.state == HS_CAPTURE_UNAVAILABLE &&
           r.reason == HS_CAPTURE_REASON_LIMIT_REACHED);

    volatile bool cancel = true;
    r = hs_capture_analyze_hccapx(path, output, 16, &cancel);
    assert(r.state == HS_CAPTURE_CANCELLED &&
           r.reason == HS_CAPTURE_REASON_CANCELLED);
}

int main(void)
{
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0);
    ready_pair();
    evidence_reasons();
    ssid_and_linktypes();
    incomplete_capture_and_cancellation();
    limits_and_full_scan();
    hccapx_files();
    assert(strcmp(hs_capture_reason_name(HS_CAPTURE_REASON_NO_MATCHING_PAIR), "no_matching_pair") == 0);
    assert(strcmp(hs_capture_reason_name(HS_CAPTURE_REASON_TRUNCATED), "truncated") == 0);
    assert(strcmp(hs_capture_reason_name(HS_CAPTURE_REASON_IO_ERROR), "io_error") == 0);
    assert(strcmp(hs_capture_reason_name((hs_capture_reason_t)999), "unknown") == 0);
    assert(unlink(path) == 0);
    puts("hs_capture_analyzer: all tests passed");
    return 0;
}
