#include "pcap_summary_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define REPORT_TRUNCATION_NOTE "\n[report truncated]\n"
#define REPORT_RESERVE         (sizeof(REPORT_TRUNCATION_NOTE))

typedef struct {
    char *out;
    size_t limit;        /* usable space, terminator excluded */
    size_t position;
    bool truncated;
} report_writer_t;

/* Appends or gives up: a partially written line would change the output in a
 * way that depends on the buffer size, and the report has to stay comparable. */
static void report_append(report_writer_t *writer, const char *format, ...)
{
    if (!writer || writer->truncated) return;
    size_t available = writer->limit - writer->position;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->out + writer->position, available, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= available) {
        writer->out[writer->position] = '\0';
        writer->truncated = true;
        return;
    }
    writer->position += (size_t)written;
}

static double timestamp_seconds(uint32_t seconds, uint32_t fraction,
                                pcap_timestamp_resolution_t resolution)
{
    double scale = (resolution == PCAP_TIMESTAMP_NANOSECONDS) ? 1000000000.0 : 1000000.0;
    return (double)seconds + (double)fraction / scale;
}

static double capture_duration(const pcap_scan_summary_t *scan,
                               pcap_timestamp_resolution_t resolution)
{
    if (!scan || scan->packet_count == 0) return 0.0;
    double first = timestamp_seconds(scan->first_timestamp_seconds,
                                     scan->first_timestamp_fraction, resolution);
    double last = timestamp_seconds(scan->last_timestamp_seconds,
                                    scan->last_timestamp_fraction, resolution);
    return (last > first) ? last - first : 0.0;
}

static void append_ratio(report_writer_t *writer, const char *label,
                         const pcap_ratio_t *ratio)
{
    char text[64];
    pcap_ratio_format(ratio, text, sizeof(text));
    report_append(writer, "  %-18s %s\n", label, text);
}

/* A burst is only worth naming when the window actually holds enough samples to
 * separate a spike from ordinary jitter. */
static bool burst_is_notable(const pcap_window_t *window, uint64_t minimum_total,
                             double minimum_score)
{
    return window && window->configured && window->total >= minimum_total &&
           pcap_window_burst_score(window) >= minimum_score;
}

static void append_text_top(report_writer_t *writer, unsigned indent, const char *title,
                            const pcap_summary_text_counter_t *entries,
                            uint16_t count, uint16_t limit, bool approximate)
{
    report_append(writer, "%*s%s%s\n", (int)indent, "", title,
                  approximate ? " (approximate)" : "");
    if (count == 0) {
        report_append(writer, "%*snone observed\n", (int)indent + 2, "");
        return;
    }
    uint16_t shown = count < limit ? count : limit;
    for (uint16_t i = 0; i < shown; i++) {
        report_append(writer, "%*s%u. %s - %llu\n", (int)indent + 2, "",
                      (unsigned)(i + 1U), entries[i].label,
                      (unsigned long long)entries[i].count);
    }
    if (shown < count) {
        report_append(writer, "%*s(%u more tracked)\n", (int)indent + 2, "",
                      (unsigned)(count - shown));
    }
}

static void append_burst(report_writer_t *writer, const char *label,
                         const pcap_window_t *window)
{
    uint16_t peak_index = 0;
    uint32_t peak = pcap_window_peak(window, &peak_index);
    report_append(writer,
                  "  %-18s %lu in one %.3f s window (x%.1f the average, window %u of %u)\n",
                  label, (unsigned long)peak, pcap_window_bucket_seconds(window),
                  pcap_window_burst_score(window), (unsigned)(peak_index + 1U),
                  (unsigned)window->bucket_count);
}

/* Same numbers as append_burst(), phrased as a finding so the Anomalies bullets
 * read as one list instead of a mix of sentences and padded columns. */
static void append_burst_finding(report_writer_t *writer, const char *label,
                                 const char *noun, const pcap_window_t *window)
{
    report_append(writer, "  - %s: %lu %s(s) inside one %.3f s window, %.1fx the average\n",
                  label, (unsigned long)pcap_window_peak(window, NULL), noun,
                  pcap_window_bucket_seconds(window),
                  pcap_window_burst_score(window));
}

static void render_capture(report_writer_t *writer,
                           const pcap_capture_info_t *info,
                           const pcap_scan_summary_t *scan,
                           const pcap_summary_t *summary)
{
    double duration = capture_duration(scan, info->timestamp_resolution);
    report_append(writer, "ESPShark capture summary v%u\n", PCAP_SUMMARY_REPORT_VERSION);
    report_append(writer, "===========================\n\n");
    report_append(writer, "Capture\n");
    report_append(writer, "  %-18s %s (%lu)\n", "link type",
                  pcap_reader_link_type_name(info->link_type),
                  (unsigned long)info->link_type);
    report_append(writer, "  %-18s %s, %s timestamps, snaplen %lu\n", "format",
                  info->big_endian ? "big endian" : "little endian",
                  info->timestamp_resolution == PCAP_TIMESTAMP_NANOSECONDS
                      ? "nanosecond" : "microsecond",
                  (unsigned long)info->snaplen);
    report_append(writer, "  %-18s %llu record(s), %llu captured byte(s)\n", "size",
                  (unsigned long long)scan->packet_count,
                  (unsigned long long)scan->captured_bytes);
    report_append(writer, "  %-18s %.3f s\n", "duration", duration);
    report_append(writer, "  %-18s %llu record(s) analyzed in detail%s\n", "analysis",
                  (unsigned long long)summary->analyzed_packets,
                  summary->analyzed_packets < scan->packet_count
                      ? " (sample of the capture)" : "");
    if (scan->index_limited || scan->truncated_tail || scan->malformed_records > 0) {
        report_append(writer, "  %-18s%s%s%s\n", "completeness",
                      scan->index_limited ? " index limited" : "",
                      scan->truncated_tail ? " truncated tail" : "",
                      scan->malformed_records > 0 ? " malformed records" : "");
    } else {
        report_append(writer, "  %-18s complete\n", "completeness");
    }
}

static void render_traffic(report_writer_t *writer, const pcap_summary_t *summary)
{
    report_append(writer, "\nTraffic\n");
    report_append(writer, "  %-18s %llu\n", "analyzed packets",
                  (unsigned long long)summary->analyzed_packets);
    report_append(writer, "  %-18s %llu captured, %llu on the wire\n", "bytes",
                  (unsigned long long)summary->captured_bytes,
                  (unsigned long long)summary->original_bytes);
    report_append(writer, "  %-18s %llu%s\n", "unique endpoints",
                  (unsigned long long)summary->unique_endpoints.distinct,
                  summary->unique_endpoints.approximate ? " (approximate)" : "");
    report_append(writer, "  %-18s %llu%s\n", "unique host pairs",
                  (unsigned long long)summary->unique_host_pairs.distinct,
                  summary->unique_host_pairs.approximate ? " (approximate)" : "");
    if (summary->host_pair_count > 0) {
        report_append(writer, "  %-18s %s\n", "busiest pair",
                      summary->host_pairs[0].label);
    }
    append_ratio(writer, "pair dominance", &summary->top_pair_share);
    if (summary->endpoint_count > 0) {
        report_append(writer, "  %-18s %s\n", "busiest endpoint",
                      summary->endpoints[0].label);
    }
    append_ratio(writer, "talker dominance", &summary->top_talker_share);
    append_ratio(writer, "TCP share", &summary->tcp_share);
    append_ratio(writer, "UDP share", &summary->udp_share);
    /* Below this many samples a "peak" is one packet and the multiplier says
     * nothing, so the line is left out entirely rather than printed as noise. */
    if (burst_is_notable(&summary->packet_window, PCAP_SUMMARY_MIN_RATIO_SAMPLES, 0.0)) {
        append_burst(writer, "peak packets", &summary->packet_window);
    }
    append_text_top(writer, 2U, "Top host pairs", summary->host_pairs,
                    summary->host_pair_count, 5U,
                    summary->host_pair_table_approximate);
    append_text_top(writer, 2U, "Top endpoints", summary->endpoints,
                    summary->endpoint_count, 5U,
                    summary->endpoint_table_approximate);
}

static void render_protocols(report_writer_t *writer, const pcap_summary_t *summary)
{
    report_append(writer, "\nProtocols\n");
    report_append(writer,
                  "  %-18s TCP %llu | UDP %llu | ARP %llu | DNS %llu | HTTP %llu | TLS %llu\n",
                  "packet mix",
                  (unsigned long long)summary->tcp_packets,
                  (unsigned long long)summary->udp_packets,
                  (unsigned long long)summary->arp_packets,
                  (unsigned long long)summary->dns_packets,
                  (unsigned long long)summary->http_packets,
                  (unsigned long long)summary->tls_packets);
    if (summary->wifi_management_packets > 0 || summary->eapol_packets > 0) {
        report_append(writer,
                      "  %-18s management %llu | beacon %llu | probe req %llu | "
                      "probe resp %llu | deauth %llu | EAPOL %llu\n",
                      "802.11",
                      (unsigned long long)summary->wifi_management_packets,
                      (unsigned long long)summary->wifi_beacons,
                      (unsigned long long)summary->wifi_probe_requests,
                      (unsigned long long)summary->wifi_probe_responses,
                      (unsigned long long)summary->wifi_deauthentications,
                      (unsigned long long)summary->eapol_packets);
    }
    append_text_top(writer, 2U, "Top protocols", summary->protocols,
                    summary->protocol_count, 6U, summary->protocol_table_approximate);
    append_text_top(writer, 2U, "Top services", summary->services,
                    summary->service_count, 6U, summary->service_table_approximate);

    report_append(writer, "  DNS\n");
    report_append(writer, "    %-16s %llu queries, %llu responses\n", "volume",
                  (unsigned long long)summary->dns_queries,
                  (unsigned long long)summary->dns_responses);
    char text[64];
    pcap_ratio_format(&summary->dns_answered_ratio, text, sizeof(text));
    report_append(writer, "    %-16s %s\n", "answered", text);
    pcap_ratio_format(&summary->dns_nxdomain_ratio, text, sizeof(text));
    report_append(writer, "    %-16s %s\n", "NXDOMAIN", text);
    report_append(writer, "    %-16s NOERROR %llu | SERVFAIL %llu | other %llu\n", "rcodes",
                  (unsigned long long)summary->dns_noerror,
                  (unsigned long long)summary->dns_servfail,
                  (unsigned long long)summary->dns_other_rcode);
    report_append(writer, "    %-16s %llu%s\n", "unique names",
                  (unsigned long long)summary->dns_unique_domains_observed,
                  summary->unique_domains.approximate ? " (approximate)" : "");
    append_text_top(writer, 4U, "Top DNS names", summary->dns_domains,
                    summary->dns_domain_count, 6U,
                    summary->dns_domain_table_approximate);
    append_text_top(writer, 4U, "DNS query types", summary->dns_types,
                    summary->dns_type_count, 4U, summary->dns_type_table_approximate);
    if (summary->decode_limited_packets > 0) {
        report_append(writer,
                      "  Note: %llu packet(s) were decoded from their first bytes only, "
                      "so deeper layers are reported as unknown rather than absent.\n",
                      (unsigned long long)summary->decode_limited_packets);
    }
}

static void render_anomalies(report_writer_t *writer,
                             const pcap_scan_summary_t *scan,
                             const pcap_summary_t *summary)
{
    report_append(writer, "\nAnomalies\n");
    unsigned reported = 0;
    char text[64];

    if (summary->malformed_packets > 0) {
        pcap_ratio_format(&summary->malformed_ratio, text, sizeof(text));
        report_append(writer, "  - malformed frames: %llu, %s\n",
                      (unsigned long long)summary->malformed_packets, text);
        reported++;
    }
    if (summary->truncated_packets > 0) {
        pcap_ratio_format(&summary->truncated_ratio, text, sizeof(text));
        report_append(writer,
                      "  - frames cut by the capture snaplen: %llu, %s\n",
                      (unsigned long long)summary->truncated_packets, text);
        reported++;
    }
    if (scan->truncated_tail) {
        report_append(writer,
                      "  - the file ends inside a record; the last record was dropped\n");
        reported++;
    }
    if (scan->malformed_records > 0) {
        report_append(writer, "  - unusable record headers skipped during scan: %lu\n",
                      (unsigned long)scan->malformed_records);
        reported++;
    }
    /* Traffic already printed the peak window, so this only names it as a
     * finding instead of repeating the whole line. */
    if (burst_is_notable(&summary->packet_window, PCAP_SUMMARY_MIN_RATIO_SAMPLES, 4.0)) {
        report_append(writer,
                      "  - the packet rate is bursty: the busiest %.3f s window holds "
                      "%.1fx the average\n",
                      pcap_window_bucket_seconds(&summary->packet_window),
                      pcap_window_burst_score(&summary->packet_window));
        reported++;
    }
    if (burst_is_notable(&summary->dns_nxdomain_window, 10U, 3.0)) {
        append_burst_finding(writer, "NXDOMAIN burst", "answer",
                             &summary->dns_nxdomain_window);
        reported++;
    }
    if (burst_is_notable(&summary->deauthentication_window, 5U, 2.0)) {
        append_burst_finding(writer, "deauthentication burst", "frame",
                             &summary->deauthentication_window);
        reported++;
    }
    if (summary->top_pair_share.valid && !summary->top_pair_share.low_sample &&
        summary->top_pair_share.value >= 0.8) {
        pcap_ratio_format(&summary->top_pair_share, text, sizeof(text));
        report_append(writer, "  - one host pair carries %s of the analyzed traffic\n", text);
        reported++;
    }
    if (summary->dns_nxdomain_ratio.valid && !summary->dns_nxdomain_ratio.low_sample &&
        summary->dns_nxdomain_ratio.value >= 0.3) {
        pcap_ratio_format(&summary->dns_nxdomain_ratio, text, sizeof(text));
        report_append(writer, "  - DNS answers are mostly NXDOMAIN: %s\n", text);
        reported++;
    }
    if (summary->dns_queries > summary->dns_responses) {
        report_append(writer, "  - %llu DNS query/queries were never answered in this capture\n",
                      (unsigned long long)(summary->dns_queries - summary->dns_responses));
        reported++;
    }
    if (summary->packet_window.out_of_range > 0) {
        report_append(writer, "  - %llu record(s) carry a timestamp outside the capture span\n",
                      (unsigned long long)summary->packet_window.out_of_range);
        reported++;
    }
    if (summary->key_normalization_skipped) {
        report_append(writer,
                      "  - some endpoints could not be reduced to a canonical key, "
                      "so the unique counts are a floor\n");
        reported++;
    }
    if (reported == 0) {
        report_append(writer, "  none observed\n");
    }
}

static void render_indicators(report_writer_t *writer, const pcap_summary_t *summary)
{
    report_append(writer, "\nIOC\n");
    report_append(writer,
                  "  Observed indicators only. Nothing here was checked against an "
                  "external threat feed, and no item is a verdict.\n");
    unsigned reported = 0;
    char text[64];

    if (summary->dns_long_names > 0) {
        report_append(writer,
                      "  - observed indicator: %llu DNS name(s) of 50+ characters "
                      "(consistent with tunneling, also with CDN labels)\n",
                      (unsigned long long)summary->dns_long_names);
        reported++;
    }
    if (summary->dns_many_label_names > 0) {
        report_append(writer,
                      "  - observed indicator: %llu DNS name(s) with 5+ labels\n",
                      (unsigned long long)summary->dns_many_label_names);
        reported++;
    }
    if (summary->dns_nxdomain >= 10U && summary->dns_nxdomain_ratio.valid &&
        summary->dns_nxdomain_ratio.value >= 0.5) {
        pcap_ratio_format(&summary->dns_nxdomain_ratio, text, sizeof(text));
        report_append(writer,
                      "  - observed indicator: sustained NXDOMAIN share %s "
                      "(domain generation or a stale client)\n", text);
        reported++;
    }
    if (summary->http_packets > 0) {
        report_append(writer,
                      "  - observed indicator: %llu cleartext HTTP packet(s); "
                      "their payload is readable to anyone on the path\n",
                      (unsigned long long)summary->http_packets);
        reported++;
    }
    if (summary->wifi_deauthentications > 0) {
        report_append(writer,
                      "  - observed indicator: %llu deauthentication frame(s)\n",
                      (unsigned long long)summary->wifi_deauthentications);
        reported++;
    }
    if (summary->eapol_packets > 0) {
        report_append(writer,
                      "  - observed indicator: %llu EAPOL frame(s); a full handshake "
                      "is offline-crackable material\n",
                      (unsigned long long)summary->eapol_packets);
        reported++;
    }
    if (summary->malformed_ratio.valid && !summary->malformed_ratio.low_sample &&
        summary->malformed_ratio.value >= 0.05) {
        pcap_ratio_format(&summary->malformed_ratio, text, sizeof(text));
        report_append(writer,
                      "  - observed indicator: malformed share %s, which is high enough "
                      "to mean either a lossy capture or crafted frames\n", text);
        reported++;
    }
    if (reported == 0) {
        report_append(writer, "  none observed\n");
    }
}

size_t pcap_summary_render_report(const pcap_capture_info_t *capture_info,
                                  const pcap_scan_summary_t *scan_summary,
                                  const pcap_summary_t *summary,
                                  char *out, size_t out_size)
{
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    if (!capture_info || !scan_summary || !summary || out_size <= REPORT_RESERVE) {
        return 0;
    }
    report_writer_t writer = {
        .out = out,
        .limit = out_size - REPORT_RESERVE,
        .position = 0,
        .truncated = false,
    };
    render_capture(&writer, capture_info, scan_summary, summary);
    render_traffic(&writer, summary);
    render_protocols(&writer, summary);
    render_anomalies(&writer, scan_summary, summary);
    render_indicators(&writer, summary);
    if (writer.truncated) {
        int written = snprintf(out + writer.position, out_size - writer.position,
                               "%s", REPORT_TRUNCATION_NOTE);
        if (written > 0) writer.position += (size_t)written;
    }
    return writer.position;
}
