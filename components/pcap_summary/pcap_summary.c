#include "pcap_summary.h"

#include <stdlib.h>
#include <string.h>

#define PCAP_SUMMARY_PROGRESS_INTERVAL 64U

typedef enum {
    COUNTER_EXISTING = 0,
    COUNTER_INSERTED,
    COUNTER_REPLACED,
} counter_add_result_t;

static counter_add_result_t add_text_counter(pcap_summary_text_counter_t *entries,
                                             size_t capacity, uint16_t *used,
                                             const char *label, uint64_t bytes,
                                             bool *approximate)
{
    if (!entries || !used || !label || !label[0] || capacity == 0) {
        return COUNTER_EXISTING;
    }
    for (uint16_t i = 0; i < *used; i++) {
        if (strcmp(entries[i].label, label) == 0) {
            entries[i].count++;
            entries[i].bytes += bytes;
            return COUNTER_EXISTING;
        }
    }
    if (*used < capacity) {
        pcap_summary_text_counter_t *entry = &entries[(*used)++];
        size_t length = strnlen(label, sizeof(entry->label) - 1U);
        memcpy(entry->label, label, length);
        entry->label[length] = '\0';
        entry->count = 1;
        entry->bytes = bytes;
        return COUNTER_INSERTED;
    }

    size_t minimum = 0;
    for (size_t i = 1; i < capacity; i++) {
        if (entries[i].count < entries[minimum].count) {
            minimum = i;
        }
    }
    uint64_t previous_count = entries[minimum].count;
    size_t length = strnlen(label, sizeof(entries[minimum].label) - 1U);
    memcpy(entries[minimum].label, label, length);
    entries[minimum].label[length] = '\0';
    entries[minimum].count = previous_count + 1U;
    entries[minimum].bytes = bytes;
    if (approximate) {
        *approximate = true;
    }
    return COUNTER_REPLACED;
}

static void add_port_counter(pcap_summary_port_counter_t *entries, size_t capacity,
                             uint16_t *used, uint16_t port, uint8_t ip_protocol,
                             bool *approximate)
{
    if (!entries || !used || port == 0 || capacity == 0) {
        return;
    }
    for (uint16_t i = 0; i < *used; i++) {
        if (entries[i].port == port && entries[i].ip_protocol == ip_protocol) {
            entries[i].count++;
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
    entries[minimum].count = previous_count + 1U;
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
    qsort(summary->protocols, summary->protocol_count,
          sizeof(summary->protocols[0]), compare_text_counter);
    qsort(summary->endpoints, summary->endpoint_count,
          sizeof(summary->endpoints[0]), compare_text_counter);
    qsort(summary->ports, summary->port_count,
          sizeof(summary->ports[0]), compare_port_counter);
    qsort(summary->dns_domains, summary->dns_domain_count,
          sizeof(summary->dns_domains[0]), compare_text_counter);
    qsort(summary->dns_answers, summary->dns_answer_count,
          sizeof(summary->dns_answers[0]), compare_text_counter);
    qsort(summary->dns_clients, summary->dns_client_count,
          sizeof(summary->dns_clients[0]), compare_text_counter);
    qsort(summary->dns_servers, summary->dns_server_count,
          sizeof(summary->dns_servers[0]), compare_text_counter);
    qsort(summary->dns_types, summary->dns_type_count,
          sizeof(summary->dns_types[0]), compare_text_counter);
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
            if (details.wifi_frame_subtype == 12) summary_out->wifi_deauthentications++;
        }

        add_text_counter(summary_out->protocols, PCAP_SUMMARY_MAX_PROTOCOLS,
                         &summary_out->protocol_count,
                         details.protocol[0] ? details.protocol : "Unknown",
                         packets[i].captured_length,
                         &summary_out->protocol_table_approximate);
        add_text_counter(summary_out->endpoints, PCAP_SUMMARY_MAX_ENDPOINTS,
                         &summary_out->endpoint_count, details.source,
                         packets[i].captured_length,
                         &summary_out->endpoint_table_approximate);
        add_text_counter(summary_out->endpoints, PCAP_SUMMARY_MAX_ENDPOINTS,
                         &summary_out->endpoint_count, details.destination,
                         packets[i].captured_length,
                         &summary_out->endpoint_table_approximate);
        add_port_counter(summary_out->ports, PCAP_SUMMARY_MAX_PORTS,
                         &summary_out->port_count, details.source_port,
                         details.ip_protocol, &summary_out->port_table_approximate);
        add_port_counter(summary_out->ports, PCAP_SUMMARY_MAX_PORTS,
                         &summary_out->port_count, details.destination_port,
                         details.ip_protocol, &summary_out->port_table_approximate);

        if (details.flags & PCAP_PACKET_FLAG_DNS) {
            summary_out->dns_packets++;
            if (details.dns_valid) {
                if (details.dns_response) {
                    summary_out->dns_responses++;
                    if (details.dns_rcode == 0) summary_out->dns_noerror++;
                    else if (details.dns_rcode == 2) summary_out->dns_servfail++;
                    else if (details.dns_rcode == 3) summary_out->dns_nxdomain++;
                    else summary_out->dns_other_rcode++;
                } else {
                    summary_out->dns_queries++;
                }
                if (details.dns_query[0]) {
                    counter_add_result_t domain_result =
                        add_text_counter(summary_out->dns_domains,
                                         PCAP_SUMMARY_MAX_DNS_DOMAINS,
                                         &summary_out->dns_domain_count,
                                         details.dns_query, 0,
                                         &summary_out->dns_domain_table_approximate);
                    if (domain_result != COUNTER_EXISTING) {
                        summary_out->dns_unique_domains_observed++;
                    }
                    size_t query_length = strlen(details.dns_query);
                    if (query_length >= 50U) summary_out->dns_long_names++;
                    if (dns_label_count(details.dns_query) >= 5U) {
                        summary_out->dns_many_label_names++;
                    }
                }
                if (details.dns_first_answer[0]) {
                    add_text_counter(summary_out->dns_answers,
                                     PCAP_SUMMARY_MAX_DNS_ANSWERS,
                                     &summary_out->dns_answer_count,
                                     details.dns_first_answer, 0,
                                     &summary_out->dns_answer_table_approximate);
                }
                add_text_counter(summary_out->dns_types, PCAP_SUMMARY_MAX_DNS_TYPES,
                                 &summary_out->dns_type_count,
                                 pcap_reader_dns_type_name(details.dns_qtype), 0,
                                 &summary_out->dns_type_table_approximate);
                const char *client = details.dns_response
                                         ? details.destination : details.source;
                const char *server = details.dns_response
                                         ? details.source : details.destination;
                add_text_counter(summary_out->dns_clients, PCAP_SUMMARY_MAX_DNS_PEERS,
                                 &summary_out->dns_client_count, client, 0,
                                 &summary_out->dns_client_table_approximate);
                add_text_counter(summary_out->dns_servers, PCAP_SUMMARY_MAX_DNS_PEERS,
                                 &summary_out->dns_server_count, server, 0,
                                 &summary_out->dns_server_table_approximate);
            }
        }

        if (progress_cb && ((i + 1U) % PCAP_SUMMARY_PROGRESS_INTERVAL) == 0) {
            progress_cb(i + 1U, packet_count, progress_ctx);
        }
    }

    sort_summary_tables(summary_out);
    if (progress_cb) {
        progress_cb(packet_count, packet_count, progress_ctx);
    }
    return PCAP_READER_OK;
}
