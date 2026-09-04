#include "pcap_summary.h"

#include <string.h>

#define PCAP_SUMMARY_PROGRESS_INTERVAL 64U

static unsigned dns_label_count(const char *name)
{
    if (!name || !name[0]) {
        return 0;
    }
    unsigned labels = 1;
    for (const char *cursor = name; *cursor; cursor++) {
        if (*cursor == '.') {
            labels++;
        }
    }
    return labels;
}

static void sort_summary_tables(pcap_summary_t *summary)
{
    pcap_topk_sort_text(summary->protocols, summary->protocol_count);
    pcap_topk_sort_text(summary->endpoints, summary->endpoint_count);
    pcap_topk_sort_port(summary->ports, summary->port_count);
    pcap_topk_sort_text(summary->dns_domains, summary->dns_domain_count);
    pcap_topk_sort_text(summary->dns_answers, summary->dns_answer_count);
    pcap_topk_sort_text(summary->dns_clients, summary->dns_client_count);
    pcap_topk_sort_text(summary->dns_servers, summary->dns_server_count);
    pcap_topk_sort_text(summary->dns_types, summary->dns_type_count);
    pcap_topk_sort_text(summary->host_pairs, summary->host_pair_count);
    pcap_topk_sort_text(summary->services, summary->service_count);
}

/* The window reducers need the capture span before the first sample lands, and
 * an index is not guaranteed to be in timestamp order, so the span comes from a
 * pass over the record headers. No file I/O happens here. */
static void configure_windows(pcap_summary_t *summary,
                              const pcap_packet_index_t *packets, size_t packet_count,
                              pcap_timestamp_resolution_t resolution)
{
    uint64_t first = 0;
    uint64_t last = 0;
    for (size_t i = 0; i < packet_count; i++) {
        uint64_t stamp = pcap_reader_packet_time_us(&packets[i], resolution);
        if (i == 0 || stamp < first) first = stamp;
        if (i == 0 || stamp > last) last = stamp;
    }
    summary->first_timestamp_us = first;
    summary->last_timestamp_us = last;
    pcap_window_init(&summary->packet_window, first, last);
    pcap_window_init(&summary->dns_nxdomain_window, first, last);
    pcap_window_init(&summary->deauthentication_window, first, last);
}

static void finish_ratios(pcap_summary_t *summary)
{
    const uint64_t analyzed = summary->analyzed_packets;
    const uint64_t minimum = PCAP_SUMMARY_MIN_RATIO_SAMPLES;

    summary->tcp_share = pcap_reduce_ratio(summary->tcp_packets, analyzed, minimum);
    summary->udp_share = pcap_reduce_ratio(summary->udp_packets, analyzed, minimum);
    summary->malformed_ratio =
        pcap_reduce_ratio(summary->malformed_packets, analyzed, minimum);
    summary->truncated_ratio =
        pcap_reduce_ratio(summary->truncated_packets, analyzed, minimum);
    summary->dns_answered_ratio =
        pcap_reduce_ratio(summary->dns_responses, summary->dns_queries, minimum);
    summary->dns_nxdomain_ratio =
        pcap_reduce_ratio(summary->dns_nxdomain, summary->dns_responses, minimum);
    summary->top_pair_share = pcap_reduce_ratio(
        summary->host_pair_count ? summary->host_pairs[0].count : 0U, analyzed, minimum);
    summary->top_talker_share = pcap_reduce_ratio(
        summary->endpoint_count ? summary->endpoints[0].count : 0U, analyzed, minimum);
}

pcap_reader_status_t pcap_summary_build(pcap_reader_t *reader,
                                        const pcap_packet_index_t *packets,
                                        size_t packet_count,
                                        pcap_summary_t *summary_out,
                                        uint32_t *packet_flags_out,
                                        size_t packet_flags_capacity,
                                        const volatile bool *cancel_requested,
                                        pcap_summary_progress_cb_t progress_cb,
                                        void *progress_ctx)
{
    if (!reader || !summary_out || (packet_count > 0 && !packets) ||
        (packet_count > packet_flags_capacity) ||
        (packet_flags_capacity > 0 && !packet_flags_out)) {
        return PCAP_READER_INVALID_ARG;
    }
    memset(summary_out, 0, sizeof(*summary_out));
    if (packet_flags_out && packet_flags_capacity > 0) {
        memset(packet_flags_out, 0, packet_flags_capacity * sizeof(packet_flags_out[0]));
    }

    pcap_timestamp_resolution_t resolution = pcap_reader_timestamp_resolution(reader);
    configure_windows(summary_out, packets, packet_count, resolution);

    for (size_t i = 0; i < packet_count; i++) {
        if (cancel_requested && *cancel_requested) {
            return PCAP_READER_CANCELLED;
        }
        pcap_packet_details_t details;
        pcap_reader_status_t status = pcap_reader_describe_packet(reader, &packets[i], &details);
        if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) {
            return status;
        }
        if (status == PCAP_READER_LIMIT_REACHED) {
            summary_out->decode_limited_packets++;
        }
        packet_flags_out[i] = details.flags;
        summary_out->analyzed_packets++;
        summary_out->captured_bytes += packets[i].captured_length;
        summary_out->original_bytes += packets[i].original_length;

        uint64_t timestamp_us = pcap_reader_packet_time_us(&packets[i], resolution);
        pcap_window_add(&summary_out->packet_window, timestamp_us);

        if (details.flags & PCAP_PACKET_FLAG_MALFORMED) summary_out->malformed_packets++;
        if (details.flags & PCAP_PACKET_FLAG_TRUNCATED) summary_out->truncated_packets++;
        if (details.flags & PCAP_PACKET_FLAG_TCP) summary_out->tcp_packets++;
        if (details.flags & PCAP_PACKET_FLAG_UDP) summary_out->udp_packets++;
        if (details.flags & PCAP_PACKET_FLAG_ARP) summary_out->arp_packets++;
        if (details.flags & PCAP_PACKET_FLAG_HTTP) summary_out->http_packets++;
        if (details.flags & PCAP_PACKET_FLAG_TLS) summary_out->tls_packets++;
        if (details.flags & PCAP_PACKET_FLAG_EAPOL) summary_out->eapol_packets++;
        if (details.flags & PCAP_PACKET_FLAG_WIFI_MGMT) {
            summary_out->wifi_management_packets++;
            if (details.wifi_frame_subtype == 8) summary_out->wifi_beacons++;
            if (details.wifi_frame_subtype == 4) summary_out->wifi_probe_requests++;
            if (details.wifi_frame_subtype == 5) summary_out->wifi_probe_responses++;
            if (details.wifi_frame_subtype == 12) {
                summary_out->wifi_deauthentications++;
                pcap_window_add(&summary_out->deauthentication_window, timestamp_us);
            }
        }

        pcap_topk_add_text(summary_out->protocols, PCAP_SUMMARY_MAX_PROTOCOLS,
                           &summary_out->protocol_count,
                           details.protocol[0] ? details.protocol : "Unknown",
                           packets[i].captured_length,
                           &summary_out->protocol_table_approximate);
        /* Endpoint labels stay exactly as the decoder printed them, so the
         * packet table, the flow views and this table keep naming a host the
         * same way. Normalization is applied to the derived keys below. */
        pcap_topk_add_text(summary_out->endpoints, PCAP_SUMMARY_MAX_ENDPOINTS,
                           &summary_out->endpoint_count, details.source,
                           packets[i].captured_length,
                           &summary_out->endpoint_table_approximate);
        pcap_topk_add_text(summary_out->endpoints, PCAP_SUMMARY_MAX_ENDPOINTS,
                           &summary_out->endpoint_count, details.destination,
                           packets[i].captured_length,
                           &summary_out->endpoint_table_approximate);
        pcap_topk_add_port(summary_out->ports, PCAP_SUMMARY_MAX_PORTS,
                           &summary_out->port_count, details.source_port,
                           details.ip_protocol, &summary_out->port_table_approximate);
        pcap_topk_add_port(summary_out->ports, PCAP_SUMMARY_MAX_PORTS,
                           &summary_out->port_count, details.destination_port,
                           details.ip_protocol, &summary_out->port_table_approximate);

        char key[PCAP_KEY_TEXT_MAX];
        if (pcap_key_host(details.source, key, sizeof(key))) {
            pcap_unique_add(&summary_out->unique_endpoints, key);
        } else if (details.source[0]) {
            summary_out->key_normalization_skipped = true;
        }
        if (pcap_key_host(details.destination, key, sizeof(key))) {
            pcap_unique_add(&summary_out->unique_endpoints, key);
        } else if (details.destination[0]) {
            summary_out->key_normalization_skipped = true;
        }
        if (pcap_key_host_pair(details.source, details.destination, key, sizeof(key))) {
            pcap_unique_add(&summary_out->unique_host_pairs, key);
            pcap_topk_add_text(summary_out->host_pairs, PCAP_SUMMARY_MAX_HOST_PAIRS,
                               &summary_out->host_pair_count, key,
                               packets[i].captured_length,
                               &summary_out->host_pair_table_approximate);
        } else if (details.source[0] && details.destination[0]) {
            summary_out->key_normalization_skipped = true;
            summary_out->host_pair_table_approximate = true;
        }
        /* One service per packet, taken from the lower port. The client side of
         * a connection is an ephemeral number that says nothing about the
         * service, and counting both ends would fill the table with them. */
        uint16_t service_port = details.source_port;
        if (details.destination_port &&
            (service_port == 0U || details.destination_port < service_port)) {
            service_port = details.destination_port;
        }
        if (pcap_key_service(service_port, details.ip_protocol, key, sizeof(key))) {
            pcap_topk_add_text(summary_out->services, PCAP_SUMMARY_MAX_SERVICES,
                               &summary_out->service_count, key,
                               packets[i].captured_length,
                               &summary_out->service_table_approximate);
        }

        if (details.flags & PCAP_PACKET_FLAG_DNS) {
            summary_out->dns_packets++;
            if (details.dns_valid) {
                if (details.dns_response) {
                    summary_out->dns_responses++;
                    if (details.dns_rcode == 0) summary_out->dns_noerror++;
                    else if (details.dns_rcode == 2) summary_out->dns_servfail++;
                    else if (details.dns_rcode == 3) {
                        summary_out->dns_nxdomain++;
                        pcap_window_add(&summary_out->dns_nxdomain_window, timestamp_us);
                    } else summary_out->dns_other_rcode++;
                } else {
                    summary_out->dns_queries++;
                }
                if (details.dns_query[0]) {
                    char domain[PCAP_KEY_TEXT_MAX];
                    const char *domain_key = details.dns_query;
                    if (pcap_key_domain(details.dns_query, domain, sizeof(domain))) {
                        domain_key = domain;
                    } else {
                        summary_out->key_normalization_skipped = true;
                    }
                    pcap_topk_add_text(summary_out->dns_domains,
                                       PCAP_SUMMARY_MAX_DNS_DOMAINS,
                                       &summary_out->dns_domain_count,
                                       domain_key, 0,
                                       &summary_out->dns_domain_table_approximate);
                    pcap_unique_add(&summary_out->unique_domains, domain_key);
                    size_t query_length = strlen(domain_key);
                    if (query_length >= 50U) summary_out->dns_long_names++;
                    if (dns_label_count(domain_key) >= 5U) {
                        summary_out->dns_many_label_names++;
                    }
                }
                if (details.dns_first_answer[0]) {
                    pcap_topk_add_text(summary_out->dns_answers,
                                       PCAP_SUMMARY_MAX_DNS_ANSWERS,
                                       &summary_out->dns_answer_count,
                                       details.dns_first_answer, 0,
                                       &summary_out->dns_answer_table_approximate);
                }
                pcap_topk_add_text(summary_out->dns_types, PCAP_SUMMARY_MAX_DNS_TYPES,
                                   &summary_out->dns_type_count,
                                   pcap_reader_dns_type_name(details.dns_qtype), 0,
                                   &summary_out->dns_type_table_approximate);
                const char *client = details.dns_response
                                         ? details.destination : details.source;
                const char *server = details.dns_response
                                         ? details.source : details.destination;
                pcap_topk_add_text(summary_out->dns_clients, PCAP_SUMMARY_MAX_DNS_PEERS,
                                   &summary_out->dns_client_count, client, 0,
                                   &summary_out->dns_client_table_approximate);
                pcap_topk_add_text(summary_out->dns_servers, PCAP_SUMMARY_MAX_DNS_PEERS,
                                   &summary_out->dns_server_count, server, 0,
                                   &summary_out->dns_server_table_approximate);
            }
        }

        if (progress_cb && ((i + 1U) % PCAP_SUMMARY_PROGRESS_INTERVAL) == 0) {
            progress_cb(i + 1U, packet_count, progress_ctx);
        }
    }

    /* The sketch counts a re-observed key once, which the bounded domain table
     * cannot do after it starts evicting. */
    summary_out->dns_unique_domains_observed = summary_out->unique_domains.distinct;

    sort_summary_tables(summary_out);
    finish_ratios(summary_out);
    if (progress_cb) {
        progress_cb(packet_count, packet_count, progress_ctx);
    }
    return PCAP_READER_OK;
}
