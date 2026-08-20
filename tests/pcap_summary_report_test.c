// Host integration test: synthesized PCAP -> pcap_reader -> pcap_summary ->
// deterministic English report.
//
// Covers the integration list of docs/PCAP_Analysis_and_Implementation_Plan.md
// section 22.9: a text baseline, an empty capture, a truncated global header, a
// truncated last record, malformed protocol data, an NXDOMAIN burst, one-flow
// dominance, high cardinality and byte-for-byte determinism.
//
// The fixtures are written to disk because pcap_reader is a file reader; they
// go to the directory given as argv[1], or /tmp by default.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcap_reader.h"
#include "pcap_summary.h"
#include "pcap_summary_report.h"

static int failures = 0;
static char fixture_dir[512] = "/tmp";

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

/* ------------------------------------------------------------- fixtures --- */

#define CAPTURE_EPOCH 1723459200UL   /* fixed, so the report never depends on "now" */

static const uint8_t mac_client[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t mac_router[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const uint8_t ip_client[4]  = {192, 168, 1, 10};
static const uint8_t ip_router[4]  = {192, 168, 1, 1};
static const uint8_t ip_server[4]  = {93, 184, 216, 34};

static void put_be16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)(value & 0xFFU);
}

static void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

// Each path gets its own buffer: a shared static one silently aliases two
// fixtures onto the same file the moment a test needs both at once.
#define FIXTURE_PATH(variable, name) \
    char variable[640]; \
    snprintf(variable, sizeof(variable), "%s/%s", fixture_dir, (name))

static FILE *pcap_create(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) return NULL;
    uint8_t header[24];
    put_le32(header, 0xA1B2C3D4UL);      /* little-endian magic, microsecond stamps */
    header[4] = 2; header[5] = 0;        /* version 2.4 */
    header[6] = 4; header[7] = 0;
    memset(header + 8, 0, 8);            /* thiszone, sigfigs */
    put_le32(header + 16, 65535UL);      /* snaplen */
    put_le32(header + 20, 1UL);          /* LINKTYPE_ETHERNET */
    fwrite(header, 1, sizeof(header), file);
    return file;
}

static void pcap_write(FILE *file, uint32_t seconds, uint32_t microseconds,
                       const uint8_t *frame, size_t length)
{
    uint8_t header[16];
    put_le32(header, seconds);
    put_le32(header + 4, microseconds);
    put_le32(header + 8, (uint32_t)length);
    put_le32(header + 12, (uint32_t)length);
    fwrite(header, 1, sizeof(header), file);
    fwrite(frame, 1, length, file);
}

static size_t build_ethernet(uint8_t *frame, const uint8_t *destination,
                             const uint8_t *source, uint16_t ether_type)
{
    memcpy(frame, destination, 6);
    memcpy(frame + 6, source, 6);
    put_be16(frame + 12, ether_type);
    return 14U;
}

/* Header checksums are left at zero: the decoder under test reads fields and
 * never validates the checksum, and a fixed zero keeps the fixtures readable. */
static size_t build_ipv4(uint8_t *frame, size_t offset, uint8_t protocol,
                         const uint8_t *source, const uint8_t *destination,
                         size_t payload_length)
{
    uint8_t *ip = frame + offset;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    put_be16(ip + 2, (uint16_t)(20U + payload_length));
    ip[8] = 64;                       /* TTL */
    ip[9] = protocol;
    memcpy(ip + 12, source, 4);
    memcpy(ip + 16, destination, 4);
    return offset + 20U;
}

static size_t build_udp(uint8_t *frame, size_t offset, uint16_t source_port,
                        uint16_t destination_port, const uint8_t *payload,
                        size_t payload_length)
{
    uint8_t *udp = frame + offset;
    put_be16(udp, source_port);
    put_be16(udp + 2, destination_port);
    put_be16(udp + 4, (uint16_t)(8U + payload_length));
    put_be16(udp + 6, 0);
    memcpy(udp + 8, payload, payload_length);
    return offset + 8U + payload_length;
}

static size_t build_tcp(uint8_t *frame, size_t offset, uint16_t source_port,
                        uint16_t destination_port, uint8_t flags,
                        const uint8_t *payload, size_t payload_length)
{
    uint8_t *tcp = frame + offset;
    memset(tcp, 0, 20);
    put_be16(tcp, source_port);
    put_be16(tcp + 2, destination_port);
    put_be16(tcp + 4, 0x0001);        /* sequence */
    tcp[12] = 0x50;                   /* data offset 5 words */
    tcp[13] = flags;
    put_be16(tcp + 14, 8192);         /* window */
    if (payload_length) memcpy(tcp + 20, payload, payload_length);
    return offset + 20U + payload_length;
}

/* "www.example.com" -> 3www7example3com0 */
static size_t encode_dns_name(uint8_t *out, const char *name)
{
    size_t position = 0;
    const char *label = name;
    while (*label) {
        const char *dot = strchr(label, '.');
        size_t length = dot ? (size_t)(dot - label) : strlen(label);
        out[position++] = (uint8_t)length;
        memcpy(out + position, label, length);
        position += length;
        if (!dot) break;
        label = dot + 1;
    }
    out[position++] = 0;
    return position;
}

static size_t build_dns_message(uint8_t *out, uint16_t identifier, const char *name,
                                bool response, uint8_t rcode, bool with_answer)
{
    put_be16(out, identifier);
    put_be16(out + 2, (uint16_t)((response ? 0x8180U : 0x0100U) | rcode));
    put_be16(out + 4, 1);                                   /* questions */
    put_be16(out + 6, (uint16_t)(with_answer ? 1 : 0));     /* answers */
    put_be16(out + 8, 0);
    put_be16(out + 10, 0);
    size_t position = 12U + encode_dns_name(out + 12, name);
    put_be16(out + position, 1);      /* A */
    put_be16(out + position + 2, 1);  /* IN */
    position += 4U;
    if (with_answer) {
        put_be16(out + position, 0xC00CU);      /* pointer back to the question */
        put_be16(out + position + 2, 1);
        put_be16(out + position + 4, 1);
        out[position + 6] = 0; out[position + 7] = 0;
        out[position + 8] = 0; out[position + 9] = 60;      /* TTL */
        put_be16(out + position + 10, 4);
        memcpy(out + position + 12, ip_server, 4);
        position += 16U;
    }
    return position;
}

static void write_dns_packet(FILE *file, uint32_t seconds, uint32_t microseconds,
                             uint16_t identifier, const char *name, bool response,
                             uint8_t rcode, bool with_answer)
{
    uint8_t message[256];
    size_t message_length = build_dns_message(message, identifier, name, response,
                                              rcode, with_answer);
    uint8_t frame[512];
    size_t length = build_ethernet(frame,
                                   response ? mac_client : mac_router,
                                   response ? mac_router : mac_client, 0x0800U);
    length = build_ipv4(frame, length, 17U,
                        response ? ip_router : ip_client,
                        response ? ip_client : ip_router,
                        8U + message_length);
    length = build_udp(frame, length, response ? 53U : 51000U,
                       response ? 51000U : 53U, message, message_length);
    pcap_write(file, seconds, microseconds, frame, length);
}

static void write_tcp_packet(FILE *file, uint32_t seconds, uint32_t microseconds,
                             uint16_t source_port, uint16_t destination_port,
                             bool from_client, const char *payload)
{
    size_t payload_length = payload ? strlen(payload) : 0U;
    uint8_t frame[1024];
    size_t length = build_ethernet(frame,
                                   from_client ? mac_router : mac_client,
                                   from_client ? mac_client : mac_router, 0x0800U);
    length = build_ipv4(frame, length, 6U,
                        from_client ? ip_client : ip_server,
                        from_client ? ip_server : ip_client,
                        20U + payload_length);
    length = build_tcp(frame, length, source_port, destination_port, 0x18U,
                       (const uint8_t *)payload, payload_length);
    pcap_write(file, seconds, microseconds, frame, length);
}

static void build_reference_capture(const char *path)
{
    FILE *file = pcap_create(path);
    if (!file) { printf("FAIL: cannot create %s\n", path); failures++; return; }

    /* ARP request. */
    uint8_t frame[128];
    size_t length = build_ethernet(frame, (const uint8_t *)"\xFF\xFF\xFF\xFF\xFF\xFF",
                                   mac_client, 0x0806U);
    uint8_t *arp = frame + length;
    put_be16(arp, 1); put_be16(arp + 2, 0x0800U);
    arp[4] = 6; arp[5] = 4; put_be16(arp + 6, 1);
    memcpy(arp + 8, mac_client, 6);
    memcpy(arp + 14, ip_client, 4);
    memset(arp + 18, 0, 6);
    memcpy(arp + 24, ip_router, 4);
    pcap_write(file, CAPTURE_EPOCH, 0U, frame, length + 28U);

    /* The same name in two cases: without key normalization these would be two
     * rows in the domain table. */
    write_dns_packet(file, CAPTURE_EPOCH, 100000U, 0x1234U, "WWW.Example.COM", false, 0, false);
    write_dns_packet(file, CAPTURE_EPOCH, 200000U, 0x1234U, "www.example.com", true, 0, true);
    write_dns_packet(file, CAPTURE_EPOCH, 300000U, 0x1235U, "missing.example.com", false, 0, false);
    write_dns_packet(file, CAPTURE_EPOCH, 400000U, 0x1235U, "missing.example.com", true, 3, false);

    /* TLS ClientHello prefix and plain HTTP, so both application flags fire. */
    const char tls_hello[] = "\x16\x03\x01\x00\x2c\x01\x00\x00\x28\x03\x03";
    uint8_t tls_frame[256];
    size_t tls_length = build_ethernet(tls_frame, mac_router, mac_client, 0x0800U);
    tls_length = build_ipv4(tls_frame, tls_length, 6U, ip_client, ip_server,
                            20U + sizeof(tls_hello) - 1U);
    tls_length = build_tcp(tls_frame, tls_length, 51001U, 443U, 0x18U,
                           (const uint8_t *)tls_hello, sizeof(tls_hello) - 1U);
    pcap_write(file, CAPTURE_EPOCH, 500000U, tls_frame, tls_length);

    write_tcp_packet(file, CAPTURE_EPOCH, 600000U, 51002U, 80U, true,
                     "GET /index.html HTTP/1.1\r\nHost: test.local\r\n\r\n");
    write_tcp_packet(file, CAPTURE_EPOCH, 700000U, 80U, 51002U, false,
                     "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");

    /* One NTP exchange, so the service table has a third entry. */
    const uint8_t ntp[8] = {0x1B, 0, 0, 0, 0, 0, 0, 0};
    uint8_t ntp_frame[128];
    size_t ntp_length = build_ethernet(ntp_frame, mac_router, mac_client, 0x0800U);
    ntp_length = build_ipv4(ntp_frame, ntp_length, 17U, ip_client, ip_router, 8U + sizeof(ntp));
    ntp_length = build_udp(ntp_frame, ntp_length, 51003U, 123U, ntp, sizeof(ntp));
    pcap_write(file, CAPTURE_EPOCH, 800000U, ntp_frame, ntp_length);

    fclose(file);
}

/* ------------------------------------------------------------- analysis --- */

typedef struct {
    pcap_capture_info_t capture_info;
    pcap_scan_summary_t scan;
    pcap_summary_t summary;
    pcap_reader_status_t open_status;
    pcap_reader_status_t scan_status;
    pcap_reader_status_t summary_status;
    char report[PCAP_SUMMARY_REPORT_SUGGESTED_SIZE];
} analysis_t;

#define ANALYSIS_INDEX_CAPACITY 4096U

static bool analyze(const char *path, analysis_t *out)
{
    memset(out, 0, sizeof(*out));
    pcap_reader_t *reader = NULL;
    out->open_status = pcap_reader_open(path, &reader, &out->capture_info);
    if (out->open_status != PCAP_READER_OK) return false;

    pcap_packet_index_t *index = calloc(ANALYSIS_INDEX_CAPACITY, sizeof(*index));
    uint32_t *flags = calloc(ANALYSIS_INDEX_CAPACITY, sizeof(*flags));
    if (!index || !flags) {
        free(index); free(flags); pcap_reader_close(reader);
        printf("FAIL: out of memory\n"); failures++;
        return false;
    }
    out->scan_status = pcap_reader_scan(reader, index, ANALYSIS_INDEX_CAPACITY,
                                        &out->scan, NULL, NULL, NULL);
    out->summary_status = pcap_summary_build(reader, index, out->scan.indexed_packets,
                                             &out->summary, flags,
                                             ANALYSIS_INDEX_CAPACITY, NULL, NULL, NULL);
    pcap_summary_render_report(&out->capture_info, &out->scan, &out->summary,
                               out->report, sizeof(out->report));
    free(index);
    free(flags);
    pcap_reader_close(reader);
    return true;
}

/* --------------------------------------------------------------- tests --- */

// Byte-for-byte expectation for the reference capture. Any change to the
// renderer has to be reviewed and pasted here on purpose.
static const char reference_report[] =
"ESPShark capture summary v1\n"
"===========================\n"
"\n"
"Capture\n"
"  link type          Ethernet (1)\n"
"  format             little endian, microsecond timestamps, snaplen 65535\n"
"  size               9 record(s), 676 captured byte(s)\n"
"  duration           0.800 s\n"
"  analysis           9 record(s) analyzed in detail\n"
"  completeness       complete\n"
"\n"
"Traffic\n"
"  analyzed packets   9\n"
"  bytes              676 captured, 676 on the wire\n"
"  unique endpoints   5\n"
"  unique host pairs  3\n"
"  busiest pair       192.168.1.1 <-> 192.168.1.10\n"
"  pair dominance     55.6% (5/9, low sample)\n"
"  busiest endpoint   192.168.1.10\n"
"  talker dominance   88.9% (8/9, low sample)\n"
"  TCP share          33.3% (3/9, low sample)\n"
"  UDP share          55.6% (5/9, low sample)\n"
"  Top host pairs\n"
"    1. 192.168.1.1 <-> 192.168.1.10 - 5\n"
"    2. 192.168.1.10 <-> 93.184.216.34 - 3\n"
"    3. 02:11:22:33:44:55 <-> ff:ff:ff:ff:ff:ff - 1\n"
"  Top endpoints\n"
"    1. 192.168.1.10 - 8\n"
"    2. 192.168.1.1 - 5\n"
"    3. 93.184.216.34 - 3\n"
"    4. 02:11:22:33:44:55 - 1\n"
"    5. FF:FF:FF:FF:FF:FF - 1\n"
"\n"
"Protocols\n"
"  packet mix         TCP 3 | UDP 5 | ARP 1 | DNS 4 | HTTP 2 | TLS 1\n"
"  Top protocols\n"
"    1. DNS - 4\n"
"    2. HTTP - 2\n"
"    3. ARP - 1\n"
"    4. NTP - 1\n"
"    5. TLS - 1\n"
"  Top services\n"
"    1. dns/udp - 4\n"
"    2. http/tcp - 2\n"
"    3. https/tcp - 1\n"
"    4. ntp/udp - 1\n"
"  DNS\n"
"    volume           2 queries, 2 responses\n"
"    answered         100.0% (2/2, low sample)\n"
"    NXDOMAIN         50.0% (1/2, low sample)\n"
"    rcodes           NOERROR 1 | SERVFAIL 0 | other 0\n"
"    unique names     2\n"
"    Top DNS names\n"
"      1. missing.example.com - 2\n"
"      2. www.example.com - 2\n"
"    DNS query types\n"
"      1. A - 4\n"
"\n"
"Anomalies\n"
"  none observed\n"
"\n"
"IOC\n"
"  Observed indicators only. Nothing here was checked against an external threat feed, and no item is a verdict.\n"
"  - observed indicator: 2 cleartext HTTP packet(s); their payload is readable to anyone on the path\n";

static void test_reference_report(void)
{
    FIXTURE_PATH(path, "espshark_reference.pcap");
    build_reference_capture(path);

    analysis_t analysis;
    if (!analyze(path, &analysis)) {
        printf("FAIL: reference capture did not open (%s)\n",
               pcap_reader_status_name(analysis.open_status));
        failures++;
        return;
    }
    CHECK(analysis.scan_status == PCAP_READER_OK, "scan=%s",
          pcap_reader_status_name(analysis.scan_status));
    CHECK(analysis.summary_status == PCAP_READER_OK, "summary=%s",
          pcap_reader_status_name(analysis.summary_status));
    CHECK(analysis.summary.analyzed_packets == 9U, "analyzed=%llu",
          (unsigned long long)analysis.summary.analyzed_packets);

    // Key normalization folds "WWW.Example.COM" and "www.example.com" together.
    CHECK(analysis.summary.dns_domain_count == 2U, "domain rows=%u",
          analysis.summary.dns_domain_count);
    CHECK(analysis.summary.unique_domains.distinct == 2U, "unique domains=%llu",
          (unsigned long long)analysis.summary.unique_domains.distinct);
    CHECK(strcmp(analysis.summary.dns_domains[0].label, "www.example.com") == 0 ||
          strcmp(analysis.summary.dns_domains[1].label, "www.example.com") == 0,
          "lowercased domain missing");

    // Both directions of the resolver conversation collapse into one pair.
    CHECK(analysis.summary.host_pair_count == 3U, "pairs=%u",
          analysis.summary.host_pair_count);
    CHECK(analysis.summary.unique_host_pairs.distinct == 3U, "unique pairs=%llu",
          (unsigned long long)analysis.summary.unique_host_pairs.distinct);

    CHECK(analysis.summary.dns_queries == 2U && analysis.summary.dns_responses == 2U,
          "dns q=%llu r=%llu", (unsigned long long)analysis.summary.dns_queries,
          (unsigned long long)analysis.summary.dns_responses);
    CHECK(analysis.summary.dns_nxdomain == 1U, "nxdomain=%llu",
          (unsigned long long)analysis.summary.dns_nxdomain);
    CHECK(analysis.summary.http_packets == 2U && analysis.summary.tls_packets == 1U,
          "http=%llu tls=%llu", (unsigned long long)analysis.summary.http_packets,
          (unsigned long long)analysis.summary.tls_packets);

    // Services come from the normalized port/protocol key.
    bool has_https = false;
    bool has_ntp = false;
    for (uint16_t i = 0; i < analysis.summary.service_count; i++) {
        if (strcmp(analysis.summary.services[i].label, "https/tcp") == 0) has_https = true;
        if (strcmp(analysis.summary.services[i].label, "ntp/udp") == 0) has_ntp = true;
    }
    CHECK(has_https && has_ntp, "service keys missing (https=%d ntp=%d)", has_https, has_ntp);

    // The whole report is pinned byte for byte.
    if (strcmp(analysis.report, reference_report) != 0) {
        printf("FAIL %s:%d: report baseline mismatch. Actual report:\n"
               "----8<----\n%s----8<----\n", __FILE__, __LINE__, analysis.report);
        failures++;
    }

    // Determinism: a second run over the same file produces identical bytes.
    analysis_t second;
    if (analyze(path, &second)) {
        CHECK(strcmp(analysis.report, second.report) == 0, "report is not reproducible");
        CHECK(memcmp(&analysis.summary, &second.summary, sizeof(second.summary)) == 0,
              "summary is not reproducible");
    }
}

static void test_empty_capture(void)
{
    FIXTURE_PATH(path, "espshark_empty.pcap");
    FILE *file = pcap_create(path);
    if (file) fclose(file);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "empty capture failed to open");
    CHECK(analysis.scan.packet_count == 0U, "packets=%llu",
          (unsigned long long)analysis.scan.packet_count);
    CHECK(analysis.summary.analyzed_packets == 0U, "analyzed nonzero on empty capture");
    // Nothing observed must read as "no samples", never as a confident zero.
    CHECK(!analysis.summary.tcp_share.valid && !analysis.summary.dns_nxdomain_ratio.valid,
          "empty capture produced a valid ratio");
    CHECK(strstr(analysis.report, "n/a (no samples)") != NULL,
          "empty capture hides its missing denominators");
    CHECK(strstr(analysis.report, "none observed") != NULL,
          "empty capture has no empty-section marker");
    CHECK(strstr(analysis.report, "[report truncated]") == NULL,
          "empty report was truncated");
}

static void test_truncated_global_header(void)
{
    FIXTURE_PATH(path, "espshark_short_header.pcap");
    FILE *file = fopen(path, "wb");
    if (file) {
        uint8_t partial[10];
        put_le32(partial, 0xA1B2C3D4UL);
        memset(partial + 4, 0, sizeof(partial) - 4U);
        fwrite(partial, 1, sizeof(partial), file);
        fclose(file);
    }
    analysis_t analysis;
    bool opened = analyze(path, &analysis);
    CHECK(!opened, "a 10-byte file was accepted as a capture");
    CHECK(analysis.open_status == PCAP_READER_TRUNCATED ||
          analysis.open_status == PCAP_READER_INVALID_FORMAT,
          "open status=%s", pcap_reader_status_name(analysis.open_status));
}

static void test_truncated_last_record(void)
{
    FIXTURE_PATH(path, "espshark_cut_tail.pcap");
    FIXTURE_PATH(reference, "espshark_reference.pcap");
    build_reference_capture(reference);

    FILE *source = fopen(reference, "rb");
    FILE *target = fopen(path, "wb");
    if (source && target) {
        uint8_t buffer[4096];
        size_t read = fread(buffer, 1, sizeof(buffer), source);
        /* Drop the tail of the last record, keeping its header. */
        if (read > 20U) fwrite(buffer, 1, read - 20U, target);
    }
    if (source) fclose(source);
    if (target) fclose(target);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "truncated capture failed to open");
    CHECK(analysis.scan.truncated_tail, "truncated tail not reported");
    CHECK(analysis.scan.packet_count == 8U, "kept packets=%llu",
          (unsigned long long)analysis.scan.packet_count);
    CHECK(strstr(analysis.report, "ends inside a record") != NULL,
          "report hides the truncated tail");
}

static void test_malformed_records(void)
{
    FIXTURE_PATH(path, "espshark_malformed.pcap");
    FILE *file = pcap_create(path);
    if (!file) { printf("FAIL: cannot create %s\n", path); failures++; return; }

    /* An Ethernet header that claims IPv4 but carries four bytes of nothing. */
    uint8_t frame[64];
    size_t length = build_ethernet(frame, mac_router, mac_client, 0x0800U);
    memset(frame + length, 0xFF, 4);
    pcap_write(file, CAPTURE_EPOCH, 0U, frame, length + 4U);

    /* An IPv4 header whose total length lies about the payload. */
    length = build_ethernet(frame, mac_router, mac_client, 0x0800U);
    length = build_ipv4(frame, length, 6U, ip_client, ip_server, 1400U);
    pcap_write(file, CAPTURE_EPOCH, 10U, frame, length);

    /* A record whose captured length is larger than the bytes that follow: the
     * writer states 400 bytes and supplies 20. */
    uint8_t header[16];
    put_le32(header, CAPTURE_EPOCH);
    put_le32(header + 4, 20U);
    put_le32(header + 8, 400U);
    put_le32(header + 12, 400U);
    fwrite(header, 1, sizeof(header), file);
    fwrite(frame, 1, 20U, file);
    fclose(file);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "malformed capture failed to open");
    CHECK(analysis.summary_status == PCAP_READER_OK ||
          analysis.summary_status == PCAP_READER_TRUNCATED,
          "summary status=%s", pcap_reader_status_name(analysis.summary_status));
    CHECK(analysis.summary.malformed_packets > 0U || analysis.scan.malformed_records > 0U ||
          analysis.scan.truncated_tail,
          "nothing was flagged on a capture built out of broken records");
    CHECK(analysis.report[0] != '\0', "no report for a malformed capture");
}

static void test_nxdomain_burst(void)
{
    FIXTURE_PATH(path, "espshark_nxdomain.pcap");
    FILE *file = pcap_create(path);
    if (!file) { printf("FAIL: cannot create %s\n", path); failures++; return; }

    /* One quiet minute of traffic with 30 NXDOMAIN answers inside 30 ms. */
    for (unsigned i = 0; i < 30U; i++) {
        char name[64];
        snprintf(name, sizeof(name), "gen%03u.example.com", i);
        write_dns_packet(file, CAPTURE_EPOCH, 0U, (uint16_t)(0x2000U + i), name, false, 0, false);
        write_dns_packet(file, CAPTURE_EPOCH, 1000U * i, (uint16_t)(0x2000U + i),
                         name, true, 3, false);
    }
    for (unsigned i = 0; i < 30U; i++) {
        write_tcp_packet(file, CAPTURE_EPOCH + 30U + i, 0U, 51100U, 443U, true, NULL);
    }
    fclose(file);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "NXDOMAIN capture failed to open");
    CHECK(analysis.summary.dns_nxdomain == 30U, "nxdomain=%llu",
          (unsigned long long)analysis.summary.dns_nxdomain);
    CHECK(analysis.summary.dns_nxdomain_ratio.valid &&
          !analysis.summary.dns_nxdomain_ratio.low_sample,
          "NXDOMAIN ratio not usable");
    CHECK(pcap_window_burst_score(&analysis.summary.dns_nxdomain_window) > 5.0,
          "burst score=%.2f",
          pcap_window_burst_score(&analysis.summary.dns_nxdomain_window));
    CHECK(strstr(analysis.report, "NXDOMAIN burst") != NULL,
          "report does not name the NXDOMAIN burst");
    CHECK(strstr(analysis.report, "sustained NXDOMAIN share") != NULL,
          "report does not raise the NXDOMAIN indicator");
    // An indicator is never a verdict.
    CHECK(strstr(analysis.report, "observed indicator") != NULL,
          "indicator wording lost");
}

static void test_one_flow_dominance(void)
{
    FIXTURE_PATH(path, "espshark_dominance.pcap");
    FILE *file = pcap_create(path);
    if (!file) { printf("FAIL: cannot create %s\n", path); failures++; return; }
    for (unsigned i = 0; i < 95U; i++) {
        write_tcp_packet(file, CAPTURE_EPOCH + i / 10U, (i % 10U) * 100000U,
                         51200U, 443U, true, NULL);
    }
    for (unsigned i = 0; i < 5U; i++) {
        write_dns_packet(file, CAPTURE_EPOCH + 10U + i, 0U, (uint16_t)(0x3000U + i),
                         "example.org", false, 0, false);
    }
    fclose(file);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "dominance capture failed to open");
    CHECK(analysis.summary.top_pair_share.valid &&
          !analysis.summary.top_pair_share.low_sample,
          "dominance ratio not usable");
    CHECK(analysis.summary.top_pair_share.value > 0.9,
          "dominance=%.3f", analysis.summary.top_pair_share.value);
    CHECK(strstr(analysis.report, "one host pair carries") != NULL,
          "report does not call out one-pair dominance");
}

static void test_high_cardinality(void)
{
    FIXTURE_PATH(path, "espshark_cardinality.pcap");
    FILE *file = pcap_create(path);
    if (!file) { printf("FAIL: cannot create %s\n", path); failures++; return; }
    for (unsigned i = 0; i < 200U; i++) {
        char name[64];
        snprintf(name, sizeof(name), "host%03u.cdn.example", i);
        write_dns_packet(file, CAPTURE_EPOCH, 1000U * i, (uint16_t)(0x4000U + i),
                         name, false, 0, false);
    }
    fclose(file);

    analysis_t analysis;
    CHECK(analyze(path, &analysis), "cardinality capture failed to open");
    CHECK(analysis.summary.dns_domain_count == PCAP_SUMMARY_MAX_DNS_DOMAINS,
          "domain rows=%u", analysis.summary.dns_domain_count);
    CHECK(analysis.summary.dns_domain_table_approximate,
          "a table that had to evict is not marked approximate");
    // The bounded table stops at its capacity; the sketch keeps counting, which
    // is the whole reason both numbers exist.
    CHECK(analysis.summary.dns_unique_domains_observed >= 190U &&
          analysis.summary.dns_unique_domains_observed <= 200U,
          "unique domains=%llu",
          (unsigned long long)analysis.summary.dns_unique_domains_observed);
    CHECK(strstr(analysis.report, "(approximate)") != NULL,
          "report hides that a table was capped");
}

static void test_report_buffer_limits(void)
{
    FIXTURE_PATH(path, "espshark_reference.pcap");
    build_reference_capture(path);

    pcap_reader_t *reader = NULL;
    pcap_capture_info_t info;
    if (pcap_reader_open(path, &reader, &info) != PCAP_READER_OK) {
        printf("FAIL: cannot reopen the reference capture\n");
        failures++;
        return;
    }
    pcap_scan_summary_t scan;
    pcap_packet_index_t *index = calloc(ANALYSIS_INDEX_CAPACITY, sizeof(*index));
    uint32_t *flags = calloc(ANALYSIS_INDEX_CAPACITY, sizeof(*flags));
    pcap_summary_t summary;
    pcap_reader_scan(reader, index, ANALYSIS_INDEX_CAPACITY, &scan, NULL, NULL, NULL);
    pcap_summary_build(reader, index, scan.indexed_packets, &summary, flags,
                       ANALYSIS_INDEX_CAPACITY, NULL, NULL, NULL);

    char small[400];
    size_t written = pcap_summary_render_report(&info, &scan, &summary, small, sizeof(small));
    CHECK(written < sizeof(small), "renderer wrote %zu into a %zu buffer",
          written, sizeof(small));
    CHECK(strstr(small, "[report truncated]") != NULL,
          "a clipped report does not say so");

    char tiny[4] = {'x', 'x', 'x', 'x'};
    CHECK(pcap_summary_render_report(&info, &scan, &summary, tiny, sizeof(tiny)) == 0,
          "renderer claimed output in a 4-byte buffer");
    CHECK(tiny[0] == '\0', "tiny buffer left unterminated");
    CHECK(pcap_summary_render_report(NULL, &scan, &summary, small, sizeof(small)) == 0,
          "renderer accepted a NULL capture");

    free(index);
    free(flags);
    pcap_reader_close(reader);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(fixture_dir, sizeof(fixture_dir), "%s", argv[1]);
    }
    test_reference_report();
    test_empty_capture();
    test_truncated_global_header();
    test_truncated_last_record();
    test_malformed_records();
    test_nxdomain_burst();
    test_one_flow_dominance();
    test_high_cardinality();
    test_report_buffer_limits();

    if (failures == 0) {
        printf("all pcap_summary report tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
