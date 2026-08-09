#include "pcap_flow.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PCAP_FLOW_CLASSIFY_BYTES 512U
#define PCAP_FLOW_STREAM_READ   4096U

typedef struct {
    uint32_t packet_number;
    uint32_t sequence;
    uint32_t payload_offset;
    uint32_t payload_length;
    uint8_t direction;
} pcap_stream_segment_t;

static uint64_t packet_time_us(const pcap_packet_index_t *packet,
                               pcap_timestamp_resolution_t resolution)
{
    if (!packet) return 0;
    uint64_t fraction = packet->timestamp_fraction;
    if (resolution == PCAP_TIMESTAMP_NANOSECONDS) fraction /= 1000U;
    return (uint64_t)packet->timestamp_seconds * 1000000ULL + fraction;
}

static bool port_is(uint16_t source, uint16_t destination, uint16_t port)
{
    return source == port || destination == port;
}

static bool port_in_range(uint16_t source, uint16_t destination,
                          uint16_t first, uint16_t last)
{
    return (source >= first && source <= last) ||
           (destination >= first && destination <= last);
}

static bool payload_starts(const uint8_t *payload, size_t length, const char *text)
{
    size_t text_length = strlen(text);
    return payload && length >= text_length && memcmp(payload, text, text_length) == 0;
}

static bool payload_contains(const uint8_t *payload, size_t length,
                             const uint8_t *needle, size_t needle_length)
{
    if (!payload || !needle || needle_length == 0 || length < needle_length) return false;
    for (size_t i = 0; i + needle_length <= length; i++) {
        if (memcmp(payload + i, needle, needle_length) == 0) return true;
    }
    return false;
}

static void copy_text(char *destination, size_t destination_size,
                      const char *source, size_t source_length)
{
    if (!destination || destination_size == 0U) return;
    size_t count = 0;
    if (source) {
        while (count + 1U < destination_size && count < source_length && source[count]) {
            destination[count] = source[count];
            count++;
        }
    }
    destination[count] = '\0';
}

static bool address_is_internal(const char *address)
{
    if (!address || !address[0]) return false;
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(address, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        (void)c;
        (void)d;
        return a == 10U || a == 127U || (a == 192U && b == 168U) ||
               (a == 172U && b >= 16U && b <= 31U) ||
               (a == 169U && b == 254U);
    }
    return strncasecmp(address, "fe80:", 5U) == 0 ||
           strncasecmp(address, "fc", 2U) == 0 ||
           strncasecmp(address, "fd", 2U) == 0 || strcmp(address, "::1") == 0;
}

static bool address_is_multicast(const char *address)
{
    if (!address || !address[0]) return false;
    unsigned first = 0;
    if (sscanf(address, "%u", &first) == 1 && strchr(address, '.')) {
        return first >= 224U || strcmp(address, "255.255.255.255") == 0;
    }
    return strncasecmp(address, "ff", 2U) == 0;
}

static bool mac_is_group(const char *mac)
{
    if (!mac || !mac[0]) return false;
    unsigned first = 0;
    return sscanf(mac, "%2x", &first) == 1 && (first & 1U) != 0U;
}

static uint16_t read_payload_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void extract_http_metadata(const uint8_t *payload, size_t length,
                                  pcap_flow_entry_t *flow)
{
    if (!payload || !flow || length == 0U) return;
    size_t line_length = 0;
    while (line_length < length && line_length < 120U && payload[line_length] != '\r' &&
           payload[line_length] != '\n') line_length++;
    if (line_length > 0U && flow->application_detail[0] == '\0') {
        copy_text(flow->application_detail, sizeof(flow->application_detail),
                  (const char *)payload, line_length);
    }
    for (size_t i = 0; i + 6U < length; i++) {
        if ((i == 0U || payload[i - 1U] == '\n') &&
            strncasecmp((const char *)payload + i, "Host:", 5U) == 0) {
            size_t start = i + 5U;
            while (start < length && (payload[start] == ' ' || payload[start] == '\t')) start++;
            size_t end = start;
            while (end < length && payload[end] != '\r' && payload[end] != '\n') end++;
            copy_text(flow->server_name, sizeof(flow->server_name),
                      (const char *)payload + start, end - start);
            break;
        }
    }
}

static void extract_tls_client_hello(const uint8_t *payload, size_t length,
                                     pcap_flow_entry_t *flow)
{
    if (!payload || !flow || length < 11U || payload[0] != 0x16U ||
        payload[5] != 0x01U) return;
    flow->tls_version = read_payload_be16(payload + 9U);
    size_t position = 43U;
    if (position >= length) return;
    size_t session_length = payload[position++];
    if (position + session_length + 2U > length) return;
    position += session_length;
    size_t cipher_length = read_payload_be16(payload + position);
    position += 2U;
    if (position + cipher_length + 1U > length) return;
    position += cipher_length;
    size_t compression_length = payload[position++];
    if (position + compression_length + 2U > length) return;
    position += compression_length;
    size_t extensions_length = read_payload_be16(payload + position);
    position += 2U;
    size_t extensions_end = position + extensions_length;
    if (extensions_end > length) extensions_end = length;
    char alpn[32] = {0};
    while (position + 4U <= extensions_end) {
        uint16_t type = read_payload_be16(payload + position);
        size_t extension_length = read_payload_be16(payload + position + 2U);
        position += 4U;
        if (position + extension_length > extensions_end) break;
        if (type == 0U && extension_length >= 5U) {
            size_t name_position = position + 2U;
            if (name_position + 3U <= position + extension_length &&
                payload[name_position] == 0U) {
                size_t name_length = read_payload_be16(payload + name_position + 1U);
                name_position += 3U;
                if (name_position + name_length <= position + extension_length) {
                    copy_text(flow->server_name, sizeof(flow->server_name),
                              (const char *)payload + name_position, name_length);
                }
            }
        } else if (type == 16U && extension_length >= 3U) {
            size_t alpn_position = position + 2U;
            if (alpn_position < position + extension_length) {
                size_t alpn_length = payload[alpn_position++];
                if (alpn_position + alpn_length <= position + extension_length) {
                    copy_text(alpn, sizeof(alpn),
                              (const char *)payload + alpn_position, alpn_length);
                }
            }
        }
        position += extension_length;
    }
    if (flow->application_detail[0] == '\0') {
        if (alpn[0]) {
            snprintf(flow->application_detail, sizeof(flow->application_detail),
                     "TLS 0x%04X | ALPN %s", flow->tls_version, alpn);
        } else {
            snprintf(flow->application_detail, sizeof(flow->application_detail),
                     "TLS 0x%04X", flow->tls_version);
        }
    }
}

static void extract_bittorrent_metadata(const uint8_t *payload, size_t length,
                                        pcap_flow_entry_t *flow)
{
    if (!payload || !flow || length < 48U || payload[0] != 19U ||
        flow->application_detail[0]) return;
    size_t position = 0;
    int written = snprintf(flow->application_detail, sizeof(flow->application_detail),
                           "info_hash=");
    if (written < 0) return;
    position = (size_t)written;
    for (size_t i = 28U; i < 48U && position + 2U < sizeof(flow->application_detail); i++) {
        written = snprintf(flow->application_detail + position,
                           sizeof(flow->application_detail) - position, "%02x", payload[i]);
        if (written != 2) break;
        position += 2U;
    }
}

static void update_flow_metadata(pcap_flow_entry_t *flow, pcap_application_t application,
                                 const uint8_t *payload, size_t payload_length)
{
    if (!flow || !payload || payload_length == 0U) return;
    if (application == PCAP_APP_HTTP) extract_http_metadata(payload, payload_length, flow);
    if (application == PCAP_APP_TLS) extract_tls_client_hello(payload, payload_length, flow);
    if (application == PCAP_APP_BITTORRENT) {
        extract_bittorrent_metadata(payload, payload_length, flow);
    }
}

static bool looks_like_http(const uint8_t *payload, size_t length)
{
    static const char *markers[] = {
        "GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ",
        "PATCH ", "CONNECT ", "HTTP/"
    };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if (payload_starts(payload, length, markers[i])) return true;
    }
    return false;
}

static bool looks_like_tls(const uint8_t *payload, size_t length)
{
    return payload && length >= 5U && payload[0] >= 20U && payload[0] <= 23U &&
           payload[1] == 3U && payload[2] <= 4U;
}

static bool looks_like_quic(const uint8_t *payload, size_t length)
{
    if (!payload || length < 6U || (payload[0] & 0xC0U) != 0xC0U) return false;
    return payload[1] != 0U || payload[2] != 0U || payload[3] != 0U || payload[4] != 0U;
}

static bool looks_like_bittorrent(const uint8_t *payload, size_t length)
{
    static const uint8_t marker[] = "BitTorrent protocol";
    return payload && length >= 20U && payload[0] == 19U &&
           memcmp(payload + 1, marker, sizeof(marker) - 1U) == 0;
}

static bool looks_like_bittorrent_dht(const uint8_t *payload, size_t length)
{
    static const uint8_t query_marker[] = "1:y1:q";
    static const uint8_t response_marker[] = "1:y1:r";
    return payload && length >= 12U && payload[0] == 'd' &&
           (payload_contains(payload, length, query_marker, sizeof(query_marker) - 1U) ||
            payload_contains(payload, length, response_marker, sizeof(response_marker) - 1U));
}

static bool looks_like_mqtt(const uint8_t *payload, size_t length)
{
    static const uint8_t mqtt31[] = {0x00, 0x04, 'M', 'Q', 'T', 'T'};
    static const uint8_t mqtt3[] = {0x00, 0x06, 'M', 'Q', 'I', 's', 'd', 'p'};
    return payload && length >= 8U && (payload[0] & 0xF0U) == 0x10U &&
           (payload_contains(payload, length, mqtt31, sizeof(mqtt31)) ||
            payload_contains(payload, length, mqtt3, sizeof(mqtt3)));
}

static bool looks_like_smb(const uint8_t *payload, size_t length)
{
    static const uint8_t smb1[] = {0xFF, 'S', 'M', 'B'};
    static const uint8_t smb2[] = {0xFE, 'S', 'M', 'B'};
    return payload_contains(payload, length, smb1, sizeof(smb1)) ||
           payload_contains(payload, length, smb2, sizeof(smb2));
}

static pcap_application_t classify_application(const pcap_packet_details_t *details,
                                               const uint8_t *payload,
                                               size_t payload_length,
                                               pcap_app_confidence_t *confidence)
{
    *confidence = PCAP_APP_CONFIDENCE_TRANSPORT;
    if (!details) return PCAP_APP_UNKNOWN;
    uint16_t source = details->source_port;
    uint16_t destination = details->destination_port;

    if (details->flags & PCAP_PACKET_FLAG_ARP) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_ARP;
    }
    if (details->flags & PCAP_PACKET_FLAG_EAPOL) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_EAPOL;
    }
    if (details->flags & PCAP_PACKET_FLAG_WIFI_MGMT) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_WIFI_MGMT;
    }
    if (details->ip_protocol == 1) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_ICMP;
    }
    if (details->ip_protocol == 58) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_ICMPV6;
    }

    if (looks_like_bittorrent(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_BITTORRENT;
    }
    if (details->ip_protocol == 17 && looks_like_bittorrent_dht(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_BITTORRENT_DHT;
    }
    if (looks_like_http(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_HTTP;
    }
    if (looks_like_tls(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_TLS;
    }
    if (details->ip_protocol == 17 && looks_like_quic(payload, payload_length) &&
        port_is(source, destination, 443)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_QUIC;
    }
    if (payload_starts(payload, payload_length, "SSH-")) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_SSH;
    }
    if (looks_like_smb(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_SMB;
    }
    if (looks_like_mqtt(payload, payload_length)) {
        *confidence = PCAP_APP_CONFIDENCE_CONFIRMED;
        return PCAP_APP_MQTT;
    }
    if (details->dns_valid || port_is(source, destination, 53) ||
        port_is(source, destination, 5353)) {
        *confidence = details->dns_valid ? PCAP_APP_CONFIDENCE_CONFIRMED
                                        : PCAP_APP_CONFIDENCE_LIKELY;
        return port_is(source, destination, 5353) ? PCAP_APP_MDNS : PCAP_APP_DNS;
    }

    *confidence = PCAP_APP_CONFIDENCE_LIKELY;
    if (port_is(source, destination, 67) || port_is(source, destination, 68)) return PCAP_APP_DHCP;
    if (port_is(source, destination, 5355)) return PCAP_APP_LLMNR;
    if (port_is(source, destination, 137)) return PCAP_APP_NBNS;
    if (port_is(source, destination, 1900)) return PCAP_APP_SSDP;
    if (port_is(source, destination, 123)) return PCAP_APP_NTP;
    if (port_is(source, destination, 80) || port_is(source, destination, 8080) ||
        port_is(source, destination, 8000)) return PCAP_APP_HTTP;
    if (port_is(source, destination, 443) || port_is(source, destination, 8443)) {
        return details->ip_protocol == 17 ? PCAP_APP_QUIC : PCAP_APP_TLS;
    }
    if (port_is(source, destination, 22)) return PCAP_APP_SSH;
    if (port_is(source, destination, 20) || port_is(source, destination, 21)) return PCAP_APP_FTP;
    if (port_is(source, destination, 23)) return PCAP_APP_TELNET;
    if (port_is(source, destination, 139) || port_is(source, destination, 445)) return PCAP_APP_SMB;
    if (port_is(source, destination, 1883) || port_is(source, destination, 8883)) return PCAP_APP_MQTT;
    if (port_in_range(source, destination, 6881, 6999)) return PCAP_APP_BITTORRENT;
    if (port_is(source, destination, 25) || port_is(source, destination, 465) ||
        port_is(source, destination, 587)) return PCAP_APP_SMTP;
    if (port_is(source, destination, 143) || port_is(source, destination, 993)) return PCAP_APP_IMAP;
    if (port_is(source, destination, 110) || port_is(source, destination, 995)) return PCAP_APP_POP3;

    *confidence = PCAP_APP_CONFIDENCE_TRANSPORT;
    return details->ip_protocol == 6 ? PCAP_APP_TCP :
           (details->ip_protocol == 17 ? PCAP_APP_UDP : PCAP_APP_UNKNOWN);
}

static int flow_direction(const pcap_flow_entry_t *flow,
                          const pcap_packet_details_t *details)
{
    if (!flow || !details) return -1;
    if (flow->ip_protocol == details->ip_protocol &&
        flow->originator_port == details->source_port &&
        flow->responder_port == details->destination_port &&
        strcmp(flow->originator, details->source) == 0 &&
        strcmp(flow->responder, details->destination) == 0) return 0;
    if (flow->ip_protocol == details->ip_protocol &&
        flow->originator_port == details->destination_port &&
        flow->responder_port == details->source_port &&
        strcmp(flow->originator, details->destination) == 0 &&
        strcmp(flow->responder, details->source) == 0) return 1;
    return -1;
}

static uint16_t find_flow(const pcap_flow_analysis_t *analysis,
                          const pcap_packet_details_t *details, int *direction)
{
    for (uint16_t i = 0; i < analysis->flow_count; i++) {
        int found_direction = flow_direction(&analysis->flows[i], details);
        if (found_direction >= 0) {
            *direction = found_direction;
            return i;
        }
    }
    return PCAP_FLOW_ID_NONE;
}

static uint16_t create_flow(pcap_flow_analysis_t *analysis,
                            const pcap_packet_details_t *details,
                            uint32_t packet_number, uint64_t time_us, int *direction)
{
    if (analysis->flow_count >= PCAP_FLOW_MAX_FLOWS) {
        analysis->flow_limited = true;
        return PCAP_FLOW_ID_NONE;
    }
    uint16_t id = (uint16_t)analysis->flow_count++;
    pcap_flow_entry_t *flow = &analysis->flows[id];
    bool syn_ack = details->ip_protocol == 6 &&
                   (details->tcp_flags & 0x12U) == 0x12U;
    const char *originator = syn_ack ? details->destination : details->source;
    const char *responder = syn_ack ? details->source : details->destination;
    const char *originator_mac = syn_ack ? details->destination_mac : details->source_mac;
    const char *responder_mac = syn_ack ? details->source_mac : details->destination_mac;
    copy_text(flow->originator, sizeof(flow->originator), originator, strlen(originator));
    copy_text(flow->responder, sizeof(flow->responder), responder, strlen(responder));
    copy_text(flow->originator_mac, sizeof(flow->originator_mac), originator_mac,
              strlen(originator_mac));
    copy_text(flow->responder_mac, sizeof(flow->responder_mac), responder_mac,
              strlen(responder_mac));
    flow->originator_port = syn_ack ? details->destination_port : details->source_port;
    flow->responder_port = syn_ack ? details->source_port : details->destination_port;
    flow->ip_protocol = details->ip_protocol;
    flow->first_packet = packet_number;
    flow->last_packet = packet_number;
    flow->first_time_us = time_us;
    flow->last_time_us = time_us;
    *direction = syn_ack ? 1 : 0;
    return id;
}

static int find_device(const pcap_flow_analysis_t *analysis, const char *address)
{
    if (!analysis || !address || !address[0]) return -1;
    for (uint32_t i = 0; i < analysis->device_count; i++) {
        if (strcasecmp(analysis->devices[i].address, address) == 0) return (int)i;
    }
    return -1;
}

static pcap_device_entry_t *observe_device(pcap_flow_analysis_t *analysis,
                                           const char *address, const char *mac,
                                           uint64_t time_us, bool sent,
                                           uint32_t captured_bytes)
{
    if (!analysis || !address || !address[0] || address_is_multicast(address)) return NULL;
    int index = find_device(analysis, address);
    if (index < 0) {
        if (analysis->device_count >= PCAP_FLOW_MAX_DEVICES) {
            analysis->device_limited = true;
            return NULL;
        }
        index = (int)analysis->device_count++;
        pcap_device_entry_t *created = &analysis->devices[index];
        copy_text(created->address, sizeof(created->address), address, strlen(address));
        created->internal = address_is_internal(address);
        created->first_time_us = time_us;
    }
    pcap_device_entry_t *device = &analysis->devices[index];
    if (mac && mac[0] && !mac_is_group(mac)) {
        copy_text(device->mac, sizeof(device->mac), mac, strlen(mac));
    }
    if (device->first_time_us == 0U || time_us < device->first_time_us) {
        device->first_time_us = time_us;
    }
    if (time_us > device->last_time_us) device->last_time_us = time_us;
    if (sent) {
        device->sent_packets++;
        device->sent_bytes += captured_bytes;
    } else {
        device->received_packets++;
        device->received_bytes += captured_bytes;
    }
    return device;
}

static void add_device_service(pcap_flow_analysis_t *analysis, const char *address,
                               uint16_t port, uint8_t ip_protocol, uint8_t application,
                               uint32_t packets, uint64_t bytes)
{
    int index = find_device(analysis, address);
    if (index < 0 || port == 0U) return;
    pcap_device_entry_t *device = &analysis->devices[index];
    for (uint16_t i = 0; i < device->service_count; i++) {
        pcap_device_service_t *service = &device->services[i];
        if (service->port == port && service->ip_protocol == ip_protocol) {
            service->packets += packets;
            service->bytes += bytes;
            if (application > service->application) service->application = application;
            return;
        }
    }
    if (device->service_count >= PCAP_FLOW_DEVICE_SERVICES) {
        device->service_limited = true;
        return;
    }
    pcap_device_service_t *service = &device->services[device->service_count++];
    service->port = port;
    service->ip_protocol = ip_protocol;
    service->application = application;
    service->packets = packets;
    service->bytes = bytes;
}

static pcap_security_alert_t *add_alert(pcap_flow_analysis_t *analysis,
                                        pcap_alert_type_t type,
                                        pcap_health_level_t severity,
                                        pcap_app_confidence_t confidence,
                                        const char *source, const char *target,
                                        uint16_t service_port, uint16_t flow_id,
                                        uint32_t first_packet, uint32_t last_packet,
                                        uint64_t first_time_us, uint64_t last_time_us,
                                        const char *detail)
{
    if (!analysis) return NULL;
    for (uint32_t i = 0; i < analysis->alert_count; i++) {
        pcap_security_alert_t *existing = &analysis->alerts[i];
        if (existing->type == type && existing->service_port == service_port &&
            strcasecmp(existing->source, source ? source : "") == 0 &&
            strcasecmp(existing->target, target ? target : "") == 0) return existing;
    }
    if (analysis->alert_count >= PCAP_FLOW_MAX_ALERTS) {
        analysis->alert_limited = true;
        return NULL;
    }
    pcap_security_alert_t *alert = &analysis->alerts[analysis->alert_count++];
    alert->type = (uint8_t)type;
    alert->severity = (uint8_t)severity;
    alert->confidence = (uint8_t)confidence;
    alert->flow_id = flow_id;
    alert->service_port = service_port;
    alert->first_packet = first_packet;
    alert->last_packet = last_packet;
    alert->first_time_us = first_time_us;
    alert->last_time_us = last_time_us;
    copy_text(alert->source, sizeof(alert->source), source, source ? strlen(source) : 0U);
    copy_text(alert->target, sizeof(alert->target), target, target ? strlen(target) : 0U);
    copy_text(alert->detail, sizeof(alert->detail), detail, detail ? strlen(detail) : 0U);
    if (severity > analysis->health_level) analysis->health_level = (uint8_t)severity;
    return alert;
}

static void alert_evidence(pcap_security_alert_t *alert, uint32_t packet_number)
{
    if (!alert || alert->evidence_count >= PCAP_FLOW_ALERT_EVIDENCE) return;
    for (uint8_t i = 0; i < alert->evidence_count; i++) {
        if (alert->evidence_packets[i] == packet_number) return;
    }
    alert->evidence_packets[alert->evidence_count++] = packet_number;
}

static unsigned bit_count64(uint64_t value)
{
    unsigned count = 0;
    while (value) {
        count += (unsigned)(value & 1ULL);
        value >>= 1U;
    }
    return count;
}

static uint32_t text_hash(const char *text)
{
    uint32_t hash = 2166136261UL;
    if (!text) return hash;
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619UL;
    }
    return hash;
}

static void build_scan_alerts(pcap_flow_analysis_t *analysis)
{
    const uint64_t window_us = 60000000ULL;
    for (uint16_t anchor = 0; anchor < analysis->flow_count; anchor++) {
        const pcap_flow_entry_t *base = &analysis->flows[anchor];
        if (base->ip_protocol != 6U && base->ip_protocol != 17U) continue;
        uint64_t port_bits = 0U;
        uint64_t host_bits = 0U;
        uint32_t last_packet = base->last_packet;
        uint64_t last_time = base->last_time_us;
        for (uint16_t i = anchor; i < analysis->flow_count; i++) {
            const pcap_flow_entry_t *flow = &analysis->flows[i];
            if (flow->first_time_us < base->first_time_us ||
                flow->first_time_us - base->first_time_us > window_us) continue;
            if (strcasecmp(flow->originator, base->originator) != 0) continue;
            if (strcasecmp(flow->responder, base->responder) == 0) {
                uint64_t bit = 1ULL << (flow->responder_port & 63U);
                port_bits |= bit;
            }
            if (flow->responder_port == base->responder_port &&
                flow->ip_protocol == base->ip_protocol) {
                uint64_t bit = 1ULL << (text_hash(flow->responder) & 63U);
                host_bits |= bit;
            }
            if (flow->last_packet > last_packet) last_packet = flow->last_packet;
            if (flow->last_time_us > last_time) last_time = flow->last_time_us;
        }
        uint32_t port_matches = bit_count64(port_bits);
        uint32_t host_matches = bit_count64(host_bits);
        if (port_matches >= 10U) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "%.47s contacted at least %lu ports on %.47s within 60 s",
                     base->originator, (unsigned long)port_matches, base->responder);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_PORT_SCAN, PCAP_HEALTH_SUSPICIOUS,
                PCAP_APP_CONFIDENCE_CONFIRMED, base->originator, base->responder,
                0U, PCAP_FLOW_ID_NONE, base->first_packet, last_packet,
                base->first_time_us, last_time, detail);
            for (uint16_t i = anchor; alert && i < analysis->flow_count; i++) {
                const pcap_flow_entry_t *flow = &analysis->flows[i];
                if (strcasecmp(flow->originator, base->originator) == 0 &&
                    strcasecmp(flow->responder, base->responder) == 0 &&
                    flow->first_time_us >= base->first_time_us &&
                    flow->first_time_us - base->first_time_us <= window_us) {
                    alert_evidence(alert, flow->first_packet);
                }
            }
        }
        if (host_matches >= 8U) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "%.47s contacted at least %lu hosts on %s/%u within 60 s",
                     base->originator, (unsigned long)host_matches,
                     pcap_flow_transport_name(base->ip_protocol), base->responder_port);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_HOST_SWEEP, PCAP_HEALTH_SUSPICIOUS,
                PCAP_APP_CONFIDENCE_CONFIRMED, base->originator, "multiple hosts",
                base->responder_port, PCAP_FLOW_ID_NONE, base->first_packet, last_packet,
                base->first_time_us, last_time, detail);
            for (uint16_t i = anchor; alert && i < analysis->flow_count; i++) {
                const pcap_flow_entry_t *flow = &analysis->flows[i];
                if (strcasecmp(flow->originator, base->originator) == 0 &&
                    flow->responder_port == base->responder_port &&
                    flow->first_time_us >= base->first_time_us &&
                    flow->first_time_us - base->first_time_us <= window_us) {
                    alert_evidence(alert, flow->first_packet);
                }
            }
        }
    }
}

static void build_flow_alerts(pcap_flow_analysis_t *analysis)
{
    for (uint16_t i = 0; i < analysis->flow_count; i++) {
        const pcap_flow_entry_t *flow = &analysis->flows[i];
        if (flow->originator_payload_bytes >= 65536ULL &&
            flow->originator_payload_bytes > (flow->responder_payload_bytes + 1ULL) * 10ULL &&
            address_is_internal(flow->originator) && !address_is_internal(flow->responder)) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "%.35s sent %llu payload B and received %llu B from external %.35s",
                     flow->originator,
                     (unsigned long long)flow->originator_payload_bytes,
                     (unsigned long long)flow->responder_payload_bytes, flow->responder);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_EXFIL_CANDIDATE, PCAP_HEALTH_WATCH,
                PCAP_APP_CONFIDENCE_LIKELY, flow->originator, flow->responder,
                flow->responder_port, i, flow->first_packet, flow->last_packet,
                flow->first_time_us, flow->last_time_us, detail);
            alert_evidence(alert, flow->first_packet);
        }
        pcap_application_t app = (pcap_application_t)flow->app_protocol;
        if (app == PCAP_APP_TELNET || app == PCAP_APP_FTP || app == PCAP_APP_HTTP ||
            app == PCAP_APP_SMTP || app == PCAP_APP_POP3 || app == PCAP_APP_IMAP) {
            char detail[160];
            snprintf(detail, sizeof(detail), "%.23s observed between %.35s and %.35s on port %u",
                     pcap_flow_application_name(app), flow->originator, flow->responder,
                     flow->responder_port);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_CLEARTEXT_SERVICE, PCAP_HEALTH_WATCH,
                (pcap_app_confidence_t)flow->app_confidence, flow->originator,
                flow->responder, flow->responder_port, i, flow->first_packet,
                flow->last_packet, flow->first_time_us, flow->last_time_us, detail);
            alert_evidence(alert, flow->first_packet);
        }
        if (app == PCAP_APP_TLS && flow->tls_version > 0U &&
            flow->tls_version <= 0x0302U) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "TLS legacy version 0x%04X observed for %.47s:%u%s%.47s",
                     flow->tls_version, flow->responder, flow->responder_port,
                     flow->server_name[0] ? " SNI " : "",
                     flow->server_name[0] ? flow->server_name : "");
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_WEAK_TLS, PCAP_HEALTH_WATCH,
                PCAP_APP_CONFIDENCE_CONFIRMED, flow->originator, flow->responder,
                flow->responder_port, i, flow->first_packet, flow->last_packet,
                flow->first_time_us, flow->last_time_us, detail);
            alert_evidence(alert, flow->first_packet);
        }
        uint32_t tcp_packets = flow->originator_packets + flow->responder_packets;
        uint32_t retransmissions = flow->originator_retransmissions +
                                   flow->responder_retransmissions;
        if (flow->ip_protocol == 6U && tcp_packets >= 20U &&
            ((retransmissions * 100U / tcp_packets) >= 10U ||
             flow->zero_window_packets >= 3U)) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "TCP %.35s -> %.35s:%u has %lu retransmission indicators and %lu zero-window packets",
                     flow->originator, flow->responder, flow->responder_port,
                     (unsigned long)retransmissions,
                     (unsigned long)flow->zero_window_packets);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_TCP_QUALITY, PCAP_HEALTH_WATCH,
                PCAP_APP_CONFIDENCE_LIKELY, flow->originator, flow->responder,
                flow->responder_port, i, flow->first_packet, flow->last_packet,
                flow->first_time_us, flow->last_time_us, detail);
            alert_evidence(alert, flow->first_packet);
        }
    }

    for (uint16_t anchor = 0; anchor < analysis->flow_count; anchor++) {
        const pcap_flow_entry_t *base = &analysis->flows[anchor];
        uint32_t count = 1U;
        uint64_t previous = base->first_time_us;
        uint64_t interval_sum = 0U;
        uint64_t interval_min = UINT64_MAX;
        uint64_t interval_max = 0U;
        uint32_t last_packet = base->last_packet;
        for (uint16_t i = anchor + 1U; i < analysis->flow_count; i++) {
            const pcap_flow_entry_t *flow = &analysis->flows[i];
            if (flow->ip_protocol != base->ip_protocol ||
                flow->responder_port != base->responder_port ||
                strcasecmp(flow->originator, base->originator) != 0 ||
                strcasecmp(flow->responder, base->responder) != 0) continue;
            uint64_t interval = flow->first_time_us - previous;
            previous = flow->first_time_us;
            interval_sum += interval;
            if (interval < interval_min) interval_min = interval;
            if (interval > interval_max) interval_max = interval;
            last_packet = flow->last_packet;
            count++;
        }
        if (count >= 5U) {
            uint64_t mean = interval_sum / (count - 1U);
            if (mean >= 2000000ULL && interval_max - interval_min <= mean / 3U) {
                char detail[160];
                snprintf(detail, sizeof(detail),
                         "%lu regular connections from %.35s to %.35s:%u, mean interval %.1f s",
                         (unsigned long)count, base->originator, base->responder,
                         base->responder_port, (double)mean / 1000000.0);
                pcap_security_alert_t *alert = add_alert(
                    analysis, PCAP_ALERT_BEACONING, PCAP_HEALTH_WATCH,
                    PCAP_APP_CONFIDENCE_LIKELY, base->originator, base->responder,
                    base->responder_port, PCAP_FLOW_ID_NONE, base->first_packet, last_packet,
                    base->first_time_us, previous, detail);
                alert_evidence(alert, base->first_packet);
            }
        }
    }
}

static void build_worm_alerts(pcap_flow_analysis_t *analysis)
{
    uint32_t original_count = analysis->alert_count;
    for (uint32_t first = 0; first < original_count; first++) {
        const pcap_security_alert_t *a = &analysis->alerts[first];
        if (a->type != PCAP_ALERT_HOST_SWEEP) continue;
        for (uint32_t second = 0; second < original_count; second++) {
            const pcap_security_alert_t *b = &analysis->alerts[second];
            if (b->type != PCAP_ALERT_HOST_SWEEP || b->first_time_us <= a->first_time_us ||
                b->first_time_us - a->first_time_us > 120000000ULL ||
                b->service_port != a->service_port) continue;
            bool first_touched_second = false;
            for (uint16_t f = 0; f < analysis->flow_count; f++) {
                const pcap_flow_entry_t *flow = &analysis->flows[f];
                if (strcasecmp(flow->originator, a->source) == 0 &&
                    strcasecmp(flow->responder, b->source) == 0 &&
                    flow->first_time_us >= a->first_time_us &&
                    flow->first_time_us <= b->first_time_us) {
                    first_touched_second = true;
                    break;
                }
            }
            if (!first_touched_second) continue;
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "%.47s swept port %u, then contacted %.47s which repeated the sweep",
                     a->source, a->service_port, b->source);
            pcap_security_alert_t *alert = add_alert(
                analysis, PCAP_ALERT_WORM_LIKE_SPREAD, PCAP_HEALTH_CRITICAL,
                PCAP_APP_CONFIDENCE_LIKELY, a->source, b->source, a->service_port,
                PCAP_FLOW_ID_NONE, a->first_packet, b->last_packet,
                a->first_time_us, b->last_time_us, detail);
            if (alert) {
                for (uint8_t e = 0; e < a->evidence_count; e++) {
                    alert_evidence(alert, a->evidence_packets[e]);
                }
                for (uint8_t e = 0; e < b->evidence_count; e++) {
                    alert_evidence(alert, b->evidence_packets[e]);
                }
            }
        }
    }
}

pcap_reader_status_t pcap_flow_analysis_build(
    pcap_reader_t *reader, const pcap_capture_info_t *capture_info,
    const pcap_packet_index_t *packet_index, size_t packet_count,
    pcap_flow_analysis_t *analysis, const volatile bool *cancel_requested)
{
    if (!reader || !capture_info || !packet_index || !analysis ||
        packet_count > PCAP_FLOW_MAX_PACKET_MAP) return PCAP_READER_INVALID_ARG;
    memset(analysis, 0, sizeof(*analysis));
    analysis->health_level = PCAP_HEALTH_INSUFFICIENT;
    for (size_t i = 0; i < PCAP_FLOW_MAX_PACKET_MAP; i++) {
        analysis->packet_flow_id[i] = PCAP_FLOW_ID_NONE;
        analysis->packet_direction[i] = 2U;
    }
    uint8_t raw[PCAP_FLOW_CLASSIFY_BYTES];
    for (uint32_t i = 0; i < packet_count; i++) {
        if (cancel_requested && *cancel_requested) return PCAP_READER_CANCELLED;
        pcap_packet_details_t details;
        pcap_reader_status_t detail_status =
            pcap_reader_describe_packet(reader, &packet_index[i], &details);
        if (detail_status != PCAP_READER_OK && detail_status != PCAP_READER_LIMIT_REACHED) {
            return detail_status;
        }
        size_t raw_length = 0;
        pcap_reader_status_t raw_status =
            pcap_reader_read_packet(reader, &packet_index[i], raw, sizeof(raw), &raw_length);
        if (raw_status != PCAP_READER_OK && raw_status != PCAP_READER_LIMIT_REACHED) {
            return raw_status;
        }
        const uint8_t *payload = NULL;
        size_t payload_length = 0;
        if (details.payload_offset > 0U && details.payload_offset < raw_length) {
            payload = raw + details.payload_offset;
            payload_length = raw_length - details.payload_offset;
        }
        pcap_app_confidence_t confidence = PCAP_APP_CONFIDENCE_NONE;
        pcap_application_t app = classify_application(&details, payload, payload_length,
                                                      &confidence);
        analysis->packet_application[i] = (uint8_t)app;
        analysis->packet_confidence[i] = (uint8_t)confidence;
        uint64_t time_us = packet_time_us(&packet_index[i],
                                          capture_info->timestamp_resolution);

        if ((details.flags & (PCAP_PACKET_FLAG_IPV4 | PCAP_PACKET_FLAG_IPV6)) != 0U) {
            observe_device(analysis, details.source, details.source_mac, time_us, true,
                           packet_index[i].captured_length);
            observe_device(analysis, details.destination, details.destination_mac, time_us,
                           false, packet_index[i].captured_length);
            if (address_is_multicast(details.destination) ||
                mac_is_group(details.destination_mac)) analysis->broadcast_packets++;
        }
        if ((details.flags & PCAP_PACKET_FLAG_ARP) != 0U && details.arp_sender_ip[0]) {
            int existing_index = find_device(analysis, details.arp_sender_ip);
            if (existing_index >= 0 && analysis->devices[existing_index].mac[0] &&
                details.arp_sender_mac[0] &&
                strcasecmp(analysis->devices[existing_index].mac,
                           details.arp_sender_mac) != 0) {
                char detail[160];
                snprintf(detail, sizeof(detail), "%.63s changed MAC from %.17s to %.17s",
                         details.arp_sender_ip, analysis->devices[existing_index].mac,
                         details.arp_sender_mac);
                pcap_security_alert_t *alert = add_alert(
                    analysis, PCAP_ALERT_ARP_CONFLICT, PCAP_HEALTH_CRITICAL,
                    PCAP_APP_CONFIDENCE_CONFIRMED, details.arp_sender_ip,
                    details.arp_target_ip, 0U, PCAP_FLOW_ID_NONE, i, i,
                    time_us, time_us, detail);
                alert_evidence(alert, i);
            }
            observe_device(analysis, details.arp_sender_ip, details.arp_sender_mac,
                           time_us, true, packet_index[i].captured_length);
            if (details.arp_target_ip[0]) {
                observe_device(analysis, details.arp_target_ip, details.arp_target_mac,
                               time_us, false, packet_index[i].captured_length);
            }
            if (mac_is_group(details.destination_mac)) analysis->broadcast_packets++;
        }
        if (details.dns_valid) {
            analysis->dns_packets++;
            if (details.dns_response && details.dns_rcode != 0U) {
                analysis->dns_error_packets++;
            }
            if (details.dns_response && details.dns_query[0] &&
                details.dns_first_answer[0]) {
                int answer_device = find_device(analysis, details.dns_first_answer);
                if (answer_device >= 0 && analysis->devices[answer_device].hostname[0] == '\0') {
                    copy_text(analysis->devices[answer_device].hostname,
                              sizeof(analysis->devices[answer_device].hostname),
                              details.dns_query, strlen(details.dns_query));
                }
            }
            if (!details.dns_response && details.dns_query[0]) {
                size_t query_length = strlen(details.dns_query);
                unsigned labels = 1U;
                for (size_t character = 0; character < query_length; character++) {
                    if (details.dns_query[character] == '.') labels++;
                }
                if (query_length >= 50U || labels >= 5U) {
                    analysis->dns_suspicious_names++;
                }
            }
        }

        if (!details.malformed &&
            (details.ip_protocol == 6 || details.ip_protocol == 17) &&
            details.source[0] && details.destination[0] &&
            details.source_port && details.destination_port) {
            int direction = -1;
            uint16_t flow_id = find_flow(analysis, &details, &direction);
            if (flow_id == PCAP_FLOW_ID_NONE) {
                flow_id = create_flow(analysis, &details, i, time_us, &direction);
            }
            if (flow_id != PCAP_FLOW_ID_NONE) {
                pcap_flow_entry_t *flow = &analysis->flows[flow_id];
                flow->last_packet = i;
                flow->last_time_us = time_us;
                if (confidence > flow->app_confidence) {
                    flow->app_protocol = (uint8_t)app;
                    flow->app_confidence = (uint8_t)confidence;
                }
                update_flow_metadata(flow, app, payload, payload_length);
                uint64_t payload_bytes = details.payload_offset < packet_index[i].captured_length
                    ? packet_index[i].captured_length - details.payload_offset : 0;
                if (details.ip_protocol == 6U) {
                    if (direction == 0 && (details.tcp_flags & 0x12U) == 0x02U &&
                        flow->syn_time_us == 0U) {
                        flow->syn_time_us = time_us;
                    } else if (direction == 1 && (details.tcp_flags & 0x12U) == 0x12U &&
                               flow->syn_time_us > 0U && time_us >= flow->syn_time_us &&
                               flow->handshake_rtt_us == 0U) {
                        flow->handshake_rtt_us = time_us - flow->syn_time_us;
                    }
                    if (details.tcp_window == 0U && (details.tcp_flags & 0x04U) == 0U) {
                        flow->zero_window_packets++;
                    }
                    uint32_t advance = (uint32_t)payload_bytes;
                    if (details.tcp_flags & 0x02U) advance++;
                    if (details.tcp_flags & 0x01U) advance++;
                    if (advance > 0U) {
                        uint32_t end_sequence = details.tcp_sequence + advance;
                        uint32_t *next_sequence = direction == 0
                            ? &flow->originator_next_sequence
                            : &flow->responder_next_sequence;
                        bool *sequence_seen = direction == 0
                            ? &flow->originator_sequence_seen
                            : &flow->responder_sequence_seen;
                        uint32_t *retransmissions = direction == 0
                            ? &flow->originator_retransmissions
                            : &flow->responder_retransmissions;
                        if (*sequence_seen && payload_bytes > 0U &&
                            details.tcp_sequence < *next_sequence &&
                            *next_sequence - details.tcp_sequence < 0x80000000UL) {
                            (*retransmissions)++;
                        }
                        if (!*sequence_seen || end_sequence > *next_sequence) {
                            *next_sequence = end_sequence;
                        }
                        *sequence_seen = true;
                    }
                }
                if (direction == 0) {
                    flow->originator_packets++;
                    flow->originator_bytes += packet_index[i].captured_length;
                    flow->originator_payload_bytes += payload_bytes;
                    flow->originator_tcp_flags |= details.tcp_flags;
                } else {
                    flow->responder_packets++;
                    flow->responder_bytes += packet_index[i].captured_length;
                    flow->responder_payload_bytes += payload_bytes;
                    flow->responder_tcp_flags |= details.tcp_flags;
                }
                analysis->packet_flow_id[i] = flow_id;
                analysis->packet_direction[i] = (uint8_t)direction;
            } else {
                analysis->overflow_packets++;
            }
        }
        analysis->analyzed_packets++;
    }

    for (uint32_t i = 0; i < analysis->analyzed_packets; i++) {
        uint16_t flow_id = analysis->packet_flow_id[i];
        if (flow_id != PCAP_FLOW_ID_NONE && flow_id < analysis->flow_count) {
            analysis->packet_application[i] = analysis->flows[flow_id].app_protocol;
            analysis->packet_confidence[i] = analysis->flows[flow_id].app_confidence;
        }
        uint8_t app = analysis->packet_application[i];
        if (app >= PCAP_APP_COUNT) app = PCAP_APP_UNKNOWN;
        analysis->applications[app].packets++;
        analysis->applications[app].bytes += packet_index[i].captured_length;
        if (analysis->packet_confidence[i] == PCAP_APP_CONFIDENCE_CONFIRMED) {
            analysis->applications[app].confirmed_packets++;
        } else if (analysis->packet_confidence[i] == PCAP_APP_CONFIDENCE_LIKELY) {
            analysis->applications[app].likely_packets++;
        }
    }
    for (uint32_t i = 0; i < analysis->flow_count; i++) {
        pcap_flow_entry_t *flow = &analysis->flows[i];
        uint8_t app = flow->app_protocol < PCAP_APP_COUNT
                          ? flow->app_protocol : PCAP_APP_UNKNOWN;
        analysis->applications[app].flows++;
        if (flow->app_confidence == PCAP_APP_CONFIDENCE_CONFIRMED) {
            analysis->applications[app].confirmed_flows++;
        } else if (flow->app_confidence == PCAP_APP_CONFIDENCE_LIKELY) {
            analysis->applications[app].likely_flows++;
        }
        add_device_service(analysis, flow->responder, flow->responder_port,
                           flow->ip_protocol, flow->app_protocol,
                           flow->originator_packets + flow->responder_packets,
                           flow->originator_bytes + flow->responder_bytes);
    }

    build_scan_alerts(analysis);
    build_flow_alerts(analysis);
    if ((analysis->dns_packets >= 20U &&
         analysis->dns_error_packets * 100U / analysis->dns_packets >= 30U) ||
        analysis->dns_suspicious_names >= 10U) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "DNS errors %lu/%lu; long or multi-label query indicators %lu",
                 (unsigned long)analysis->dns_error_packets,
                 (unsigned long)analysis->dns_packets,
                 (unsigned long)analysis->dns_suspicious_names);
        add_alert(analysis, PCAP_ALERT_DNS_ANOMALY, PCAP_HEALTH_SUSPICIOUS,
                  PCAP_APP_CONFIDENCE_CONFIRMED, "DNS clients", "DNS servers", 53U,
                  PCAP_FLOW_ID_NONE, 0U, analysis->analyzed_packets - 1U, 0U, 0U, detail);
    }
    if (analysis->analyzed_packets >= 100U &&
        analysis->broadcast_packets * 100U / analysis->analyzed_packets >= 40U) {
        char detail[160];
        snprintf(detail, sizeof(detail), "%lu of %lu analyzed packets were multicast/broadcast",
                 (unsigned long)analysis->broadcast_packets,
                 (unsigned long)analysis->analyzed_packets);
        add_alert(analysis, PCAP_ALERT_EXCESSIVE_BROADCAST, PCAP_HEALTH_WATCH,
                  PCAP_APP_CONFIDENCE_CONFIRMED, "local segment", "group traffic", 0U,
                  PCAP_FLOW_ID_NONE, 0U, analysis->analyzed_packets - 1U, 0U, 0U, detail);
    }
    build_worm_alerts(analysis);
    if (analysis->analyzed_packets >= 50U) {
        if (analysis->health_level == PCAP_HEALTH_INSUFFICIENT) {
            analysis->health_level = PCAP_HEALTH_HEALTHY;
        }
    }
    return PCAP_READER_OK;
}

const char *pcap_flow_application_name(pcap_application_t application)
{
    static const char *names[PCAP_APP_COUNT] = {
        "Other/Unknown", "TCP", "UDP", "ARP", "ICMP", "ICMPv6", "EAPOL",
        "802.11 MGMT", "DNS", "mDNS", "DHCP", "HTTP",
        "HTTPS/TLS", "QUIC", "SSDP/UPnP", "NTP", "SSH", "FTP", "Telnet",
        "SMB", "MQTT", "BitTorrent", "BitTorrent DHT", "LLMNR", "NBNS",
        "SMTP", "IMAP", "POP3"
    };
    return application < PCAP_APP_COUNT ? names[application] : "Unknown";
}

const char *pcap_flow_confidence_name(pcap_app_confidence_t confidence)
{
    switch (confidence) {
        case PCAP_APP_CONFIDENCE_CONFIRMED: return "CONFIRMED";
        case PCAP_APP_CONFIDENCE_LIKELY: return "LIKELY";
        case PCAP_APP_CONFIDENCE_TRANSPORT: return "TRANSPORT";
        default: return "UNKNOWN";
    }
}

const char *pcap_flow_transport_name(uint8_t ip_protocol)
{
    return ip_protocol == 6 ? "TCP" : (ip_protocol == 17 ? "UDP" : "IP");
}

const char *pcap_flow_health_name(pcap_health_level_t level)
{
    switch (level) {
        case PCAP_HEALTH_HEALTHY: return "HEALTHY";
        case PCAP_HEALTH_WATCH: return "WATCH";
        case PCAP_HEALTH_SUSPICIOUS: return "SUSPICIOUS";
        case PCAP_HEALTH_CRITICAL: return "CRITICAL";
        default: return "INSUFFICIENT DATA";
    }
}

const char *pcap_flow_alert_name(pcap_alert_type_t type)
{
    switch (type) {
        case PCAP_ALERT_PORT_SCAN: return "PORT SCAN";
        case PCAP_ALERT_HOST_SWEEP: return "HOST SWEEP";
        case PCAP_ALERT_ARP_CONFLICT: return "ARP CONFLICT";
        case PCAP_ALERT_DNS_ANOMALY: return "DNS ANOMALY";
        case PCAP_ALERT_BEACONING: return "BEACONING";
        case PCAP_ALERT_EXFIL_CANDIDATE: return "EXFIL CANDIDATE";
        case PCAP_ALERT_CLEARTEXT_SERVICE: return "CLEARTEXT SERVICE";
        case PCAP_ALERT_WORM_LIKE_SPREAD: return "WORM-LIKE SPREAD";
        case PCAP_ALERT_EXCESSIVE_BROADCAST: return "EXCESSIVE BROADCAST";
        case PCAP_ALERT_WEAK_TLS: return "WEAK TLS";
        case PCAP_ALERT_TCP_QUALITY: return "TCP QUALITY";
        default: return "UNKNOWN ALERT";
    }
}

void pcap_flow_filter_clear(pcap_flow_filter_t *filter)
{
    if (filter) memset(filter, 0, sizeof(*filter));
}

bool pcap_flow_filter_matches(
    const pcap_flow_filter_t *filter, const pcap_flow_analysis_t *analysis,
    uint32_t packet_number, const pcap_packet_index_t *packet,
    const pcap_packet_details_t *details, pcap_timestamp_resolution_t resolution)
{
    if (!filter || !filter->enabled) return true;
    if (!analysis || !packet || packet_number >= analysis->analyzed_packets) {
        return false;
    }
    uint16_t packet_flow = analysis->packet_flow_id[packet_number];
    const pcap_flow_entry_t *flow = packet_flow != PCAP_FLOW_ID_NONE &&
                                    packet_flow < analysis->flow_count
                                        ? &analysis->flows[packet_flow] : NULL;
    uint8_t direction = analysis->packet_direction[packet_number];
    const char *source_address = details ? details->source :
        (flow ? (direction == 0 ? flow->originator : flow->responder) : NULL);
    const char *destination_address = details ? details->destination :
        (flow ? (direction == 0 ? flow->responder : flow->originator) : NULL);
    const char *source_mac = details ? details->source_mac :
        (flow ? (direction == 0 ? flow->originator_mac : flow->responder_mac) : NULL);
    const char *destination_mac = details ? details->destination_mac :
        (flow ? (direction == 0 ? flow->responder_mac : flow->originator_mac) : NULL);
    uint16_t source_port = details ? details->source_port :
        (flow ? (direction == 0 ? flow->originator_port : flow->responder_port) : 0U);
    uint16_t destination_port = details ? details->destination_port :
        (flow ? (direction == 0 ? flow->responder_port : flow->originator_port) : 0U);
    bool needs_endpoints = filter->has_any_address || filter->has_source_address ||
                           filter->has_destination_address || filter->has_any_mac ||
                           filter->has_port;
    if (needs_endpoints && !details && !flow) return false;
    if (filter->has_any_address &&
        (!source_address || !destination_address ||
         (strcasecmp(source_address, filter->any_address) != 0 &&
          strcasecmp(destination_address, filter->any_address) != 0))) return false;
    if (filter->has_source_address &&
        (!source_address || strcasecmp(source_address, filter->source_address) != 0)) return false;
    if (filter->has_destination_address &&
        (!destination_address ||
         strcasecmp(destination_address, filter->destination_address) != 0)) return false;
    if (filter->has_any_mac &&
        (!source_mac || !destination_mac ||
         (strcasecmp(source_mac, filter->any_mac) != 0 &&
          strcasecmp(destination_mac, filter->any_mac) != 0))) return false;
    if (filter->has_port && source_port != filter->port &&
        destination_port != filter->port) return false;
    if (filter->has_flow && analysis->packet_flow_id[packet_number] != filter->flow_id) {
        return false;
    }
    if (filter->has_application &&
        analysis->packet_application[packet_number] != filter->application) return false;
    if (filter->has_time_window) {
        uint64_t time_us = packet_time_us(packet, resolution);
        if (time_us < filter->time_start_us || time_us > filter->time_end_us) return false;
    }
    return true;
}

void pcap_flow_filter_describe(const pcap_flow_filter_t *filter,
                               char *output, size_t output_size)
{
    if (!output || output_size == 0) return;
    if (!filter || !filter->enabled) {
        snprintf(output, output_size, "No quick filter");
    } else if (filter->has_flow) {
        snprintf(output, output_size, "FLOW #%u", (unsigned)filter->flow_id + 1U);
    } else if (filter->has_application) {
        snprintf(output, output_size, "APP %s",
                 pcap_flow_application_name((pcap_application_t)filter->application));
    } else if (filter->has_any_address) {
        snprintf(output, output_size, "HOST %s", filter->any_address);
    } else if (filter->has_any_mac) {
        snprintf(output, output_size, "MAC %s", filter->any_mac);
    } else if (filter->has_port) {
        snprintf(output, output_size, "PORT %u", filter->port);
    } else if (filter->has_time_window) {
        double duration = filter->time_end_us >= filter->time_start_us
                              ? (double)(filter->time_end_us - filter->time_start_us) /
                                    1000000.0
                              : 0.0;
        snprintf(output, output_size, "TIME WINDOW %.3f s", duration);
    } else {
        snprintf(output, output_size, "Custom filter");
    }
}

static bool stream_append(char *output, size_t output_size, size_t *position,
                          pcap_flow_stream_result_t *result, const char *format, ...)
{
    if (*position >= output_size) {
        result->output_truncated = true;
        return false;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *position, output_size - *position, format, args);
    va_end(args);
    if (written < 0) return false;
    if ((size_t)written >= output_size - *position) {
        *position = output_size - 1U;
        result->output_truncated = true;
        return false;
    }
    *position += (size_t)written;
    return true;
}

static int segment_compare(const void *left, const void *right)
{
    const pcap_stream_segment_t *a = left;
    const pcap_stream_segment_t *b = right;
    if (a->direction != b->direction) return (int)a->direction - (int)b->direction;
    if (a->sequence < b->sequence) return -1;
    if (a->sequence > b->sequence) return 1;
    return a->packet_number < b->packet_number ? -1 :
           (a->packet_number > b->packet_number ? 1 : 0);
}

pcap_reader_status_t pcap_flow_build_stream_ex(
    pcap_reader_t *reader, const pcap_packet_index_t *packet_index,
    size_t packet_count, const pcap_flow_analysis_t *analysis, uint16_t flow_id,
    pcap_flow_stream_mode_t mode, char *output, size_t output_size,
    pcap_flow_stream_result_t *result)
{
    if (!reader || !packet_index || !analysis || flow_id >= analysis->flow_count ||
        mode > PCAP_FLOW_STREAM_HEX || !output || output_size < 2U || !result) {
        return PCAP_READER_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    output[0] = '\0';
    pcap_stream_segment_t *segments = calloc(PCAP_FLOW_STREAM_SEGMENTS, sizeof(*segments));
    uint8_t *raw = malloc(PCAP_FLOW_STREAM_READ);
    if (!segments || !raw) {
        free(segments);
        free(raw);
        return PCAP_READER_NO_MEMORY;
    }
    uint32_t segment_count = 0;
    for (uint32_t i = 0; i < packet_count && i < analysis->analyzed_packets; i++) {
        if (analysis->packet_flow_id[i] != flow_id) continue;
        result->matching_packets++;
        pcap_packet_details_t details;
        pcap_reader_status_t status = pcap_reader_describe_packet(reader, &packet_index[i],
                                                                  &details);
        if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) continue;
        uint32_t payload_length = details.payload_offset < packet_index[i].captured_length
            ? packet_index[i].captured_length - details.payload_offset : 0;
        if (payload_length == 0) continue;
        result->payload_packets++;
        result->payload_bytes += payload_length;
        if (segment_count >= PCAP_FLOW_STREAM_SEGMENTS) {
            result->segment_limited = true;
            continue;
        }
        segments[segment_count++] = (pcap_stream_segment_t) {
            .packet_number = i,
            .sequence = details.tcp_sequence,
            .payload_offset = details.payload_offset,
            .payload_length = payload_length,
            .direction = analysis->packet_direction[i],
        };
    }
    result->segments_used = segment_count;
    const pcap_flow_entry_t *flow = &analysis->flows[flow_id];
    size_t position = 0;
    stream_append(output, output_size, &position, result,
                  "Flow #%u | %s | %s (%s) | %s\n"
                  "%s:%u <-> %s:%u\n"
                  "%lu packet(s), %lu payload packet(s), %llu payload bytes\n\n",
                  (unsigned)flow_id + 1U, pcap_flow_transport_name(flow->ip_protocol),
                  pcap_flow_application_name((pcap_application_t)flow->app_protocol),
                  pcap_flow_confidence_name((pcap_app_confidence_t)flow->app_confidence),
                  mode == PCAP_FLOW_STREAM_HEX ? "HEX" : "ASCII",
                  flow->originator, flow->originator_port,
                  flow->responder, flow->responder_port,
                  (unsigned long)result->matching_packets,
                  (unsigned long)result->payload_packets,
                  (unsigned long long)result->payload_bytes);

    if (flow->ip_protocol == 6) qsort(segments, segment_count, sizeof(*segments), segment_compare);
    for (uint8_t direction = 0; direction < 2 && !result->output_truncated; direction++) {
        stream_append(output, output_size, &position, result,
                      direction == 0 ? "=== ORIGINATOR -> RESPONDER ===\n"
                                     : "\n=== RESPONDER -> ORIGINATOR ===\n");
        bool have_expected = false;
        uint32_t expected = 0;
        for (uint32_t i = 0; i < segment_count && !result->output_truncated; i++) {
            pcap_stream_segment_t *segment = &segments[i];
            if (segment->direction != direction) continue;
            uint32_t skip = 0;
            if (flow->ip_protocol == 6 && have_expected) {
                if (segment->sequence > expected) {
                    uint32_t gap = segment->sequence - expected;
                    result->gap_bytes += gap;
                    result->stream_incomplete = true;
                    stream_append(output, output_size, &position, result,
                                  "\n[GAP: %lu byte(s)]\n", (unsigned long)gap);
                } else if (segment->sequence < expected) {
                    uint32_t overlap = expected - segment->sequence;
                    result->retransmissions++;
                    if (overlap >= segment->payload_length) continue;
                    skip = overlap;
                }
            }
            size_t bytes_read = 0;
            pcap_reader_status_t status = pcap_reader_read_packet(
                reader, &packet_index[segment->packet_number], raw,
                PCAP_FLOW_STREAM_READ, &bytes_read);
            if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) continue;
            size_t payload_offset = segment->payload_offset + skip;
            if (payload_offset >= bytes_read) {
                result->stream_incomplete = true;
                continue;
            }
            size_t available = bytes_read - payload_offset;
            size_t wanted = segment->payload_length - skip;
            if (available > wanted) available = wanted;
            stream_append(output, output_size, &position, result,
                          "\n[#%lu +%lu B] ",
                          (unsigned long)segment->packet_number + 1U,
                          (unsigned long)available);
            for (size_t byte = 0; byte < available && !result->output_truncated; byte++) {
                unsigned char c = raw[payload_offset + byte];
                if (mode == PCAP_FLOW_STREAM_HEX) {
                    stream_append(output, output_size, &position, result, "%02X%s", c,
                                  ((byte + 1U) % 16U) == 0U ? "\n" : " ");
                } else {
                    char shown = (c == '\r' || c == '\n' || c == '\t') ? (char)c :
                                 (isprint(c) ? (char)c : '.');
                    stream_append(output, output_size, &position, result, "%c", shown);
                }
            }
            if (flow->ip_protocol == 6) {
                expected = segment->sequence + segment->payload_length;
                have_expected = true;
            }
            if (available < wanted) result->stream_incomplete = true;
        }
        stream_append(output, output_size, &position, result, "\n");
    }
    if (result->segment_limited || result->output_truncated || result->stream_incomplete) {
        stream_append(output, output_size, &position, result,
                      "\nLIMITED: segments=%s output=%s stream=%s gaps=%llu retrans=%lu\n",
                      result->segment_limited ? "YES" : "NO",
                      result->output_truncated ? "TRUNCATED" : "OK",
                      result->stream_incomplete ? "INCOMPLETE" : "COMPLETE",
                      (unsigned long long)result->gap_bytes,
                      (unsigned long)result->retransmissions);
    }
    free(raw);
    free(segments);
    return PCAP_READER_OK;
}

pcap_reader_status_t pcap_flow_build_stream(
    pcap_reader_t *reader, const pcap_packet_index_t *packet_index,
    size_t packet_count, const pcap_flow_analysis_t *analysis, uint16_t flow_id,
    char *output, size_t output_size, pcap_flow_stream_result_t *result)
{
    return pcap_flow_build_stream_ex(reader, packet_index, packet_count, analysis,
                                     flow_id, PCAP_FLOW_STREAM_ASCII,
                                     output, output_size, result);
}
