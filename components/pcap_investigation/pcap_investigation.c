#include "pcap_investigation.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define PCAP_INV_ROOT "/sdcard/lab/espshark"
#define PCAP_INV_INTEL_ROOT PCAP_INV_ROOT "/intel"
#define PCAP_INV_BASELINE_ROOT PCAP_INV_ROOT "/baseline"
#define PCAP_INV_EXPORT_ROOT PCAP_INV_ROOT "/exports"
#define PCAP_INV_BASELINE_PATH PCAP_INV_BASELINE_ROOT "/home.espbaseline"
#define PCAP_INV_BASELINE_MAGIC "ESPBAS1"
#define PCAP_INV_BASELINE_MAGIC_SIZE 8U

typedef struct __attribute__((packed)) {
    char magic[PCAP_INV_BASELINE_MAGIC_SIZE];
    uint16_t schema_version;
    uint16_t header_size;
    uint32_t local_count;
    uint32_t remote_count;
    uint32_t domain_count;
    char source[96];
} pcap_inv_baseline_header_t;

typedef struct __attribute__((packed)) {
    char key[64];
    char hostname[96];
    char role[24];
    uint32_t service_hash;
} pcap_inv_baseline_local_t;

typedef struct __attribute__((packed)) {
    char value[96];
} pcap_inv_baseline_text_t;

static void inv_copy(char *destination, size_t destination_size, const char *source)
{
    if (!destination || destination_size == 0U) return;
    size_t length = 0U;
    if (source) {
        while (length + 1U < destination_size && source[length] != '\0') {
            destination[length] = source[length];
            length++;
        }
    }
    destination[length] = '\0';
}

static bool inv_mkdir(const char *path)
{
    if (mkdir(path, 0775) == 0) return true;
    struct stat item;
    return stat(path, &item) == 0 && S_ISDIR(item.st_mode);
}

static bool inv_prepare_dirs(void)
{
    return inv_mkdir(PCAP_INV_ROOT) && inv_mkdir(PCAP_INV_INTEL_ROOT) &&
           inv_mkdir(PCAP_INV_BASELINE_ROOT) && inv_mkdir(PCAP_INV_EXPORT_ROOT);
}

static const char *inv_basename(const char *path)
{
    const char *base = path ? strrchr(path, '/') : NULL;
    return base ? base + 1 : (path ? path : "capture");
}

static void inv_safe_stem(const char *path, char *output, size_t output_size)
{
    if (!output || output_size == 0U) return;
    const char *base = inv_basename(path);
    size_t position = 0U;
    while (*base && *base != '.' && position + 1U < output_size) {
        unsigned char character = (unsigned char)*base++;
        output[position++] = (isalnum(character) || character == '-' || character == '_')
                                 ? (char)character : '_';
    }
    if (position == 0U) inv_copy(output, output_size, "capture");
    else output[position] = '\0';
}

static bool inv_text_contains(const char *text, const char *needle)
{
    if (!text || !needle || !needle[0]) return false;
    size_t needle_length = strlen(needle);
    for (const char *position = text; *position; position++) {
        if (strncasecmp(position, needle, needle_length) == 0) return true;
    }
    return false;
}

static bool inv_domain_matches(const char *observed, const char *indicator)
{
    if (!observed || !indicator || !observed[0] || !indicator[0]) return false;
    if (strcasecmp(observed, indicator) == 0) return true;
    size_t observed_length = strlen(observed);
    size_t indicator_length = strlen(indicator);
    return observed_length > indicator_length &&
           observed[observed_length - indicator_length - 1U] == '.' &&
           strcasecmp(observed + observed_length - indicator_length, indicator) == 0;
}

static bool inv_is_admin_port(uint16_t port)
{
    static const uint16_t ports[] = {
        22U, 23U, 80U, 443U, 445U, 3389U, 5900U, 5985U, 5986U, 8080U, 8443U
    };
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
        if (port == ports[i]) return true;
    }
    return false;
}

static bool inv_is_cleartext_app(uint8_t app, uint16_t port)
{
    return app == PCAP_APP_HTTP || app == PCAP_APP_FTP || app == PCAP_APP_TELNET ||
           (app == PCAP_APP_SMTP && port != 465U) ||
           (app == PCAP_APP_IMAP && port != 993U) ||
           (app == PCAP_APP_POP3 && port != 995U) ||
           app == PCAP_APP_RTSP || app == PCAP_APP_REDIS || app == PCAP_APP_DATABASE ||
           (app == PCAP_APP_MQTT && port == 1883U) ||
           (app == PCAP_APP_COAP && port == 5683U);
}

static bool inv_domain_suspicious(const char *domain)
{
    if (!domain) return false;
    size_t length = strlen(domain);
    if (length >= 50U) return true;
    unsigned labels = 1U;
    unsigned digits = 0U;
    unsigned hyphens = 0U;
    unsigned longest_label = 0U;
    unsigned current_label = 0U;
    unsigned transitions = 0U;
    bool previous_digit = false;
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)domain[i];
        if (character == '.') {
            labels++;
            if (current_label > longest_label) longest_label = current_label;
            current_label = 0U;
            continue;
        }
        current_label++;
        if (isdigit(character)) digits++;
        if (character == '-') hyphens++;
        bool digit = isdigit(character) != 0;
        if (i > 0U && digit != previous_digit) transitions++;
        previous_digit = digit;
    }
    if (current_label > longest_label) longest_label = current_label;
    return longest_label >= 32U || labels >= 6U ||
           (length >= 24U && digits * 100U / (unsigned)length >= 35U) ||
           (length >= 28U && transitions >= 12U) || hyphens >= 6U;
}

static void inv_infer_role(const pcap_flow_analysis_t *analysis,
                           const pcap_device_entry_t *device,
                           char *output, size_t output_size)
{
    if (!analysis || !device || !output || output_size == 0U) return;
    const char *name = device->hostname;
    bool router = inv_text_contains(name, "router") || inv_text_contains(name, "gateway");
    bool printer = inv_text_contains(name, "printer") || inv_text_contains(name, "epson") ||
                   inv_text_contains(name, "brother") || inv_text_contains(name, "canon");
    bool camera = inv_text_contains(name, "camera") || inv_text_contains(name, "cam-") ||
                  inv_text_contains(name, "hikvision") || inv_text_contains(name, "dahua");
    bool mobile = inv_text_contains(name, "iphone") || inv_text_contains(name, "android") ||
                  inv_text_contains(name, "pixel") || inv_text_contains(name, "phone");
    bool nas = inv_text_contains(name, "nas") || inv_text_contains(name, "synology") ||
               inv_text_contains(name, "qnap");
    bool iot = inv_text_contains(name, "esp") || inv_text_contains(name, "shelly") ||
               inv_text_contains(name, "tuya") || inv_text_contains(name, "iot");
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        const pcap_device_entry_t *alias = &analysis->devices[i];
        if (!pcap_flow_same_local_device(device, alias)) continue;
        for (uint16_t service = 0; service < alias->service_count; service++) {
            uint16_t port = alias->services[service].port;
            uint8_t app = alias->services[service].application;
            if (port == 67U || port == 53U) router = true;
            if (port == 631U || port == 9100U) printer = true;
            if (port == 554U || app == PCAP_APP_RTSP) camera = true;
            if (port == 2049U || app == PCAP_APP_SMB) nas = true;
            if (app == PCAP_APP_MQTT || app == PCAP_APP_SSDP) iot = true;
        }
    }
    const char *role = router ? "ROUTER/INFRA" : printer ? "PRINTER" :
                       camera ? "CAMERA" : nas ? "NAS/SERVER" : mobile ? "MOBILE" :
                       iot ? "IOT" : "WORKSTATION/UNKNOWN";
    inv_copy(output, output_size, role);
}

static pcap_investigation_finding_t *inv_add_finding(
    pcap_investigation_t *investigation,
    pcap_investigation_finding_type_t type,
    pcap_health_level_t severity,
    pcap_app_confidence_t confidence,
    const char *source, const char *target,
    uint16_t port, uint16_t flow_id,
    uint32_t first_packet, uint32_t last_packet,
    uint64_t first_time_us, uint64_t last_time_us,
    const char *detail)
{
    if (!investigation) return NULL;
    if (investigation->finding_count >= PCAP_INVESTIGATION_MAX_FINDINGS) {
        investigation->finding_limited = true;
        return NULL;
    }
    pcap_investigation_finding_t *finding =
        &investigation->findings[investigation->finding_count++];
    memset(finding, 0, sizeof(*finding));
    finding->type = (uint8_t)type;
    finding->severity = (uint8_t)severity;
    finding->confidence = (uint8_t)confidence;
    finding->flow_id = flow_id;
    finding->service_port = port;
    finding->first_packet = first_packet;
    finding->last_packet = last_packet;
    finding->first_time_us = first_time_us;
    finding->last_time_us = last_time_us;
    inv_copy(finding->source, sizeof(finding->source), source);
    inv_copy(finding->target, sizeof(finding->target), target);
    inv_copy(finding->detail, sizeof(finding->detail), detail);
    if (severity >= PCAP_HEALTH_CRITICAL) investigation->critical_count++;
    else if (severity >= PCAP_HEALTH_SUSPICIOUS) investigation->suspicious_count++;
    else if (severity >= PCAP_HEALTH_WATCH) investigation->watch_count++;
    return finding;
}

static void inv_add_timeline(pcap_investigation_t *investigation,
                             pcap_investigation_event_type_t type,
                             pcap_health_level_t severity,
                             uint16_t finding_index, uint16_t flow_id,
                             uint32_t packet_number, uint64_t time_us,
                             const char *actor, const char *detail)
{
    if (!investigation) return;
    if (investigation->timeline_count >= PCAP_INVESTIGATION_MAX_TIMELINE) {
        investigation->timeline_limited = true;
        return;
    }
    pcap_investigation_event_t *event =
        &investigation->timeline[investigation->timeline_count++];
    memset(event, 0, sizeof(*event));
    event->type = (uint8_t)type;
    event->severity = (uint8_t)severity;
    event->finding_index = finding_index;
    event->flow_id = flow_id;
    event->packet_number = packet_number;
    event->time_us = time_us;
    inv_copy(event->actor, sizeof(event->actor), actor);
    inv_copy(event->detail, sizeof(event->detail), detail);
}

static int inv_event_compare(const void *left, const void *right)
{
    const pcap_investigation_event_t *a = (const pcap_investigation_event_t *)left;
    const pcap_investigation_event_t *b = (const pcap_investigation_event_t *)right;
    if (a->time_us < b->time_us) return -1;
    if (a->time_us > b->time_us) return 1;
    return (int)a->type - (int)b->type;
}

static void inv_copy_engine_alerts(const pcap_flow_analysis_t *analysis,
                                   pcap_investigation_t *investigation)
{
    for (uint32_t i = 0; i < analysis->alert_count; i++) {
        const pcap_security_alert_t *alert = &analysis->alerts[i];
        inv_add_finding(investigation, PCAP_INV_FINDING_ENGINE_ALERT,
                        (pcap_health_level_t)alert->severity,
                        (pcap_app_confidence_t)alert->confidence,
                        alert->source, alert->target, alert->service_port,
                        alert->flow_id, alert->first_packet, alert->last_packet,
                        alert->first_time_us, alert->last_time_us, alert->detail);
    }
}

static bool inv_is_representative(const pcap_flow_analysis_t *analysis, uint16_t index)
{
    if (!analysis || index >= analysis->device_count || !analysis->devices[index].internal) {
        return false;
    }
    for (uint16_t previous = 0; previous < index; previous++) {
        if (pcap_flow_same_local_device(&analysis->devices[index],
                                        &analysis->devices[previous])) return false;
    }
    return true;
}

static bool inv_group_has_address(const pcap_flow_analysis_t *analysis,
                                  const pcap_device_entry_t *representative,
                                  const char *address)
{
    if (!analysis || !representative || !address || !address[0]) return false;
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        const pcap_device_entry_t *alias = &analysis->devices[i];
        if (pcap_flow_same_local_device(representative, alias) &&
            strcasecmp(alias->address, address) == 0) return true;
    }
    return false;
}

static void inv_build_posture(const pcap_flow_analysis_t *analysis,
                              pcap_investigation_t *investigation)
{
    for (uint16_t device_index = 0; device_index < analysis->device_count; device_index++) {
        if (!inv_is_representative(analysis, device_index)) continue;
        const pcap_device_entry_t *device = &analysis->devices[device_index];
        char role[24] = {0};
        inv_infer_role(analysis, device, role, sizeof(role));
        bool admin_reported = false;
        bool iot_cleartext_reported = false;
        for (uint32_t alias_index = 0; alias_index < analysis->device_count; alias_index++) {
            const pcap_device_entry_t *alias = &analysis->devices[alias_index];
            if (!pcap_flow_same_local_device(device, alias)) continue;
            for (uint16_t service_index = 0; service_index < alias->service_count;
                 service_index++) {
                const pcap_device_service_t *service = &alias->services[service_index];
                if (inv_is_admin_port(service->port) && !admin_reported) {
                    char detail[192];
                    snprintf(detail, sizeof(detail),
                             "Observed admin-capable %.7s/%u on %.47s (%.23s); passive evidence, not proof of exposure",
                             pcap_flow_transport_name(service->ip_protocol), service->port,
                             alias->address, role);
                    inv_add_finding(investigation, PCAP_INV_FINDING_ADMIN_SERVICE,
                                    service->port == 23U ? PCAP_HEALTH_SUSPICIOUS
                                                        : PCAP_HEALTH_WATCH,
                                    PCAP_APP_CONFIDENCE_CONFIRMED,
                                    alias->address, "observed service", service->port,
                                    PCAP_FLOW_ID_NONE, 0U, 0U,
                                    alias->first_time_us, alias->last_time_us, detail);
                    investigation->observed_admin_services++;
                    admin_reported = true;
                }
                if ((strcasecmp(role, "IOT") == 0 || strcasecmp(role, "CAMERA") == 0) &&
                    inv_is_cleartext_app(service->application, service->port) &&
                    !iot_cleartext_reported) {
                    char detail[192];
                    snprintf(detail, sizeof(detail),
                             "%.23s device used observed cleartext %.23s/%u; inspect filtered packets for credentials or commands",
                             role, pcap_flow_application_name(
                                       (pcap_application_t)service->application),
                             service->port);
                    inv_add_finding(investigation, PCAP_INV_FINDING_IOT_CLEARTEXT,
                                    PCAP_HEALTH_SUSPICIOUS,
                                    PCAP_APP_CONFIDENCE_LIKELY,
                                    alias->address, "cleartext service", service->port,
                                    PCAP_FLOW_ID_NONE, 0U, 0U,
                                    alias->first_time_us, alias->last_time_us, detail);
                    investigation->cleartext_services++;
                    iot_cleartext_reported = true;
                }
            }
        }
    }

    char dhcp_servers[4][64] = {{0}};
    uint32_t dhcp_server_count = 0U;
    for (uint16_t flow_id = 0; flow_id < analysis->flow_count; flow_id++) {
        const pcap_flow_entry_t *flow = &analysis->flows[flow_id];
        bool originator_internal = pcap_flow_address_is_internal(flow->originator);
        bool responder_internal = pcap_flow_address_is_internal(flow->responder);
        if (!originator_internal && responder_internal &&
            inv_is_admin_port(flow->responder_port)) {
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "External %.47s contacted observed local management service %.47s:%u",
                     flow->originator, flow->responder, flow->responder_port);
            inv_add_finding(investigation, PCAP_INV_FINDING_EXTERNAL_ADMIN_ACCESS,
                            PCAP_HEALTH_SUSPICIOUS, PCAP_APP_CONFIDENCE_LIKELY,
                            flow->originator, flow->responder, flow->responder_port,
                            flow_id, flow->first_packet, flow->last_packet,
                            flow->first_time_us, flow->last_time_us, detail);
        }
        if (flow->originator_port == 67U && dhcp_server_count < 4U) {
            bool known = false;
            for (uint32_t i = 0; i < dhcp_server_count; i++) {
                if (strcasecmp(dhcp_servers[i], flow->originator) == 0) known = true;
            }
            if (!known) inv_copy(dhcp_servers[dhcp_server_count++],
                                 sizeof(dhcp_servers[0]), flow->originator);
        }
        if (flow->app_protocol == PCAP_APP_SMB &&
            inv_text_contains(flow->application_detail, "SMB1")) {
            inv_add_finding(investigation, PCAP_INV_FINDING_SMB1,
                            PCAP_HEALTH_CRITICAL, PCAP_APP_CONFIDENCE_CONFIRMED,
                            flow->originator, flow->responder, flow->responder_port,
                            flow_id, flow->first_packet, flow->last_packet,
                            flow->first_time_us, flow->last_time_us,
                            "SMB1 signature observed; legacy SMB is vulnerable to downgrade and historical worm propagation");
        }
        if (flow->app_protocol == PCAP_APP_MQTT && flow->responder_port == 1883U) {
            inv_add_finding(investigation, PCAP_INV_FINDING_MQTT_CLEARTEXT,
                            PCAP_HEALTH_WATCH, PCAP_APP_CONFIDENCE_CONFIRMED,
                            flow->originator, flow->responder, flow->responder_port,
                            flow_id, flow->first_packet, flow->last_packet,
                            flow->first_time_us, flow->last_time_us,
                            "MQTT over TCP/1883 observed without transport encryption");
            investigation->cleartext_services++;
        }
        if (flow->credential_indicator) {
            inv_add_finding(investigation, PCAP_INV_FINDING_CREDENTIAL_EXPOSURE,
                            PCAP_HEALTH_CRITICAL, PCAP_APP_CONFIDENCE_CONFIRMED,
                            flow->originator, flow->responder, flow->responder_port,
                            flow_id, flow->first_packet, flow->last_packet,
                            flow->first_time_us, flow->last_time_us,
                            "Cleartext authentication marker observed; secret values were intentionally not retained");
        }
        if (flow->responder_port == 853U || flow->originator_port == 853U ||
            inv_domain_matches(flow->server_name, "dns.google") ||
            inv_domain_matches(flow->server_name, "cloudflare-dns.com") ||
            inv_domain_matches(flow->server_name, "dns.quad9.net")) {
            inv_add_finding(investigation, PCAP_INV_FINDING_ENCRYPTED_DNS,
                            PCAP_HEALTH_WATCH, PCAP_APP_CONFIDENCE_LIKELY,
                            flow->originator, flow->server_name[0]
                                                  ? flow->server_name : flow->responder,
                            flow->responder_port, flow_id, flow->first_packet,
                            flow->last_packet, flow->first_time_us, flow->last_time_us,
                            "Encrypted DNS candidate observed; local DNS visibility may be bypassed");
            investigation->encrypted_dns_flows++;
        }
    }
    if (dhcp_server_count > 1U) {
        char detail[192];
        snprintf(detail, sizeof(detail), "Observed %lu DHCP server source(s): %.63s, %.63s",
                 (unsigned long)dhcp_server_count, dhcp_servers[0], dhcp_servers[1]);
        inv_add_finding(investigation, PCAP_INV_FINDING_MULTIPLE_DHCP_SERVERS,
                        PCAP_HEALTH_SUSPICIOUS, PCAP_APP_CONFIDENCE_LIKELY,
                        "local segment", "DHCP clients", 67U, PCAP_FLOW_ID_NONE,
                        0U, 0U, 0U, 0U, detail);
    }
    if (analysis->applications[PCAP_APP_LLMNR].packets > 0U ||
        analysis->applications[PCAP_APP_NBNS].packets > 0U) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                 "LLMNR %llu packet(s), NBNS %llu packet(s); these fallback protocols expand local name-poisoning attack surface",
                 (unsigned long long)analysis->applications[PCAP_APP_LLMNR].packets,
                 (unsigned long long)analysis->applications[PCAP_APP_NBNS].packets);
        inv_add_finding(investigation, PCAP_INV_FINDING_NAME_POISONING_SURFACE,
                        PCAP_HEALTH_WATCH, PCAP_APP_CONFIDENCE_CONFIRMED,
                        "local clients", "multicast name resolution", 0U,
                        PCAP_FLOW_ID_NONE, 0U, 0U, 0U, 0U, detail);
    }
}

static void inv_build_dns_findings(const pcap_summary_t *summary,
                                   pcap_investigation_t *investigation)
{
    if (!summary) return;
    for (uint16_t i = 0; i < summary->dns_domain_count; i++) {
        const char *domain = summary->dns_domains[i].label;
        if (!inv_domain_suspicious(domain)) continue;
        char detail[192];
        snprintf(detail, sizeof(detail),
                 "DNS name %.95s matched offline length/label/digit heuristics (%llu observation(s)); indicator only",
                 domain, (unsigned long long)summary->dns_domains[i].count);
        inv_add_finding(investigation, PCAP_INV_FINDING_SUSPICIOUS_DNS_NAME,
                        PCAP_HEALTH_WATCH, PCAP_APP_CONFIDENCE_LIKELY,
                        "DNS clients", domain, 53U, PCAP_FLOW_ID_NONE,
                        0U, 0U, 0U, 0U, detail);
        investigation->suspicious_dns_names++;
    }
    if (summary->wifi_deauthentications >= 5U) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                 "%llu 802.11 deauthentication frame(s) observed in the indexed sample",
                 (unsigned long long)summary->wifi_deauthentications);
        inv_add_finding(investigation, PCAP_INV_FINDING_WIFI_DEAUTH_BURST,
                        PCAP_HEALTH_SUSPICIOUS, PCAP_APP_CONFIDENCE_CONFIRMED,
                        "802.11 segment", "stations", 0U, PCAP_FLOW_ID_NONE,
                        0U, 0U, 0U, 0U, detail);
    }
}

static bool inv_trim_line(char *line)
{
    if (!line) return false;
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';
    char *start = line;
    while (*start && isspace((unsigned char)*start)) start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    if (start != line) memmove(line, start, (size_t)(end - start) + 1U);
    return line[0] != '\0';
}

static void inv_load_ip_iocs(const pcap_flow_analysis_t *analysis,
                             pcap_investigation_t *investigation)
{
    FILE *file = fopen(PCAP_INV_INTEL_ROOT "/ips.txt", "r");
    if (!file) return;
    investigation->intel_loaded = true;
    char line[128];
    while (fgets(line, sizeof(line), file)) {
        if (!inv_trim_line(line)) continue;
        investigation->ioc_ip_entries++;
        for (uint32_t i = 0; i < analysis->device_count; i++) {
            const pcap_device_entry_t *device = &analysis->devices[i];
            if (strcasecmp(device->address, line) != 0) continue;
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "Observed endpoint %.63s matched offline IOC entry from ips.txt",
                     device->address);
            inv_add_finding(investigation, PCAP_INV_FINDING_IOC_IP,
                            PCAP_HEALTH_CRITICAL, PCAP_APP_CONFIDENCE_CONFIRMED,
                            device->address, "offline IOC", 0U, PCAP_FLOW_ID_NONE,
                            0U, 0U, device->first_time_us, device->last_time_us, detail);
            investigation->ioc_matches++;
        }
    }
    fclose(file);
}

static void inv_load_domain_iocs(const pcap_summary_t *summary,
                                 const pcap_flow_analysis_t *analysis,
                                 pcap_investigation_t *investigation)
{
    FILE *file = fopen(PCAP_INV_INTEL_ROOT "/domains.txt", "r");
    if (!file) return;
    investigation->intel_loaded = true;
    char line[128];
    while (fgets(line, sizeof(line), file)) {
        if (!inv_trim_line(line)) continue;
        investigation->ioc_domain_entries++;
        bool matched = false;
        if (summary) {
            for (uint16_t i = 0; i < summary->dns_domain_count; i++) {
                if (inv_domain_matches(summary->dns_domains[i].label, line)) {
                    matched = true;
                    break;
                }
            }
        }
        uint16_t flow_id = PCAP_FLOW_ID_NONE;
        const char *source = "DNS clients";
        for (uint16_t i = 0; i < analysis->flow_count; i++) {
            if (inv_domain_matches(analysis->flows[i].server_name, line)) {
                matched = true;
                flow_id = i;
                source = analysis->flows[i].originator;
                break;
            }
        }
        if (matched) {
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "Observed domain/SNI %.95s matched offline IOC entry from domains.txt",
                     line);
            inv_add_finding(investigation, PCAP_INV_FINDING_IOC_DOMAIN,
                            PCAP_HEALTH_CRITICAL, PCAP_APP_CONFIDENCE_CONFIRMED,
                            source, line, 0U, flow_id, 0U, 0U, 0U, 0U, detail);
            investigation->ioc_matches++;
        }
    }
    fclose(file);
}

static void inv_load_port_rules(const pcap_flow_analysis_t *analysis,
                                pcap_investigation_t *investigation)
{
    FILE *file = fopen(PCAP_INV_INTEL_ROOT "/forbidden_ports.txt", "r");
    if (!file) return;
    investigation->intel_loaded = true;
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        if (!inv_trim_line(line)) continue;
        char *end = NULL;
        unsigned long value = strtoul(line, &end, 10);
        if (end == line || value == 0UL || value > 65535UL) continue;
        uint16_t port = (uint16_t)value;
        investigation->rule_port_entries++;
        for (uint16_t flow_id = 0; flow_id < analysis->flow_count; flow_id++) {
            const pcap_flow_entry_t *flow = &analysis->flows[flow_id];
            if (flow->originator_port != port && flow->responder_port != port) continue;
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "Observed flow matched locally forbidden port %u rule", port);
            inv_add_finding(investigation, PCAP_INV_FINDING_FORBIDDEN_PORT,
                            PCAP_HEALTH_SUSPICIOUS, PCAP_APP_CONFIDENCE_CONFIRMED,
                            flow->originator, flow->responder, port, flow_id,
                            flow->first_packet, flow->last_packet,
                            flow->first_time_us, flow->last_time_us, detail);
            investigation->ioc_matches++;
            break;
        }
    }
    fclose(file);
}

static void inv_mac_prefix(const char *text, char output[7])
{
    size_t position = 0U;
    if (text) {
        for (; *text && position < 6U; text++) {
            if (isxdigit((unsigned char)*text)) {
                output[position++] = (char)toupper((unsigned char)*text);
            }
        }
    }
    output[position] = '\0';
}

static void inv_lookup_vendor(const char *mac, char *output, size_t output_size)
{
    if (!output || output_size == 0U) return;
    output[0] = '\0';
    char wanted[7];
    inv_mac_prefix(mac, wanted);
    if (strlen(wanted) != 6U) return;
    FILE *file = fopen(PCAP_INV_INTEL_ROOT "/oui.txt", "r");
    if (!file) return;
    char line[160];
    while (fgets(line, sizeof(line), file)) {
        if (!inv_trim_line(line)) continue;
        char *separator = line;
        while (*separator && !isspace((unsigned char)*separator)) separator++;
        if (!*separator) continue;
        *separator++ = '\0';
        while (*separator && isspace((unsigned char)*separator)) separator++;
        char observed[7];
        inv_mac_prefix(line, observed);
        if (strlen(observed) == 6U && strcmp(observed, wanted) == 0) {
            inv_copy(output, output_size, separator);
            break;
        }
    }
    fclose(file);
}

static void inv_build_timeline(const pcap_flow_analysis_t *analysis,
                               pcap_investigation_t *investigation)
{
    for (uint16_t i = 0; i < analysis->device_count; i++) {
        if (!inv_is_representative(analysis, i)) continue;
        const pcap_device_entry_t *device = &analysis->devices[i];
        char detail[128];
        snprintf(detail, sizeof(detail), "Local device first observed%s%.63s",
                 device->hostname[0] ? ": " : "",
                 device->hostname[0] ? device->hostname : device->address);
        inv_add_timeline(investigation, PCAP_INV_EVENT_DEVICE_FIRST_SEEN,
                         PCAP_HEALTH_HEALTHY, UINT16_MAX, PCAP_FLOW_ID_NONE,
                         0U, device->first_time_us, device->address, detail);
    }
    uint32_t flow_events = 0U;
    for (uint16_t i = 0; i < analysis->flow_count && flow_events < 32U; i++) {
        const pcap_flow_entry_t *flow = &analysis->flows[i];
        if (flow->app_protocol == PCAP_APP_TCP || flow->app_protocol == PCAP_APP_UDP ||
            flow->app_protocol == PCAP_APP_UNKNOWN) {
            continue;
        }
        char detail[128];
        snprintf(detail, sizeof(detail), "%.23s %.39s:%u -> %.39s:%u",
                 pcap_flow_application_name((pcap_application_t)flow->app_protocol),
                 flow->originator, flow->originator_port,
                 flow->responder, flow->responder_port);
        inv_add_timeline(investigation, PCAP_INV_EVENT_FLOW_FIRST_SEEN,
                         PCAP_HEALTH_HEALTHY, UINT16_MAX, i, flow->first_packet,
                         flow->first_time_us, flow->originator, detail);
        flow_events++;
    }
    for (uint32_t i = 0; i < investigation->finding_count; i++) {
        const pcap_investigation_finding_t *finding = &investigation->findings[i];
        inv_add_timeline(investigation, PCAP_INV_EVENT_FINDING,
                         (pcap_health_level_t)finding->severity, (uint16_t)i,
                         finding->flow_id, finding->first_packet,
                         finding->first_time_us, finding->source,
                         pcap_investigation_finding_name(
                             (pcap_investigation_finding_type_t)finding->type));
    }
    if (investigation->timeline_count > 1U) {
        qsort(investigation->timeline, investigation->timeline_count,
              sizeof(investigation->timeline[0]), inv_event_compare);
    }
}

void pcap_investigation_build(const pcap_summary_t *summary,
                              const pcap_flow_analysis_t *flow_analysis,
                              pcap_investigation_t *investigation)
{
    if (!investigation) return;
    memset(investigation, 0, sizeof(*investigation));
    if (!flow_analysis) return;
    inv_prepare_dirs();
    inv_copy_engine_alerts(flow_analysis, investigation);
    inv_build_posture(flow_analysis, investigation);
    inv_build_dns_findings(summary, investigation);
    inv_load_ip_iocs(flow_analysis, investigation);
    inv_load_domain_iocs(summary, flow_analysis, investigation);
    inv_load_port_rules(flow_analysis, investigation);
    inv_build_timeline(flow_analysis, investigation);
}

static void inv_append(char *output, size_t output_size, size_t *position,
                       const char *format, ...)
{
    if (!output || !position || *position >= output_size) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *position, output_size - *position, format, args);
    va_end(args);
    if (written < 0) return;
    size_t available = output_size - *position;
    if ((size_t)written >= available) *position = output_size - 1U;
    else *position += (size_t)written;
}

bool pcap_investigation_device_dossier(
    const pcap_flow_analysis_t *analysis,
    const pcap_investigation_t *investigation,
    uint16_t representative_index,
    pcap_device_dossier_t *dossier)
{
    if (!analysis || !dossier || !inv_is_representative(analysis, representative_index)) {
        return false;
    }
    memset(dossier, 0, sizeof(*dossier));
    dossier->representative_index = representative_index;
    const pcap_device_entry_t *device = &analysis->devices[representative_index];
    inv_infer_role(analysis, device, dossier->role, sizeof(dossier->role));
    inv_copy(dossier->hostname, sizeof(dossier->hostname), device->hostname);
    inv_copy(dossier->mac, sizeof(dossier->mac), device->mac);
    inv_lookup_vendor(device->mac, dossier->vendor, sizeof(dossier->vendor));
    dossier->first_time_us = UINT64_MAX;
    size_t address_position = 0U;
    size_t service_position = 0U;
    struct {
        uint16_t port;
        uint8_t protocol;
    } services[16];
    uint16_t service_count = 0U;
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        const pcap_device_entry_t *alias = &analysis->devices[i];
        if (!pcap_flow_same_local_device(device, alias)) continue;
        if (!dossier->hostname[0] && alias->hostname[0]) {
            inv_copy(dossier->hostname, sizeof(dossier->hostname), alias->hostname);
        }
        inv_append(dossier->addresses, sizeof(dossier->addresses), &address_position,
                   "%s%s", dossier->alias_count ? " | " : "", alias->address);
        dossier->alias_count++;
        dossier->sent_bytes += alias->sent_bytes;
        dossier->received_bytes += alias->received_bytes;
        if (alias->first_time_us < dossier->first_time_us) {
            dossier->first_time_us = alias->first_time_us;
        }
        if (alias->last_time_us > dossier->last_time_us) dossier->last_time_us = alias->last_time_us;
        for (uint16_t s = 0; s < alias->service_count; s++) {
            bool duplicate = false;
            for (uint16_t known = 0; known < service_count; known++) {
                if (services[known].port == alias->services[s].port &&
                    services[known].protocol == alias->services[s].ip_protocol) duplicate = true;
            }
            if (!duplicate && service_count < 16U) {
                services[service_count].port = alias->services[s].port;
                services[service_count].protocol = alias->services[s].ip_protocol;
                service_count++;
            }
        }
    }
    dossier->service_count = service_count;
    for (uint16_t i = 0; i < service_count; i++) {
        inv_append(dossier->services, sizeof(dossier->services), &service_position,
                   "%s%s/%u", i ? ", " : "",
                   pcap_flow_transport_name(services[i].protocol), services[i].port);
    }
    if (dossier->first_time_us == UINT64_MAX) dossier->first_time_us = 0U;

    struct {
        char address[64];
        uint64_t bytes;
    } peers[8] = {0};
    uint16_t peer_count = 0U;
    char domains[8][96] = {{0}};
    uint16_t domain_count = 0U;
    for (uint16_t flow_id = 0; flow_id < analysis->flow_count; flow_id++) {
        const pcap_flow_entry_t *flow = &analysis->flows[flow_id];
        const char *peer = NULL;
        if (inv_group_has_address(analysis, device, flow->originator)) peer = flow->responder;
        else if (inv_group_has_address(analysis, device, flow->responder)) peer = flow->originator;
        if (!peer) continue;
        dossier->flow_count++;
        if (flow->server_name[0]) {
            bool domain_known = false;
            for (uint16_t i = 0; i < domain_count; i++) {
                if (strcasecmp(domains[i], flow->server_name) == 0) domain_known = true;
            }
            if (!domain_known && domain_count < 8U) {
                inv_copy(domains[domain_count++], sizeof(domains[0]), flow->server_name);
            }
        }
        uint64_t bytes = flow->originator_bytes + flow->responder_bytes;
        uint16_t peer_index = UINT16_MAX;
        for (uint16_t i = 0; i < peer_count; i++) {
            if (strcasecmp(peers[i].address, peer) == 0) peer_index = i;
        }
        if (peer_index == UINT16_MAX && peer_count < 8U) {
            peer_index = peer_count++;
            inv_copy(peers[peer_index].address, sizeof(peers[peer_index].address), peer);
        }
        if (peer_index != UINT16_MAX) peers[peer_index].bytes += bytes;
    }
    dossier->remote_peer_count = peer_count;
    for (uint16_t i = 0; i < peer_count; i++) {
        uint16_t best = i;
        for (uint16_t j = i + 1U; j < peer_count; j++) {
            if (peers[j].bytes > peers[best].bytes) best = j;
        }
        if (best != i) {
            char address[64];
            uint64_t bytes = peers[i].bytes;
            inv_copy(address, sizeof(address), peers[i].address);
            peers[i] = peers[best];
            inv_copy(peers[best].address, sizeof(peers[best].address), address);
            peers[best].bytes = bytes;
        }
    }
    size_t peer_position = 0U;
    for (uint16_t i = 0; i < peer_count && i < 5U; i++) {
        inv_append(dossier->top_peers, sizeof(dossier->top_peers), &peer_position,
                   "%s%s (%llu B)", i ? ", " : "", peers[i].address,
                   (unsigned long long)peers[i].bytes);
    }
    dossier->domain_count = domain_count;
    size_t domain_position = 0U;
    for (uint16_t i = 0; i < domain_count; i++) {
        inv_append(dossier->top_domains, sizeof(dossier->top_domains), &domain_position,
                   "%s%s", i ? ", " : "", domains[i]);
    }

    if (investigation) {
        for (uint32_t i = 0; i < investigation->finding_count; i++) {
            const pcap_investigation_finding_t *finding = &investigation->findings[i];
            if (!inv_group_has_address(analysis, device, finding->source) &&
                !inv_group_has_address(analysis, device, finding->target)) continue;
            dossier->finding_count++;
            uint8_t points = finding->severity >= PCAP_HEALTH_CRITICAL ? 40U :
                             finding->severity >= PCAP_HEALTH_SUSPICIOUS ? 25U : 10U;
            unsigned risk = dossier->risk_score + points;
            dossier->risk_score = risk > 100U ? 100U : (uint8_t)risk;
        }
    }
    return true;
}

static uint32_t inv_service_hash(const pcap_flow_analysis_t *analysis,
                                 const pcap_device_entry_t *representative)
{
    uint32_t keys[48];
    uint16_t key_count = 0U;
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        const pcap_device_entry_t *alias = &analysis->devices[i];
        if (!pcap_flow_same_local_device(representative, alias)) continue;
        for (uint16_t s = 0; s < alias->service_count; s++) {
            uint32_t key = ((uint32_t)alias->services[s].ip_protocol << 16U) |
                           alias->services[s].port;
            bool duplicate = false;
            for (uint16_t known = 0; known < key_count; known++) {
                if (keys[known] == key) duplicate = true;
            }
            if (!duplicate &&
                key_count < (uint16_t)(sizeof(keys) / sizeof(keys[0]))) {
                keys[key_count++] = key;
            }
        }
    }
    for (uint16_t i = 1U; i < key_count; i++) {
        uint32_t key = keys[i];
        uint16_t position = i;
        while (position > 0U && keys[position - 1U] > key) {
            keys[position] = keys[position - 1U];
            position--;
        }
        keys[position] = key;
    }
    uint32_t hash = 2166136261U;
    for (uint16_t i = 0; i < key_count; i++) {
        hash ^= keys[i];
        hash *= 16777619U;
    }
    return hash;
}

static void inv_local_key(const pcap_device_entry_t *device, char *output, size_t output_size)
{
    inv_copy(output, output_size, device && device->mac[0] ? device->mac :
                                     (device ? device->address : ""));
}

pcap_investigation_status_t pcap_investigation_baseline_save(
    const char *source_path, const pcap_summary_t *summary,
    const pcap_flow_analysis_t *analysis, char *output_path, size_t output_path_size)
{
    if (!source_path || !summary || !analysis) return PCAP_INVESTIGATION_INVALID_ARG;
    if (!inv_prepare_dirs()) return PCAP_INVESTIGATION_IO_ERROR;
    char temp_path[PCAP_INVESTIGATION_PATH_MAX];
    int written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", PCAP_INV_BASELINE_PATH);
    if (written <= 0 || (size_t)written >= sizeof(temp_path)) return PCAP_INVESTIGATION_LIMIT;
    FILE *file = fopen(temp_path, "wb");
    if (!file) return PCAP_INVESTIGATION_IO_ERROR;
    pcap_inv_baseline_header_t header = {0};
    memcpy(header.magic, PCAP_INV_BASELINE_MAGIC, strlen(PCAP_INV_BASELINE_MAGIC));
    header.schema_version = PCAP_INVESTIGATION_BASELINE_SCHEMA;
    header.header_size = sizeof(header);
    header.local_count = pcap_flow_local_device_count(analysis);
    header.remote_count = pcap_flow_remote_endpoint_count(analysis);
    header.domain_count = summary->dns_domain_count;
    inv_copy(header.source, sizeof(header.source), inv_basename(source_path));
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1;
    for (uint16_t i = 0; ok && i < analysis->device_count; i++) {
        if (!inv_is_representative(analysis, i)) continue;
        pcap_inv_baseline_local_t record = {0};
        inv_local_key(&analysis->devices[i], record.key, sizeof(record.key));
        inv_copy(record.hostname, sizeof(record.hostname), analysis->devices[i].hostname);
        inv_infer_role(analysis, &analysis->devices[i], record.role, sizeof(record.role));
        record.service_hash = inv_service_hash(analysis, &analysis->devices[i]);
        ok = fwrite(&record, sizeof(record), 1, file) == 1;
    }
    for (uint32_t i = 0; ok && i < analysis->device_count; i++) {
        if (analysis->devices[i].internal) continue;
        pcap_inv_baseline_text_t record = {0};
        inv_copy(record.value, sizeof(record.value), analysis->devices[i].address);
        ok = fwrite(&record, sizeof(record), 1, file) == 1;
    }
    for (uint16_t i = 0; ok && i < summary->dns_domain_count; i++) {
        pcap_inv_baseline_text_t record = {0};
        inv_copy(record.value, sizeof(record.value), summary->dns_domains[i].label);
        ok = fwrite(&record, sizeof(record), 1, file) == 1;
    }
    if (ok) ok = fflush(file) == 0;
    if (ok) {
        int descriptor = fileno(file);
        if (descriptor >= 0) ok = fsync(descriptor) == 0;
    }
    if (fclose(file) != 0) ok = false;
    char backup_path[PCAP_INVESTIGATION_PATH_MAX];
    written = snprintf(backup_path, sizeof(backup_path), "%s.bak", PCAP_INV_BASELINE_PATH);
    if (written <= 0 || (size_t)written >= sizeof(backup_path)) ok = false;
    struct stat existing;
    bool had_existing = ok && stat(PCAP_INV_BASELINE_PATH, &existing) == 0;
    if (ok && had_existing) {
        unlink(backup_path);
        ok = rename(PCAP_INV_BASELINE_PATH, backup_path) == 0;
    }
    if (!ok || rename(temp_path, PCAP_INV_BASELINE_PATH) != 0) {
        if (had_existing) rename(backup_path, PCAP_INV_BASELINE_PATH);
        unlink(temp_path);
        return PCAP_INVESTIGATION_IO_ERROR;
    }
    if (had_existing) unlink(backup_path);
    if (output_path && output_path_size) {
        inv_copy(output_path, output_path_size, PCAP_INV_BASELINE_PATH);
    }
    return PCAP_INVESTIGATION_OK;
}

static bool inv_read_baseline_header(FILE *file, pcap_inv_baseline_header_t *header)
{
    return file && header && fread(header, sizeof(*header), 1, file) == 1 &&
           memcmp(header->magic, PCAP_INV_BASELINE_MAGIC,
                  strlen(PCAP_INV_BASELINE_MAGIC)) == 0 &&
           header->schema_version == PCAP_INVESTIGATION_BASELINE_SCHEMA &&
           header->header_size == sizeof(*header) && header->local_count <= 128U &&
           header->remote_count <= 128U && header->domain_count <= 64U;
}

static bool inv_baseline_has_local(FILE *file, const pcap_inv_baseline_header_t *header,
                                   const char *key, uint32_t *service_hash_out)
{
    if (!file || !header || !key) return false;
    if (fseek(file, (long)sizeof(*header), SEEK_SET) != 0) return false;
    for (uint32_t i = 0; i < header->local_count; i++) {
        pcap_inv_baseline_local_t record;
        if (fread(&record, sizeof(record), 1, file) != 1) return false;
        if (strcasecmp(record.key, key) == 0) {
            if (service_hash_out) *service_hash_out = record.service_hash;
            return true;
        }
    }
    return false;
}

static bool inv_baseline_has_text(FILE *file, const pcap_inv_baseline_header_t *header,
                                  bool domains, const char *value)
{
    long offset = (long)sizeof(*header) +
                  (long)(header->local_count * sizeof(pcap_inv_baseline_local_t));
    uint32_t count = header->remote_count;
    if (domains) {
        offset += (long)(header->remote_count * sizeof(pcap_inv_baseline_text_t));
        count = header->domain_count;
    }
    if (fseek(file, offset, SEEK_SET) != 0) return false;
    for (uint32_t i = 0; i < count; i++) {
        pcap_inv_baseline_text_t record;
        if (fread(&record, sizeof(record), 1, file) != 1) return false;
        if ((domains && inv_domain_matches(value, record.value)) ||
            (!domains && strcasecmp(value, record.value) == 0)) return true;
    }
    return false;
}

static void inv_diff_item(pcap_investigation_diff_t *diff, const char *prefix,
                          const char *value)
{
    if (diff->item_count >= PCAP_INVESTIGATION_MAX_DIFF_ITEMS) {
        diff->item_limited = true;
        return;
    }
    snprintf(diff->items[diff->item_count++], sizeof(diff->items[0]),
             "%.20s: %.104s", prefix, value ? value : "");
}

pcap_investigation_status_t pcap_investigation_baseline_compare(
    const pcap_summary_t *summary, const pcap_flow_analysis_t *analysis,
    pcap_investigation_diff_t *diff)
{
    if (!summary || !analysis || !diff) return PCAP_INVESTIGATION_INVALID_ARG;
    memset(diff, 0, sizeof(*diff));
    FILE *file = fopen(PCAP_INV_BASELINE_PATH, "rb");
    if (!file) return PCAP_INVESTIGATION_NOT_FOUND;
    pcap_inv_baseline_header_t header;
    if (!inv_read_baseline_header(file, &header)) {
        fclose(file);
        return PCAP_INVESTIGATION_INVALID_FORMAT;
    }
    diff->baseline_available = true;
    inv_copy(diff->baseline_source, sizeof(diff->baseline_source), header.source);
    for (uint16_t i = 0; i < analysis->device_count; i++) {
        if (!inv_is_representative(analysis, i)) continue;
        char key[64];
        inv_local_key(&analysis->devices[i], key, sizeof(key));
        uint32_t old_service_hash = 0U;
        if (!inv_baseline_has_local(file, &header, key, &old_service_hash)) {
            diff->new_local_devices++;
            inv_diff_item(diff, "NEW LOCAL", analysis->devices[i].hostname[0]
                                                ? analysis->devices[i].hostname : key);
        } else if (old_service_hash != inv_service_hash(analysis, &analysis->devices[i])) {
            diff->new_services++;
            inv_diff_item(diff, "SERVICE CHANGE", analysis->devices[i].hostname[0]
                                                    ? analysis->devices[i].hostname : key);
        }
    }
    if (fseek(file, (long)sizeof(header), SEEK_SET) == 0) {
        for (uint32_t i = 0; i < header.local_count; i++) {
            pcap_inv_baseline_local_t record;
            if (fread(&record, sizeof(record), 1, file) != 1) break;
            bool found = false;
            for (uint16_t current = 0; current < analysis->device_count; current++) {
                if (!inv_is_representative(analysis, current)) continue;
                char key[64];
                inv_local_key(&analysis->devices[current], key, sizeof(key));
                if (strcasecmp(key, record.key) == 0) found = true;
            }
            if (!found) {
                diff->missing_local_devices++;
                inv_diff_item(diff, "MISSING LOCAL",
                              record.hostname[0] ? record.hostname : record.key);
            }
        }
    }
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        if (analysis->devices[i].internal ||
            inv_baseline_has_text(file, &header, false, analysis->devices[i].address)) continue;
        diff->new_remote_endpoints++;
        inv_diff_item(diff, "NEW REMOTE", analysis->devices[i].address);
    }
    for (uint16_t i = 0; i < summary->dns_domain_count; i++) {
        if (inv_baseline_has_text(file, &header, true, summary->dns_domains[i].label)) continue;
        diff->new_domains++;
        inv_diff_item(diff, "NEW DOMAIN", summary->dns_domains[i].label);
    }
    fclose(file);
    return PCAP_INVESTIGATION_OK;
}

static void inv_html_text(FILE *file, const char *text)
{
    if (!file || !text) return;
    for (; *text; text++) {
        switch (*text) {
            case '&': fputs("&amp;", file); break;
            case '<': fputs("&lt;", file); break;
            case '>': fputs("&gt;", file); break;
            case '"': fputs("&quot;", file); break;
            default: fputc(*text, file); break;
        }
    }
}

pcap_investigation_status_t pcap_investigation_export_html(
    const char *source_path, const pcap_summary_t *summary,
    const pcap_flow_analysis_t *analysis, const pcap_investigation_t *investigation,
    const pcap_investigation_diff_t *diff, char *output_path, size_t output_path_size)
{
    if (!source_path || !summary || !analysis || !investigation) {
        return PCAP_INVESTIGATION_INVALID_ARG;
    }
    if (!inv_prepare_dirs()) return PCAP_INVESTIGATION_IO_ERROR;
    char stem[64];
    inv_safe_stem(source_path, stem, sizeof(stem));
    char final_path[PCAP_INVESTIGATION_PATH_MAX];
    int written = -1;
    for (unsigned sequence = 1U; sequence <= 999U; sequence++) {
        written = snprintf(final_path, sizeof(final_path), "%s/%s_investigation_%u.html",
                           PCAP_INV_EXPORT_ROOT, stem, sequence);
        if (written <= 0 || (size_t)written >= sizeof(final_path)) {
            return PCAP_INVESTIGATION_LIMIT;
        }
        struct stat item;
        if (stat(final_path, &item) != 0) break;
        if (sequence == 999U) return PCAP_INVESTIGATION_LIMIT;
    }
    char temp_path[PCAP_INVESTIGATION_PATH_MAX + 8U];
    written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", final_path);
    if (written <= 0 || (size_t)written >= sizeof(temp_path)) return PCAP_INVESTIGATION_LIMIT;
    FILE *file = fopen(temp_path, "w");
    if (!file) return PCAP_INVESTIGATION_IO_ERROR;
    fputs("<!doctype html><meta charset=\"utf-8\"><title>ESPShark Investigation</title>"
          "<style>body{font-family:system-ui;background:#07111f;color:#e8f4ff;margin:24px}"
          "h1,h2{color:#52b6ff}.card{background:#10243a;border:1px solid #3178a8;border-radius:10px;padding:14px;margin:12px 0}"
          "table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #29445c;text-align:left}"
          ".WATCH{color:#ffd166}.SUSPICIOUS{color:#ff8c42}.CRITICAL{color:#ff4d6d}.note{color:#9bb4c8}</style>", file);
    fputs("<h1>ESPShark Offline Investigation</h1><div class=\"card\"><b>Capture:</b> ", file);
    inv_html_text(file, inv_basename(source_path));
    fprintf(file, "<br>Indexed packets: %lu<br>Local devices: %lu<br>Remote endpoints: %lu<br>Flows: %lu<br>Findings: %lu</div>",
            (unsigned long)analysis->analyzed_packets,
            (unsigned long)pcap_flow_local_device_count(analysis),
            (unsigned long)pcap_flow_remote_endpoint_count(analysis),
            (unsigned long)analysis->flow_count,
            (unsigned long)investigation->finding_count);
    fputs("<p class=\"note\">Passive, bounded evidence. An observed service is not proof of general port exposure or vulnerability.</p>", file);
    fputs("<h2>Findings</h2><table><tr><th>Severity</th><th>Type</th><th>Source</th><th>Target</th><th>Evidence</th></tr>", file);
    for (uint32_t i = 0; i < investigation->finding_count; i++) {
        const pcap_investigation_finding_t *finding = &investigation->findings[i];
        const char *severity = pcap_flow_health_name((pcap_health_level_t)finding->severity);
        fprintf(file, "<tr><td class=\"%s\">", severity);
        inv_html_text(file, severity);
        fputs("</td><td>", file);
        inv_html_text(file, pcap_investigation_finding_name(
                                (pcap_investigation_finding_type_t)finding->type));
        fputs("</td><td>", file); inv_html_text(file, finding->source);
        fputs("</td><td>", file); inv_html_text(file, finding->target);
        fprintf(file, "</td><td>flow %u | packet %lu | port %u<br>",
                finding->flow_id == PCAP_FLOW_ID_NONE ? 0U : finding->flow_id + 1U,
                (unsigned long)finding->first_packet + 1U, finding->service_port);
        inv_html_text(file, finding->detail); fputs("</td></tr>", file);
    }
    fputs("</table><h2>Timeline</h2><table><tr><th>Time (us)</th><th>Event</th><th>Actor</th><th>Evidence</th></tr>", file);
    for (uint32_t i = 0; i < investigation->timeline_count; i++) {
        const pcap_investigation_event_t *event = &investigation->timeline[i];
        fprintf(file, "<tr><td>%llu</td><td>", (unsigned long long)event->time_us);
        inv_html_text(file, pcap_investigation_event_name(
                                (pcap_investigation_event_type_t)event->type));
        fputs("</td><td>", file); inv_html_text(file, event->actor);
        fputs("</td><td>", file); inv_html_text(file, event->detail); fputs("</td></tr>", file);
    }
    fputs("</table><h2>Local devices</h2><table><tr><th>Identity</th><th>Role</th><th>Addresses</th><th>Services</th><th>Traffic</th></tr>", file);
    for (uint16_t i = 0; i < analysis->device_count; i++) {
        if (!inv_is_representative(analysis, i)) continue;
        pcap_device_dossier_t dossier;
        if (!pcap_investigation_device_dossier(analysis, investigation, i, &dossier)) continue;
        fputs("<tr><td>", file); inv_html_text(file, dossier.hostname[0] ? dossier.hostname : dossier.mac);
        if (dossier.vendor[0]) { fputs("<br>", file); inv_html_text(file, dossier.vendor); }
        fputs("</td><td>", file); inv_html_text(file, dossier.role);
        fputs("</td><td>", file); inv_html_text(file, dossier.addresses);
        fputs("</td><td>", file); inv_html_text(file, dossier.services);
        fprintf(file, "</td><td>TX %llu B / RX %llu B / risk %u</td></tr>",
                (unsigned long long)dossier.sent_bytes,
                (unsigned long long)dossier.received_bytes, dossier.risk_score);
    }
    fputs("</table>", file);
    if (diff && diff->baseline_available) {
        fprintf(file, "<h2>Baseline comparison</h2><div class=\"card\">Baseline: ");
        inv_html_text(file, diff->baseline_source);
        fprintf(file, "<br>New local: %lu | Missing local: %lu | New remote: %lu | New domains: %lu | Service changes: %lu</div><ul>",
                (unsigned long)diff->new_local_devices,
                (unsigned long)diff->missing_local_devices,
                (unsigned long)diff->new_remote_endpoints,
                (unsigned long)diff->new_domains,
                (unsigned long)diff->new_services);
        for (uint32_t i = 0; i < diff->item_count; i++) {
            fputs("<li>", file); inv_html_text(file, diff->items[i]); fputs("</li>", file);
        }
        fputs("</ul>", file);
    }
    fputs("<p class=\"note\">Generated offline by ESPShark on Tab5.</p>", file);
    bool ok = fflush(file) == 0;
    if (ok) {
        int descriptor = fileno(file);
        if (descriptor >= 0) ok = fsync(descriptor) == 0;
    }
    if (fclose(file) != 0) ok = false;
    if (!ok || rename(temp_path, final_path) != 0) {
        unlink(temp_path);
        return PCAP_INVESTIGATION_IO_ERROR;
    }
    if (output_path && output_path_size) inv_copy(output_path, output_path_size, final_path);
    return PCAP_INVESTIGATION_OK;
}

const char *pcap_investigation_finding_name(pcap_investigation_finding_type_t type)
{
    static const char *names[] = {
        "ENGINE ALERT", "ADMIN SERVICE", "EXTERNAL ADMIN ACCESS", "IOT CLEARTEXT",
        "MULTIPLE DHCP SERVERS", "LLMNR/NBNS SURFACE", "SUSPICIOUS DNS NAME",
        "ENCRYPTED DNS", "SMB1", "MQTT CLEARTEXT", "WIFI DEAUTH BURST",
        "IOC IP MATCH", "IOC DOMAIN MATCH", "FORBIDDEN PORT",
        "CLEARTEXT CREDENTIAL MARKER"
    };
    return (unsigned)type < sizeof(names) / sizeof(names[0]) ? names[type] : "UNKNOWN";
}

const char *pcap_investigation_event_name(pcap_investigation_event_type_t type)
{
    switch (type) {
        case PCAP_INV_EVENT_DEVICE_FIRST_SEEN: return "DEVICE FIRST SEEN";
        case PCAP_INV_EVENT_FLOW_FIRST_SEEN: return "FLOW FIRST SEEN";
        case PCAP_INV_EVENT_FINDING: return "FINDING";
        default: return "EVENT";
    }
}

const char *pcap_investigation_status_name(pcap_investigation_status_t status)
{
    switch (status) {
        case PCAP_INVESTIGATION_OK: return "OK";
        case PCAP_INVESTIGATION_INVALID_ARG: return "INVALID ARGUMENT";
        case PCAP_INVESTIGATION_NOT_FOUND: return "NOT FOUND";
        case PCAP_INVESTIGATION_IO_ERROR: return "I/O ERROR";
        case PCAP_INVESTIGATION_INVALID_FORMAT: return "INVALID FORMAT";
        case PCAP_INVESTIGATION_LIMIT: return "LIMIT";
        default: return "UNKNOWN";
    }
}
