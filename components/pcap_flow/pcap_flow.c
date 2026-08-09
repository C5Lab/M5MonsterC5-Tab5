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
    snprintf(flow->originator, sizeof(flow->originator), "%s", originator);
    snprintf(flow->responder, sizeof(flow->responder), "%s", responder);
    snprintf(flow->originator_mac, sizeof(flow->originator_mac), "%s", originator_mac);
    snprintf(flow->responder_mac, sizeof(flow->responder_mac), "%s", responder_mac);
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

pcap_reader_status_t pcap_flow_analysis_build(
    pcap_reader_t *reader, const pcap_capture_info_t *capture_info,
    const pcap_packet_index_t *packet_index, size_t packet_count,
    pcap_flow_analysis_t *analysis, const volatile bool *cancel_requested)
{
    if (!reader || !capture_info || !packet_index || !analysis ||
        packet_count > PCAP_FLOW_MAX_PACKET_MAP) return PCAP_READER_INVALID_ARG;
    memset(analysis, 0, sizeof(*analysis));
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

        if (!details.malformed &&
            (details.ip_protocol == 6 || details.ip_protocol == 17) &&
            details.source[0] && details.destination[0] &&
            details.source_port && details.destination_port) {
            int direction = -1;
            uint16_t flow_id = find_flow(analysis, &details, &direction);
            uint64_t time_us = packet_time_us(&packet_index[i],
                                              capture_info->timestamp_resolution);
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
                uint64_t payload_bytes = details.payload_offset < packet_index[i].captured_length
                    ? packet_index[i].captured_length - details.payload_offset : 0;
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

pcap_reader_status_t pcap_flow_build_stream(
    pcap_reader_t *reader, const pcap_packet_index_t *packet_index,
    size_t packet_count, const pcap_flow_analysis_t *analysis, uint16_t flow_id,
    char *output, size_t output_size, pcap_flow_stream_result_t *result)
{
    if (!reader || !packet_index || !analysis || flow_id >= analysis->flow_count ||
        !output || output_size < 2U || !result) return PCAP_READER_INVALID_ARG;
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
                  "Flow #%u | %s | %s (%s)\n"
                  "%s:%u <-> %s:%u\n"
                  "%lu packet(s), %lu payload packet(s), %llu payload bytes\n\n",
                  (unsigned)flow_id + 1U, pcap_flow_transport_name(flow->ip_protocol),
                  pcap_flow_application_name((pcap_application_t)flow->app_protocol),
                  pcap_flow_confidence_name((pcap_app_confidence_t)flow->app_confidence),
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
                char shown = (c == '\r' || c == '\n' || c == '\t') ? (char)c :
                             (isprint(c) ? (char)c : '.');
                stream_append(output, output_size, &position, result, "%c", shown);
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
