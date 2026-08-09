#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_flow.h"
#include "pcap_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_INVESTIGATION_MAX_FINDINGS 96U
#define PCAP_INVESTIGATION_MAX_TIMELINE 128U
#define PCAP_INVESTIGATION_MAX_DIFF_ITEMS 32U
#define PCAP_INVESTIGATION_PATH_MAX 384U
#define PCAP_INVESTIGATION_BASELINE_SCHEMA 1U

typedef enum {
    PCAP_INV_FINDING_ENGINE_ALERT = 0,
    PCAP_INV_FINDING_ADMIN_SERVICE,
    PCAP_INV_FINDING_EXTERNAL_ADMIN_ACCESS,
    PCAP_INV_FINDING_IOT_CLEARTEXT,
    PCAP_INV_FINDING_MULTIPLE_DHCP_SERVERS,
    PCAP_INV_FINDING_NAME_POISONING_SURFACE,
    PCAP_INV_FINDING_SUSPICIOUS_DNS_NAME,
    PCAP_INV_FINDING_ENCRYPTED_DNS,
    PCAP_INV_FINDING_SMB1,
    PCAP_INV_FINDING_MQTT_CLEARTEXT,
    PCAP_INV_FINDING_WIFI_DEAUTH_BURST,
    PCAP_INV_FINDING_IOC_IP,
    PCAP_INV_FINDING_IOC_DOMAIN,
    PCAP_INV_FINDING_FORBIDDEN_PORT,
    PCAP_INV_FINDING_CREDENTIAL_EXPOSURE,
} pcap_investigation_finding_type_t;

typedef enum {
    PCAP_INV_EVENT_DEVICE_FIRST_SEEN = 0,
    PCAP_INV_EVENT_FLOW_FIRST_SEEN,
    PCAP_INV_EVENT_FINDING,
} pcap_investigation_event_type_t;

typedef struct {
    uint8_t type;
    uint8_t severity;
    uint8_t confidence;
    uint8_t reserved;
    uint16_t flow_id;
    uint16_t service_port;
    uint32_t first_packet;
    uint32_t last_packet;
    uint64_t first_time_us;
    uint64_t last_time_us;
    char source[64];
    char target[64];
    char detail[192];
} pcap_investigation_finding_t;

typedef struct {
    uint8_t type;
    uint8_t severity;
    uint16_t finding_index;
    uint16_t flow_id;
    uint16_t reserved;
    uint32_t packet_number;
    uint64_t time_us;
    char actor[64];
    char detail[128];
} pcap_investigation_event_t;

typedef struct {
    uint32_t finding_count;
    uint32_t timeline_count;
    uint32_t watch_count;
    uint32_t suspicious_count;
    uint32_t critical_count;
    uint32_t observed_admin_services;
    uint32_t cleartext_services;
    uint32_t suspicious_dns_names;
    uint32_t encrypted_dns_flows;
    uint32_t ioc_matches;
    uint32_t ioc_ip_entries;
    uint32_t ioc_domain_entries;
    uint32_t rule_port_entries;
    bool finding_limited;
    bool timeline_limited;
    bool intel_loaded;
    pcap_investigation_finding_t findings[PCAP_INVESTIGATION_MAX_FINDINGS];
    pcap_investigation_event_t timeline[PCAP_INVESTIGATION_MAX_TIMELINE];
} pcap_investigation_t;

typedef struct {
    uint16_t representative_index;
    uint8_t risk_score;
    uint8_t finding_count;
    uint32_t alias_count;
    uint32_t flow_count;
    uint32_t remote_peer_count;
    uint32_t service_count;
    uint32_t domain_count;
    uint64_t first_time_us;
    uint64_t last_time_us;
    uint64_t sent_bytes;
    uint64_t received_bytes;
    char role[24];
    char vendor[48];
    char hostname[96];
    char mac[18];
    char addresses[224];
    char top_peers[256];
    char top_domains[256];
    char services[256];
} pcap_device_dossier_t;

typedef struct {
    bool baseline_available;
    uint32_t new_local_devices;
    uint32_t missing_local_devices;
    uint32_t new_remote_endpoints;
    uint32_t new_domains;
    uint32_t new_services;
    uint32_t item_count;
    bool item_limited;
    char baseline_source[96];
    char items[PCAP_INVESTIGATION_MAX_DIFF_ITEMS][128];
} pcap_investigation_diff_t;

typedef enum {
    PCAP_INVESTIGATION_OK = 0,
    PCAP_INVESTIGATION_INVALID_ARG,
    PCAP_INVESTIGATION_NOT_FOUND,
    PCAP_INVESTIGATION_IO_ERROR,
    PCAP_INVESTIGATION_INVALID_FORMAT,
    PCAP_INVESTIGATION_LIMIT,
} pcap_investigation_status_t;

void pcap_investigation_build(const pcap_summary_t *summary,
                              const pcap_flow_analysis_t *flow_analysis,
                              pcap_investigation_t *investigation);

bool pcap_investigation_device_dossier(
    const pcap_flow_analysis_t *flow_analysis,
    const pcap_investigation_t *investigation,
    uint16_t representative_index,
    pcap_device_dossier_t *dossier_out);

pcap_investigation_status_t pcap_investigation_baseline_save(
    const char *source_path,
    const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis,
    char *output_path,
    size_t output_path_size);

pcap_investigation_status_t pcap_investigation_baseline_compare(
    const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis,
    pcap_investigation_diff_t *diff_out);

pcap_investigation_status_t pcap_investigation_export_html(
    const char *source_path,
    const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis,
    const pcap_investigation_t *investigation,
    const pcap_investigation_diff_t *diff,
    char *output_path,
    size_t output_path_size);

const char *pcap_investigation_finding_name(pcap_investigation_finding_type_t type);
const char *pcap_investigation_event_name(pcap_investigation_event_type_t type);
const char *pcap_investigation_status_name(pcap_investigation_status_t status);

#ifdef __cplusplus
}
#endif
