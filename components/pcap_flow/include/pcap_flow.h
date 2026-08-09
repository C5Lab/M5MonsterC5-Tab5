#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_FLOW_MAX_FLOWS          512U
#define PCAP_FLOW_MAX_PACKET_MAP    4096U
#define PCAP_FLOW_ID_NONE          UINT16_MAX
#define PCAP_FLOW_STREAM_SEGMENTS   256U
#define PCAP_FLOW_MAX_DEVICES       128U
#define PCAP_FLOW_DEVICE_SERVICES    12U
#define PCAP_FLOW_MAX_ALERTS          64U
#define PCAP_FLOW_ALERT_EVIDENCE       6U

typedef enum {
    PCAP_APP_UNKNOWN = 0,
    PCAP_APP_TCP,
    PCAP_APP_UDP,
    PCAP_APP_ARP,
    PCAP_APP_ICMP,
    PCAP_APP_ICMPV6,
    PCAP_APP_EAPOL,
    PCAP_APP_WIFI_MGMT,
    PCAP_APP_DNS,
    PCAP_APP_MDNS,
    PCAP_APP_DHCP,
    PCAP_APP_HTTP,
    PCAP_APP_TLS,
    PCAP_APP_QUIC,
    PCAP_APP_SSDP,
    PCAP_APP_NTP,
    PCAP_APP_SSH,
    PCAP_APP_FTP,
    PCAP_APP_TELNET,
    PCAP_APP_SMB,
    PCAP_APP_MQTT,
    PCAP_APP_BITTORRENT,
    PCAP_APP_BITTORRENT_DHT,
    PCAP_APP_LLMNR,
    PCAP_APP_NBNS,
    PCAP_APP_SMTP,
    PCAP_APP_IMAP,
    PCAP_APP_POP3,
    PCAP_APP_RDP,
    PCAP_APP_VNC,
    PCAP_APP_RTSP,
    PCAP_APP_COAP,
    PCAP_APP_REDIS,
    PCAP_APP_DATABASE,
    PCAP_APP_COUNT,
} pcap_application_t;

typedef enum {
    PCAP_APP_CONFIDENCE_NONE = 0,
    PCAP_APP_CONFIDENCE_TRANSPORT,
    PCAP_APP_CONFIDENCE_LIKELY,
    PCAP_APP_CONFIDENCE_CONFIRMED,
} pcap_app_confidence_t;

typedef struct {
    char originator[64];
    char responder[64];
    char originator_mac[18];
    char responder_mac[18];
    uint16_t originator_port;
    uint16_t responder_port;
    uint8_t ip_protocol;
    uint8_t app_protocol;
    uint8_t app_confidence;
    uint8_t originator_tcp_flags;
    uint8_t responder_tcp_flags;
    uint32_t first_packet;
    uint32_t last_packet;
    uint64_t first_time_us;
    uint64_t last_time_us;
    uint64_t originator_bytes;
    uint64_t responder_bytes;
    uint64_t originator_payload_bytes;
    uint64_t responder_payload_bytes;
    uint32_t originator_packets;
    uint32_t responder_packets;
    uint32_t originator_next_sequence;
    uint32_t responder_next_sequence;
    uint32_t originator_retransmissions;
    uint32_t responder_retransmissions;
    uint32_t zero_window_packets;
    uint64_t syn_time_us;
    uint64_t handshake_rtt_us;
    bool originator_sequence_seen;
    bool responder_sequence_seen;
    uint16_t tls_version;
    uint16_t tls_cipher_count;
    uint16_t tls_extension_count;
    char tls_fingerprint[17];
    bool credential_indicator;
    char server_name[96];
    char application_detail[128];
} pcap_flow_entry_t;

typedef struct {
    uint16_t port;
    uint8_t ip_protocol;
    uint8_t application;
    uint32_t packets;
    uint64_t bytes;
} pcap_device_service_t;

typedef struct {
    char address[64];
    char mac[18];
    char hostname[96];
    uint64_t first_time_us;
    uint64_t last_time_us;
    uint64_t sent_bytes;
    uint64_t received_bytes;
    uint32_t sent_packets;
    uint32_t received_packets;
    uint16_t service_count;
    bool internal;
    bool service_limited;
    pcap_device_service_t services[PCAP_FLOW_DEVICE_SERVICES];
} pcap_device_entry_t;

typedef enum {
    PCAP_HEALTH_INSUFFICIENT = 0,
    PCAP_HEALTH_HEALTHY,
    PCAP_HEALTH_WATCH,
    PCAP_HEALTH_SUSPICIOUS,
    PCAP_HEALTH_CRITICAL,
} pcap_health_level_t;

typedef enum {
    PCAP_ALERT_PORT_SCAN = 0,
    PCAP_ALERT_HOST_SWEEP,
    PCAP_ALERT_ARP_CONFLICT,
    PCAP_ALERT_DNS_ANOMALY,
    PCAP_ALERT_BEACONING,
    PCAP_ALERT_EXFIL_CANDIDATE,
    PCAP_ALERT_CLEARTEXT_SERVICE,
    PCAP_ALERT_WORM_LIKE_SPREAD,
    PCAP_ALERT_EXCESSIVE_BROADCAST,
    PCAP_ALERT_WEAK_TLS,
    PCAP_ALERT_TCP_QUALITY,
} pcap_alert_type_t;

typedef struct {
    uint8_t type;
    uint8_t severity;
    uint8_t confidence;
    uint8_t evidence_count;
    uint16_t flow_id;
    uint16_t service_port;
    uint32_t first_packet;
    uint32_t last_packet;
    uint64_t first_time_us;
    uint64_t last_time_us;
    char source[64];
    char target[64];
    char detail[160];
    uint32_t evidence_packets[PCAP_FLOW_ALERT_EVIDENCE];
} pcap_security_alert_t;

typedef struct {
    uint64_t packets;
    uint64_t bytes;
    uint32_t flows;
    uint32_t confirmed_packets;
    uint32_t likely_packets;
    uint32_t confirmed_flows;
    uint32_t likely_flows;
} pcap_app_summary_t;

typedef struct {
    uint32_t analyzed_packets;
    uint32_t flow_count;
    uint32_t overflow_packets;
    uint32_t device_count;
    uint32_t alert_count;
    uint32_t dns_packets;
    uint32_t dns_error_packets;
    uint32_t dns_suspicious_names;
    uint32_t broadcast_packets;
    bool flow_limited;
    bool device_limited;
    bool alert_limited;
    uint8_t health_level;
    uint16_t packet_flow_id[PCAP_FLOW_MAX_PACKET_MAP];
    uint8_t packet_direction[PCAP_FLOW_MAX_PACKET_MAP];
    uint8_t packet_application[PCAP_FLOW_MAX_PACKET_MAP];
    uint8_t packet_confidence[PCAP_FLOW_MAX_PACKET_MAP];
    pcap_flow_entry_t flows[PCAP_FLOW_MAX_FLOWS];
    pcap_app_summary_t applications[PCAP_APP_COUNT];
    pcap_device_entry_t devices[PCAP_FLOW_MAX_DEVICES];
    pcap_security_alert_t alerts[PCAP_FLOW_MAX_ALERTS];
} pcap_flow_analysis_t;

typedef struct {
    bool enabled;
    bool has_any_address;
    bool has_source_address;
    bool has_destination_address;
    bool has_any_mac;
    bool has_port;
    bool has_flow;
    bool has_application;
    bool has_time_window;
    char any_address[64];
    char source_address[64];
    char destination_address[64];
    char any_mac[18];
    uint16_t port;
    uint16_t flow_id;
    uint8_t application;
    uint64_t time_start_us;
    uint64_t time_end_us;
} pcap_flow_filter_t;

typedef struct {
    uint32_t matching_packets;
    uint32_t payload_packets;
    uint32_t segments_used;
    uint32_t retransmissions;
    uint64_t payload_bytes;
    uint64_t gap_bytes;
    bool segment_limited;
    bool output_truncated;
    bool stream_incomplete;
} pcap_flow_stream_result_t;

typedef enum {
    PCAP_FLOW_STREAM_ASCII = 0,
    PCAP_FLOW_STREAM_HEX,
} pcap_flow_stream_mode_t;

pcap_reader_status_t pcap_flow_analysis_build(
    pcap_reader_t *reader,
    const pcap_capture_info_t *capture_info,
    const pcap_packet_index_t *packet_index,
    size_t packet_count,
    pcap_flow_analysis_t *analysis,
    const volatile bool *cancel_requested);

const char *pcap_flow_application_name(pcap_application_t application);
const char *pcap_flow_confidence_name(pcap_app_confidence_t confidence);
const char *pcap_flow_transport_name(uint8_t ip_protocol);
const char *pcap_flow_health_name(pcap_health_level_t level);
bool pcap_flow_address_is_internal(const char *address);
const char *pcap_flow_alert_name(pcap_alert_type_t type);

bool pcap_flow_same_local_device(const pcap_device_entry_t *left,
                                 const pcap_device_entry_t *right);
uint32_t pcap_flow_local_device_count(const pcap_flow_analysis_t *analysis);
uint32_t pcap_flow_remote_endpoint_count(const pcap_flow_analysis_t *analysis);

bool pcap_flow_filter_matches(
    const pcap_flow_filter_t *filter,
    const pcap_flow_analysis_t *analysis,
    uint32_t packet_number,
    const pcap_packet_index_t *packet,
    const pcap_packet_details_t *details,
    pcap_timestamp_resolution_t resolution);

void pcap_flow_filter_clear(pcap_flow_filter_t *filter);
void pcap_flow_filter_describe(const pcap_flow_filter_t *filter,
                               char *output, size_t output_size);

pcap_reader_status_t pcap_flow_build_stream(
    pcap_reader_t *reader,
    const pcap_packet_index_t *packet_index,
    size_t packet_count,
    const pcap_flow_analysis_t *analysis,
    uint16_t flow_id,
    char *output,
    size_t output_size,
    pcap_flow_stream_result_t *result);

pcap_reader_status_t pcap_flow_build_stream_ex(
    pcap_reader_t *reader,
    const pcap_packet_index_t *packet_index,
    size_t packet_count,
    const pcap_flow_analysis_t *analysis,
    uint16_t flow_id,
    pcap_flow_stream_mode_t mode,
    char *output,
    size_t output_size,
    pcap_flow_stream_result_t *result);

#ifdef __cplusplus
}
#endif
