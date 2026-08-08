#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_SUMMARY_MAX_PROTOCOLS    16U
#define PCAP_SUMMARY_MAX_ENDPOINTS    32U
#define PCAP_SUMMARY_MAX_PORTS        24U
#define PCAP_SUMMARY_MAX_DNS_DOMAINS  64U
#define PCAP_SUMMARY_MAX_DNS_ANSWERS  24U
#define PCAP_SUMMARY_MAX_DNS_PEERS    16U
#define PCAP_SUMMARY_MAX_DNS_TYPES    16U

typedef struct {
    char label[96];
    uint64_t count;
    uint64_t bytes;
} pcap_summary_text_counter_t;

typedef struct {
    uint16_t port;
    uint8_t ip_protocol;
    uint64_t count;
} pcap_summary_port_counter_t;

typedef struct {
    uint64_t analyzed_packets;
    uint64_t captured_bytes;
    uint64_t original_bytes;
    uint64_t decode_limited_packets;
    uint64_t malformed_packets;
    uint64_t truncated_packets;

    uint64_t tcp_packets;
    uint64_t udp_packets;
    uint64_t arp_packets;
    uint64_t http_packets;
    uint64_t tls_packets;
    uint64_t eapol_packets;
    uint64_t wifi_management_packets;
    uint64_t wifi_beacons;
    uint64_t wifi_probe_requests;
    uint64_t wifi_probe_responses;
    uint64_t wifi_deauthentications;

    uint64_t dns_packets;
    uint64_t dns_queries;
    uint64_t dns_responses;
    uint64_t dns_noerror;
    uint64_t dns_nxdomain;
    uint64_t dns_servfail;
    uint64_t dns_other_rcode;
    uint64_t dns_long_names;
    uint64_t dns_many_label_names;
    uint64_t dns_unique_domains_observed;

    uint16_t protocol_count;
    uint16_t endpoint_count;
    uint16_t port_count;
    uint16_t dns_domain_count;
    uint16_t dns_answer_count;
    uint16_t dns_client_count;
    uint16_t dns_server_count;
    uint16_t dns_type_count;

    bool protocol_table_approximate;
    bool endpoint_table_approximate;
    bool port_table_approximate;
    bool dns_domain_table_approximate;
    bool dns_answer_table_approximate;
    bool dns_client_table_approximate;
    bool dns_server_table_approximate;
    bool dns_type_table_approximate;

    pcap_summary_text_counter_t protocols[PCAP_SUMMARY_MAX_PROTOCOLS];
    pcap_summary_text_counter_t endpoints[PCAP_SUMMARY_MAX_ENDPOINTS];
    pcap_summary_port_counter_t ports[PCAP_SUMMARY_MAX_PORTS];
    pcap_summary_text_counter_t dns_domains[PCAP_SUMMARY_MAX_DNS_DOMAINS];
    pcap_summary_text_counter_t dns_answers[PCAP_SUMMARY_MAX_DNS_ANSWERS];
    pcap_summary_text_counter_t dns_clients[PCAP_SUMMARY_MAX_DNS_PEERS];
    pcap_summary_text_counter_t dns_servers[PCAP_SUMMARY_MAX_DNS_PEERS];
    pcap_summary_text_counter_t dns_types[PCAP_SUMMARY_MAX_DNS_TYPES];
} pcap_summary_t;

typedef void (*pcap_summary_progress_cb_t)(size_t processed_packets,
                                           size_t total_packets,
                                           void *user_ctx);

pcap_reader_status_t pcap_summary_build(pcap_reader_t *reader,
                                        const pcap_packet_index_t *packets,
                                        size_t packet_count,
                                        pcap_summary_t *summary_out,
                                        uint32_t *packet_flags_out,
                                        size_t packet_flags_capacity,
                                        const volatile bool *cancel_requested,
                                        pcap_summary_progress_cb_t progress_cb,
                                        void *progress_ctx);

#ifdef __cplusplus
}
#endif
