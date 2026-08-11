#include "pcap_reader.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PCAP_GLOBAL_HEADER_SIZE       24U
#define PCAP_PACKET_HEADER_SIZE       16U
#define PCAP_MAX_SNAPLEN              (16U * 1024U * 1024U)
#define PCAP_DECODE_BYTES             512U
#define PCAP_PROGRESS_PACKET_INTERVAL 64U

struct pcap_reader {
    FILE *file;
    pcap_capture_info_t info;
    bool little_endian;
};

static uint16_t read_u16(const uint8_t *data, bool little_endian)
{
    if (little_endian) {
        return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    }
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t read_u32(const uint8_t *data, bool little_endian)
{
    if (little_endian) {
        return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
               ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint16_t read_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void format_mac(const uint8_t *mac, char *out, size_t out_size)
{
    if (!mac || !out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_ipv4(const uint8_t *address, char *out, size_t out_size)
{
    if (!address || !out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%u.%u.%u.%u",
             address[0], address[1], address[2], address[3]);
}

static void format_ipv6(const uint8_t *address, char *out, size_t out_size)
{
    if (!address || !out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
             "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
             address[0], address[1], address[2], address[3],
             address[4], address[5], address[6], address[7],
             address[8], address[9], address[10], address[11],
             address[12], address[13], address[14], address[15]);
}

static void format_ieee802154_address(const uint8_t *address, size_t length,
                                      char *out, size_t out_size)
{
    if (!address || !out || out_size == 0 || (length != 2 && length != 8)) {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; i < length; i++) {
        size_t source_index = length - 1 - i;
        int written = snprintf(out + pos, out_size - pos, i == 0 ? "%02X" : ":%02X",
                               address[source_index]);
        if (written < 0 || (size_t)written >= out_size - pos) {
            out[out_size - 1] = '\0';
            return;
        }
        pos += (size_t)written;
    }
}

static bool file_size(FILE *file, uint64_t *size_out)
{
    if (!file || !size_out) {
        return false;
    }
    long current = ftell(file);
    if (current < 0 || fseek(file, 0, SEEK_END) != 0) {
        return false;
    }
    long end = ftell(file);
    if (end < 0 || fseek(file, current, SEEK_SET) != 0) {
        return false;
    }
    *size_out = (uint64_t)end;
    return true;
}

pcap_reader_status_t pcap_reader_open(const char *path, pcap_reader_t **reader_out,
                                      pcap_capture_info_t *capture_info_out)
{
    if (!path || !reader_out || !capture_info_out) {
        return PCAP_READER_INVALID_ARG;
    }
    *reader_out = NULL;
    memset(capture_info_out, 0, sizeof(*capture_info_out));

    FILE *file = fopen(path, "rb");
    if (!file) {
        return PCAP_READER_IO_ERROR;
    }

    uint8_t header[PCAP_GLOBAL_HEADER_SIZE];
    size_t header_read = fread(header, 1, sizeof(header), file);
    if (header_read != sizeof(header)) {
        bool read_error = ferror(file) != 0;
        fclose(file);
        if (read_error) {
            return PCAP_READER_IO_ERROR;
        }
        return header_read >= 4 ? PCAP_READER_TRUNCATED : PCAP_READER_INVALID_FORMAT;
    }

    bool little_endian = false;
    pcap_timestamp_resolution_t resolution = PCAP_TIMESTAMP_MICROSECONDS;
    if (header[0] == 0xD4 && header[1] == 0xC3 && header[2] == 0xB2 && header[3] == 0xA1) {
        little_endian = true;
    } else if (header[0] == 0xA1 && header[1] == 0xB2 &&
               header[2] == 0xC3 && header[3] == 0xD4) {
        little_endian = false;
    } else if (header[0] == 0x4D && header[1] == 0x3C &&
               header[2] == 0xB2 && header[3] == 0xA1) {
        little_endian = true;
        resolution = PCAP_TIMESTAMP_NANOSECONDS;
    } else if (header[0] == 0xA1 && header[1] == 0xB2 &&
               header[2] == 0x3C && header[3] == 0x4D) {
        little_endian = false;
        resolution = PCAP_TIMESTAMP_NANOSECONDS;
    } else if (header[0] == 0x0A && header[1] == 0x0D &&
               header[2] == 0x0D && header[3] == 0x0A) {
        fclose(file);
        return PCAP_READER_UNSUPPORTED_FORMAT;
    } else {
        fclose(file);
        return PCAP_READER_INVALID_FORMAT;
    }

    pcap_capture_info_t info = {
        .version_major = read_u16(header + 4, little_endian),
        .version_minor = read_u16(header + 6, little_endian),
        .snaplen = read_u32(header + 16, little_endian),
        /* LINKTYPE is stored in the low 16 bits; the upper bits carry
         * optional link-layer metadata in newer classic-PCAP files. */
        .link_type = read_u32(header + 20, little_endian) & 0xFFFFU,
        .big_endian = !little_endian,
        .timestamp_resolution = resolution,
    };
    if (info.version_major != 2 || info.version_minor != 4 ||
        info.snaplen == 0 || info.snaplen > PCAP_MAX_SNAPLEN ||
        !file_size(file, &info.file_size) || info.file_size < PCAP_GLOBAL_HEADER_SIZE) {
        fclose(file);
        return PCAP_READER_INVALID_FORMAT;
    }

    pcap_reader_t *reader = calloc(1, sizeof(*reader));
    if (!reader) {
        fclose(file);
        return PCAP_READER_NO_MEMORY;
    }
    reader->file = file;
    reader->info = info;
    reader->little_endian = little_endian;
    *capture_info_out = info;
    *reader_out = reader;
    return PCAP_READER_OK;
}

void pcap_reader_close(pcap_reader_t *reader)
{
    if (!reader) {
        return;
    }
    if (reader->file) {
        fclose(reader->file);
    }
    free(reader);
}

pcap_reader_status_t pcap_reader_scan(pcap_reader_t *reader,
                                      pcap_packet_index_t *index,
                                      size_t index_capacity,
                                      pcap_scan_summary_t *summary_out,
                                      const volatile bool *cancel_requested,
                                      pcap_reader_progress_cb_t progress_cb,
                                      void *progress_ctx)
{
    if (!reader || !reader->file || !summary_out || (index_capacity > 0 && !index)) {
        return PCAP_READER_INVALID_ARG;
    }
    memset(summary_out, 0, sizeof(*summary_out));
    clearerr(reader->file);
    if (fseek(reader->file, PCAP_GLOBAL_HEADER_SIZE, SEEK_SET) != 0) {
        return PCAP_READER_IO_ERROR;
    }

    pcap_reader_status_t final_status = PCAP_READER_OK;
    uint8_t record_header[PCAP_PACKET_HEADER_SIZE];
    while (true) {
        if (cancel_requested && *cancel_requested) {
            return PCAP_READER_CANCELLED;
        }

        size_t read_count = fread(record_header, 1, sizeof(record_header), reader->file);
        if (read_count == 0 && feof(reader->file)) {
            break;
        }
        if (read_count != sizeof(record_header)) {
            if (ferror(reader->file)) {
                return PCAP_READER_IO_ERROR;
            }
            summary_out->truncated_tail = true;
            final_status = PCAP_READER_TRUNCATED;
            break;
        }

        uint32_t timestamp_seconds = read_u32(record_header, reader->little_endian);
        uint32_t timestamp_fraction = read_u32(record_header + 4, reader->little_endian);
        uint32_t captured_length = read_u32(record_header + 8, reader->little_endian);
        uint32_t original_length = read_u32(record_header + 12, reader->little_endian);
        long data_offset = ftell(reader->file);
        if (data_offset < 0) {
            return PCAP_READER_IO_ERROR;
        }

        uint32_t timestamp_limit = reader->info.timestamp_resolution == PCAP_TIMESTAMP_NANOSECONDS
                                       ? 1000000000U : 1000000U;
        if (captured_length > reader->info.snaplen || captured_length > PCAP_MAX_SNAPLEN) {
            summary_out->malformed_records++;
            return PCAP_READER_INVALID_FORMAT;
        }
        if (captured_length == 0 || timestamp_fraction >= timestamp_limit ||
            original_length < captured_length) {
            summary_out->malformed_records++;
        }
        if ((uint64_t)data_offset + captured_length > reader->info.file_size) {
            summary_out->truncated_tail = true;
            final_status = PCAP_READER_TRUNCATED;
            break;
        }

        if (summary_out->packet_count == 0) {
            summary_out->first_timestamp_seconds = timestamp_seconds;
            summary_out->first_timestamp_fraction = timestamp_fraction;
        }
        summary_out->last_timestamp_seconds = timestamp_seconds;
        summary_out->last_timestamp_fraction = timestamp_fraction;
        summary_out->packet_count++;
        summary_out->captured_bytes += captured_length;
        summary_out->original_bytes += original_length;

        if (summary_out->indexed_packets < index_capacity) {
            pcap_packet_index_t *entry = &index[summary_out->indexed_packets++];
            entry->data_offset = (uint64_t)data_offset;
            entry->timestamp_seconds = timestamp_seconds;
            entry->timestamp_fraction = timestamp_fraction;
            entry->captured_length = captured_length;
            entry->original_length = original_length;
        } else {
            summary_out->index_limited = true;
            if (final_status == PCAP_READER_OK) {
                final_status = PCAP_READER_LIMIT_REACHED;
            }
        }

        if (fseek(reader->file, (long)captured_length, SEEK_CUR) != 0) {
            return PCAP_READER_IO_ERROR;
        }

        if (progress_cb &&
            (summary_out->packet_count % PCAP_PROGRESS_PACKET_INTERVAL) == 0) {
            long offset = ftell(reader->file);
            progress_cb(offset > 0 ? (uint64_t)offset : 0, reader->info.file_size,
                        summary_out->packet_count, progress_ctx);
        }
    }

    if (progress_cb) {
        progress_cb(reader->info.file_size, reader->info.file_size,
                    summary_out->packet_count, progress_ctx);
    }
    return final_status;
}

pcap_reader_status_t pcap_reader_read_packet(pcap_reader_t *reader,
                                             const pcap_packet_index_t *packet,
                                             uint8_t *buffer,
                                             size_t buffer_capacity,
                                             size_t *bytes_read_out)
{
    if (!reader || !reader->file || !packet || !buffer || buffer_capacity == 0 ||
        !bytes_read_out) {
        return PCAP_READER_INVALID_ARG;
    }
    *bytes_read_out = 0;
    if (packet->data_offset > reader->info.file_size ||
        packet->captured_length > reader->info.file_size - packet->data_offset ||
        packet->data_offset > (uint64_t)LONG_MAX) {
        return PCAP_READER_INVALID_FORMAT;
    }
    if (fseek(reader->file, (long)packet->data_offset, SEEK_SET) != 0) {
        return PCAP_READER_IO_ERROR;
    }
    size_t requested = packet->captured_length;
    if (requested > buffer_capacity) {
        requested = buffer_capacity;
    }
    size_t actual = fread(buffer, 1, requested, reader->file);
    *bytes_read_out = actual;
    if (actual != requested) {
        return PCAP_READER_IO_ERROR;
    }
    return requested < packet->captured_length ? PCAP_READER_LIMIT_REACHED : PCAP_READER_OK;
}

static const char *transport_name(uint8_t protocol)
{
    switch (protocol) {
        case 1: return "ICMP";
        case 2: return "IGMP";
        case 6: return "TCP";
        case 17: return "UDP";
        case 41: return "IPv6";
        case 47: return "GRE";
        case 50: return "ESP";
        case 58: return "ICMPv6";
        default: return "IP";
    }
}

static const char *port_protocol(uint8_t ip_protocol, uint16_t source_port,
                                 uint16_t destination_port)
{
    if (source_port == 53 || destination_port == 53) return "DNS";
    if (source_port == 67 || destination_port == 67 ||
        source_port == 68 || destination_port == 68) return "DHCP";
    if (source_port == 80 || destination_port == 80 ||
        source_port == 8080 || destination_port == 8080) return "HTTP";
    if (source_port == 443 || destination_port == 443) return "TLS";
    if (source_port == 5353 || destination_port == 5353) return "mDNS";
    if (source_port == 1900 || destination_port == 1900) return "SSDP";
    if (source_port == 123 || destination_port == 123) return "NTP";
    return transport_name(ip_protocol);
}

static bool looks_like_http(const uint8_t *data, size_t length)
{
    static const char *methods[] = {
        "GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ", "PATCH ", "HTTP/"
    };
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        size_t method_length = strlen(methods[i]);
        if (length >= method_length && memcmp(data, methods[i], method_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool looks_like_tls(const uint8_t *data, size_t length)
{
    return length >= 5 && data[0] >= 20 && data[0] <= 23 && data[1] == 3 && data[2] <= 4;
}

const char *pcap_reader_dns_type_name(uint16_t qtype)
{
    switch (qtype) {
        case 1: return "A";
        case 2: return "NS";
        case 5: return "CNAME";
        case 6: return "SOA";
        case 12: return "PTR";
        case 15: return "MX";
        case 16: return "TXT";
        case 28: return "AAAA";
        case 33: return "SRV";
        case 41: return "OPT";
        case 43: return "DS";
        case 46: return "RRSIG";
        case 47: return "NSEC";
        case 48: return "DNSKEY";
        case 255: return "ANY";
        default: return "OTHER";
    }
}

const char *pcap_reader_dns_rcode_name(uint8_t rcode)
{
    switch (rcode) {
        case 0: return "NOERROR";
        case 1: return "FORMERR";
        case 2: return "SERVFAIL";
        case 3: return "NXDOMAIN";
        case 4: return "NOTIMP";
        case 5: return "REFUSED";
        case 9: return "NOTAUTH";
        default: return "OTHER";
    }
}

static bool dns_decode_name(const uint8_t *message, size_t message_length,
                            size_t *offset, char *output, size_t output_size)
{
    if (!message || !offset || !output || output_size == 0 || *offset >= message_length) {
        return false;
    }

    size_t position = *offset;
    size_t resume_offset = 0;
    size_t output_position = 0;
    unsigned pointer_hops = 0;
    bool jumped = false;
    output[0] = '\0';

    while (position < message_length) {
        uint8_t label_length = message[position];
        if (label_length == 0) {
            if (!jumped) {
                resume_offset = position + 1;
            }
            *offset = resume_offset;
            output[output_position] = '\0';
            return true;
        }

        if ((label_length & 0xC0U) == 0xC0U) {
            if (position + 1 >= message_length || ++pointer_hops > 32U) {
                return false;
            }
            uint16_t pointer = (uint16_t)(((label_length & 0x3FU) << 8) |
                                          message[position + 1]);
            if (pointer >= message_length) {
                return false;
            }
            if (!jumped) {
                resume_offset = position + 2;
                jumped = true;
            }
            position = pointer;
            continue;
        }

        if ((label_length & 0xC0U) != 0 || label_length > 63U ||
            position + 1U + label_length > message_length) {
            return false;
        }
        position++;
        if (output_position > 0 && output_position + 1 < output_size) {
            output[output_position++] = '.';
        }
        size_t copy_length = label_length;
        if (copy_length > output_size - output_position - 1U) {
            copy_length = output_size - output_position - 1U;
        }
        for (size_t i = 0; i < copy_length; i++) {
            uint8_t character = message[position + i];
            output[output_position++] =
                (character >= 32U && character <= 126U) ? (char)character : '.';
        }
        position += label_length;
        if (!jumped) {
            resume_offset = position;
        }
    }
    return false;
}

static void dns_record_address(pcap_packet_details_t *details,
                               const char *owner_name, const char *address)
{
    if (!details || !address || !address[0]) return;
    if (details->dns_first_address[0] == '\0') {
        snprintf(details->dns_first_address, sizeof(details->dns_first_address),
                 "%s", address);
        if (owner_name && owner_name[0]) {
            snprintf(details->dns_first_address_owner,
                     sizeof(details->dns_first_address_owner), "%s", owner_name);
        }
    }
    for (uint8_t i = 0; i < details->dns_address_count; i++) {
        if (strcasecmp(details->dns_addresses[i], address) == 0) return;
    }
    if (details->dns_address_count >= PCAP_DNS_MAX_PACKET_ADDRESSES) {
        details->dns_address_limited = true;
        return;
    }
    snprintf(details->dns_addresses[details->dns_address_count],
             sizeof(details->dns_addresses[details->dns_address_count]),
             "%s", address);
    details->dns_address_count++;
}

static void describe_dns(const uint8_t *data, size_t length, pcap_packet_details_t *details)
{
    if (!data || !details || length < 12) {
        return;
    }
    uint16_t dns_flags = read_be16(data + 2);
    details->flags |= PCAP_PACKET_FLAG_DNS;
    details->dns_valid = true;
    details->dns_id = read_be16(data);
    details->dns_response = (dns_flags & 0x8000U) != 0;
    details->dns_rcode = (uint8_t)(dns_flags & 0x0FU);
    details->dns_question_count = read_be16(data + 4);
    details->dns_answer_count = read_be16(data + 6);

    size_t offset = 12;
    bool questions_complete = true;
    for (uint16_t question = 0; question < details->dns_question_count; question++) {
        char query_name[sizeof(details->dns_query)] = {0};
        if (!dns_decode_name(data, length, &offset, query_name, sizeof(query_name)) ||
            offset + 4 > length) {
            questions_complete = false;
            break;
        }
        uint16_t qtype = read_be16(data + offset);
        offset += 4;
        if (question == 0) {
            memcpy(details->dns_query, query_name, sizeof(details->dns_query));
            details->dns_query[sizeof(details->dns_query) - 1] = '\0';
            details->dns_qtype = qtype;
        }
    }

    for (uint16_t answer = 0; questions_complete && answer < details->dns_answer_count &&
                              answer < 64U; answer++) {
        char owner_name[96];
        if (!dns_decode_name(data, length, &offset, owner_name, sizeof(owner_name)) ||
            offset + 10 > length) {
            break;
        }
        uint16_t answer_type = read_be16(data + offset);
        uint16_t data_length = read_be16(data + offset + 8);
        offset += 10;
        if (offset + data_length > length) {
            break;
        }
        if (answer_type == 1 && data_length == 4) {
            char address[64];
            format_ipv4(data + offset, address, sizeof(address));
            dns_record_address(details, owner_name, address);
            if (details->dns_first_answer[0] == '\0') {
                snprintf(details->dns_first_answer, sizeof(details->dns_first_answer),
                         "%s", address);
            }
        } else if (answer_type == 28 && data_length == 16) {
            char address[64];
            format_ipv6(data + offset, address, sizeof(address));
            dns_record_address(details, owner_name, address);
            if (details->dns_first_answer[0] == '\0') {
                snprintf(details->dns_first_answer, sizeof(details->dns_first_answer),
                         "%s", address);
            }
        } else if (details->dns_first_answer[0] == '\0') {
            if (answer_type == 2 || answer_type == 5 || answer_type == 12) {
                size_t name_offset = offset;
                (void)dns_decode_name(data, length, &name_offset,
                                      details->dns_first_answer,
                                      sizeof(details->dns_first_answer));
            } else if (answer_type == 15 && data_length > 2) {
                size_t name_offset = offset + 2;
                (void)dns_decode_name(data, length, &name_offset,
                                      details->dns_first_answer,
                                      sizeof(details->dns_first_answer));
            }
        }
        offset += data_length;
    }

    const char *direction = details->dns_response ? "DNS response" : "DNS query";
    const char *qtype_name = pcap_reader_dns_type_name(details->dns_qtype);
    if (details->dns_query[0] && details->dns_response) {
        snprintf(details->info, sizeof(details->info),
                 "%.12s %.52s %.6s %.8s%.4s%.40s",
                 direction, details->dns_query, qtype_name,
                 pcap_reader_dns_rcode_name(details->dns_rcode),
                 details->dns_first_answer[0] ? " -> " : "",
                 details->dns_first_answer);
    } else if (details->dns_query[0]) {
        snprintf(details->info, sizeof(details->info), "%.12s %.72s %.6s",
                 direction, details->dns_query, qtype_name);
    } else {
        snprintf(details->info, sizeof(details->info), "%.12s (q=%u a=%u) %.8s",
                 direction, details->dns_question_count, details->dns_answer_count,
                 pcap_reader_dns_rcode_name(details->dns_rcode));
    }
}

static void describe_transport(const uint8_t *data, size_t length, uint8_t ip_protocol,
                               size_t packet_offset, pcap_packet_details_t *details)
{
    if (!data || !details) {
        return;
    }
    details->ip_protocol = ip_protocol;
    snprintf(details->protocol, sizeof(details->protocol), "%s", transport_name(ip_protocol));

    size_t payload_offset = 0;
    if (ip_protocol == 6) {
        details->flags |= PCAP_PACKET_FLAG_TCP;
        if (length < 20) {
            details->malformed = true;
            snprintf(details->info, sizeof(details->info), "Truncated TCP header");
            return;
        }
        details->source_port = read_be16(data);
        details->destination_port = read_be16(data + 2);
        details->tcp_sequence = read_be32(data + 4);
        details->tcp_acknowledgment = read_be32(data + 8);
        details->tcp_window = read_be16(data + 14);
        uint8_t header_length = (uint8_t)((data[12] >> 4) * 4U);
        if (header_length < 20 || header_length > length) {
            details->malformed = true;
            snprintf(details->info, sizeof(details->info), "Invalid TCP header length");
            return;
        }
        payload_offset = header_length;
        uint8_t flags = data[13];
        details->tcp_flags = flags;
        snprintf(details->info, sizeof(details->info), "TCP %u -> %u flags 0x%02X",
                 details->source_port, details->destination_port, flags);
    } else if (ip_protocol == 17) {
        details->flags |= PCAP_PACKET_FLAG_UDP;
        if (length < 8) {
            details->malformed = true;
            snprintf(details->info, sizeof(details->info), "Truncated UDP header");
            return;
        }
        details->source_port = read_be16(data);
        details->destination_port = read_be16(data + 2);
        payload_offset = 8;
        snprintf(details->info, sizeof(details->info), "UDP %u -> %u",
                 details->source_port, details->destination_port);
    } else {
        snprintf(details->info, sizeof(details->info), "%s packet", transport_name(ip_protocol));
        return;
    }

    const char *application = port_protocol(ip_protocol, details->source_port,
                                            details->destination_port);
    const uint8_t *payload = data + payload_offset;
    size_t payload_length = length - payload_offset;
    details->payload_offset = (uint32_t)(packet_offset + payload_offset);
    details->payload_captured_length = (uint32_t)payload_length;
    if (looks_like_http(payload, payload_length)) {
        application = "HTTP";
    } else if (looks_like_tls(payload, payload_length)) {
        application = "TLS";
    }
    snprintf(details->protocol, sizeof(details->protocol), "%s", application);
    if (strcmp(application, "DNS") == 0 || strcmp(application, "mDNS") == 0) {
        details->flags |= PCAP_PACKET_FLAG_DNS;
    } else if (strcmp(application, "HTTP") == 0) {
        details->flags |= PCAP_PACKET_FLAG_HTTP;
    } else if (strcmp(application, "TLS") == 0) {
        details->flags |= PCAP_PACKET_FLAG_TLS;
    }

    if (strcmp(application, "DNS") == 0 || strcmp(application, "mDNS") == 0) {
        size_t dns_offset = 0;
        size_t dns_length = payload_length;
        if (ip_protocol == 6) {
            if (payload_length < 2) return;
            uint16_t declared_length = read_be16(payload);
            dns_offset = 2;
            if (declared_length < 12U) return;
            size_t available_length = payload_length - dns_offset;
            dns_length = declared_length < available_length
                             ? declared_length : available_length;
            if (dns_length < 12U) return;
        }
        describe_dns(payload + dns_offset, dns_length, details);
    } else if (strcmp(application, "HTTP") == 0 && payload_length > 0) {
        char first_line[92];
        size_t copy_length = 0;
        while (copy_length < payload_length && copy_length + 1 < sizeof(first_line) &&
               payload[copy_length] != '\r' && payload[copy_length] != '\n') {
            unsigned char c = payload[copy_length];
            first_line[copy_length] = (c >= 32 && c <= 126) ? (char)c : '.';
            copy_length++;
        }
        first_line[copy_length] = '\0';
        snprintf(details->info, sizeof(details->info), "%s", first_line);
    } else if (strcmp(application, "TLS") == 0 && looks_like_tls(payload, payload_length)) {
        snprintf(details->info, sizeof(details->info), "TLS record type %u version %u.%u",
                 payload[0], payload[1], payload[2]);
    }
}

static void describe_ipv4(const uint8_t *data, size_t length, size_t packet_offset,
                          pcap_packet_details_t *details)
{
    details->flags |= PCAP_PACKET_FLAG_IPV4;
    if (length < 20 || (data[0] >> 4) != 4) {
        details->malformed = true;
        snprintf(details->protocol, sizeof(details->protocol), "IPv4");
        snprintf(details->info, sizeof(details->info), "Malformed IPv4 header");
        return;
    }
    size_t header_length = (size_t)(data[0] & 0x0FU) * 4U;
    if (header_length < 20 || header_length > length) {
        details->malformed = true;
        snprintf(details->protocol, sizeof(details->protocol), "IPv4");
        snprintf(details->info, sizeof(details->info), "Invalid IPv4 header length");
        return;
    }
    format_ipv4(data + 12, details->source, sizeof(details->source));
    format_ipv4(data + 16, details->destination, sizeof(details->destination));
    describe_transport(data + header_length, length - header_length, data[9],
                       packet_offset + header_length, details);
}

static void describe_ipv6(const uint8_t *data, size_t length, size_t packet_offset,
                          pcap_packet_details_t *details)
{
    details->flags |= PCAP_PACKET_FLAG_IPV6;
    if (length < 40 || (data[0] >> 4) != 6) {
        details->malformed = true;
        snprintf(details->protocol, sizeof(details->protocol), "IPv6");
        snprintf(details->info, sizeof(details->info), "Malformed IPv6 header");
        return;
    }
    format_ipv6(data + 8, details->source, sizeof(details->source));
    format_ipv6(data + 24, details->destination, sizeof(details->destination));
    describe_transport(data + 40, length - 40, data[6], packet_offset + 40, details);
}

static void describe_ethernet(const uint8_t *data, size_t length,
                              pcap_packet_details_t *details)
{
    details->flags |= PCAP_PACKET_FLAG_ETHERNET;
    if (length < 14) {
        details->malformed = true;
        snprintf(details->protocol, sizeof(details->protocol), "Ethernet");
        snprintf(details->info, sizeof(details->info), "Truncated Ethernet header");
        return;
    }
    format_mac(data + 6, details->source, sizeof(details->source));
    format_mac(data, details->destination, sizeof(details->destination));
    format_mac(data + 6, details->source_mac, sizeof(details->source_mac));
    format_mac(data, details->destination_mac, sizeof(details->destination_mac));
    size_t offset = 14;
    uint16_t ether_type = read_be16(data + 12);
    int vlan_depth = 0;
    while ((ether_type == 0x8100U || ether_type == 0x88A8U) &&
           offset + 4 <= length && vlan_depth < 2) {
        ether_type = read_be16(data + offset + 2);
        offset += 4;
        vlan_depth++;
    }
    details->ether_type = ether_type;
    if (ether_type == 0x0800U) {
        describe_ipv4(data + offset, length - offset, offset, details);
    } else if (ether_type == 0x86DDU) {
        describe_ipv6(data + offset, length - offset, offset, details);
    } else if (ether_type == 0x0806U) {
        details->flags |= PCAP_PACKET_FLAG_ARP;
        snprintf(details->protocol, sizeof(details->protocol), "ARP");
        if (offset + 28U <= length && read_be16(data + offset) == 1U &&
            read_be16(data + offset + 2U) == 0x0800U &&
            data[offset + 4U] == 6U && data[offset + 5U] == 4U) {
            details->arp_operation = read_be16(data + offset + 6U);
            format_mac(data + offset + 8U, details->arp_sender_mac,
                       sizeof(details->arp_sender_mac));
            format_ipv4(data + offset + 14U, details->arp_sender_ip,
                        sizeof(details->arp_sender_ip));
            format_mac(data + offset + 18U, details->arp_target_mac,
                       sizeof(details->arp_target_mac));
            format_ipv4(data + offset + 24U, details->arp_target_ip,
                        sizeof(details->arp_target_ip));
            snprintf(details->info, sizeof(details->info),
                     "ARP %s %.15s is-at %.17s -> %.15s%s",
                     details->arp_operation == 1U ? "request" :
                     (details->arp_operation == 2U ? "reply" : "operation"),
                     details->arp_sender_ip, details->arp_sender_mac,
                     details->arp_target_ip, vlan_depth > 0 ? " (VLAN)" : "");
        } else {
            snprintf(details->info, sizeof(details->info), "ARP frame%s",
                     vlan_depth > 0 ? " (VLAN)" : "");
        }
    } else if (ether_type == 0x888EU) {
        details->flags |= PCAP_PACKET_FLAG_EAPOL;
        snprintf(details->protocol, sizeof(details->protocol), "EAPOL");
        snprintf(details->info, sizeof(details->info), "802.1X authentication frame");
    } else {
        snprintf(details->protocol, sizeof(details->protocol), "Ethernet");
        snprintf(details->info, sizeof(details->info), "EtherType 0x%04X%s",
                 ether_type, vlan_depth > 0 ? " (VLAN)" : "");
    }
}

static const char *wifi_management_subtype(uint8_t subtype)
{
    switch (subtype) {
        case 0: return "Association Request";
        case 1: return "Association Response";
        case 4: return "Probe Request";
        case 5: return "Probe Response";
        case 8: return "Beacon";
        case 10: return "Disassociation";
        case 11: return "Authentication";
        case 12: return "Deauthentication";
        default: return "Management";
    }
}

static const char *wifi_control_subtype(uint8_t subtype)
{
    switch (subtype) {
        case 8: return "Block Ack Request";
        case 9: return "Block Ack";
        case 10: return "PS-Poll";
        case 11: return "RTS";
        case 12: return "CTS";
        case 13: return "ACK";
        default: return "Control";
    }
}

static void describe_ieee80211(const uint8_t *data, size_t length,
                               pcap_packet_details_t *details)
{
    details->flags |= PCAP_PACKET_FLAG_WIFI;
    if (length < 10) {
        details->malformed = true;
        snprintf(details->protocol, sizeof(details->protocol), "802.11");
        snprintf(details->info, sizeof(details->info), "Truncated IEEE 802.11 header");
        return;
    }
    uint16_t frame_control = read_le16(data);
    uint8_t type = (uint8_t)((frame_control >> 2) & 0x03U);
    uint8_t subtype = (uint8_t)((frame_control >> 4) & 0x0FU);
    details->wifi_frame_type = type;
    details->wifi_frame_subtype = subtype;
    bool to_ds = (frame_control & 0x0100U) != 0;
    bool from_ds = (frame_control & 0x0200U) != 0;

    format_mac(data + 4, details->destination, sizeof(details->destination));
    format_mac(data + 4, details->destination_mac, sizeof(details->destination_mac));
    if (length >= 16) {
        format_mac(data + 10, details->source, sizeof(details->source));
        format_mac(data + 10, details->source_mac, sizeof(details->source_mac));
    }
    snprintf(details->protocol, sizeof(details->protocol), "802.11");
    if (type == 0) {
        details->flags |= PCAP_PACKET_FLAG_WIFI_MGMT;
        snprintf(details->info, sizeof(details->info), "%s", wifi_management_subtype(subtype));
        return;
    }
    if (type == 1) {
        snprintf(details->info, sizeof(details->info), "%s", wifi_control_subtype(subtype));
        return;
    }
    if (type != 2 || length < 24) {
        snprintf(details->info, sizeof(details->info), "Reserved frame type %u", type);
        return;
    }

    size_t header_length = 24;
    if (to_ds && from_ds) {
        header_length += 6;
    }
    if ((subtype & 0x08U) != 0) {
        header_length += 2;
    }
    if (header_length > length) {
        details->malformed = true;
        snprintf(details->info, sizeof(details->info), "Truncated 802.11 data header");
        return;
    }

    /* Address roles in data frames depend on the ToDS/FromDS bits. */
    if (to_ds && from_ds) {
        format_mac(data + 24, details->source, sizeof(details->source));
        format_mac(data + 24, details->source_mac, sizeof(details->source_mac));
        format_mac(data + 16, details->destination, sizeof(details->destination));
        format_mac(data + 16, details->destination_mac, sizeof(details->destination_mac));
    } else if (to_ds) {
        format_mac(data + 10, details->source, sizeof(details->source));
        format_mac(data + 10, details->source_mac, sizeof(details->source_mac));
        format_mac(data + 16, details->destination, sizeof(details->destination));
        format_mac(data + 16, details->destination_mac, sizeof(details->destination_mac));
    } else if (from_ds) {
        format_mac(data + 16, details->source, sizeof(details->source));
        format_mac(data + 16, details->source_mac, sizeof(details->source_mac));
        format_mac(data + 4, details->destination, sizeof(details->destination));
        format_mac(data + 4, details->destination_mac, sizeof(details->destination_mac));
    }

    const uint8_t *payload = data + header_length;
    size_t payload_length = length - header_length;
    if (payload_length >= 8 && payload[0] == 0xAA && payload[1] == 0xAA &&
        payload[2] == 0x03) {
        uint16_t ether_type = read_be16(payload + 6);
        details->ether_type = ether_type;
        if (ether_type == 0x888EU) {
            details->flags |= PCAP_PACKET_FLAG_EAPOL;
            snprintf(details->protocol, sizeof(details->protocol), "EAPOL");
            snprintf(details->info, sizeof(details->info), "802.11 EAPOL key frame");
        } else if (ether_type == 0x0800U) {
            describe_ipv4(payload + 8, payload_length - 8, header_length + 8, details);
        } else if (ether_type == 0x86DDU) {
            describe_ipv6(payload + 8, payload_length - 8, header_length + 8, details);
        } else {
            snprintf(details->info, sizeof(details->info), "802.11 Data EtherType 0x%04X",
                     ether_type);
        }
    } else {
        snprintf(details->info, sizeof(details->info), "802.11 Data%s%s",
                 to_ds ? " ToDS" : "", from_ds ? " FromDS" : "");
    }
}

static const char *ieee802154_frame_type(uint8_t type)
{
    switch (type) {
        case 0: return "Beacon";
        case 1: return "Data";
        case 2: return "ACK";
        case 3: return "MAC Command";
        case 5: return "Multipurpose";
        case 6: return "Fragment";
        case 7: return "Extended";
        default: return "Reserved";
    }
}

static bool parse_ieee802154_address(const uint8_t *data, size_t length, size_t *offset,
                                     uint8_t mode, char *out, size_t out_size)
{
    size_t address_length = mode == 2 ? 2U : (mode == 3 ? 8U : 0U);
    if (address_length == 0) {
        return mode == 0;
    }
    if (*offset + address_length > length) {
        return false;
    }
    format_ieee802154_address(data + *offset, address_length, out, out_size);
    *offset += address_length;
    return true;
}

static void describe_ieee802154(const uint8_t *data, size_t length,
                                pcap_packet_details_t *details)
{
    details->flags |= PCAP_PACKET_FLAG_IEEE802154;
    snprintf(details->protocol, sizeof(details->protocol), "802.15.4");
    if (length < 2) {
        details->malformed = true;
        snprintf(details->info, sizeof(details->info), "Truncated IEEE 802.15.4 header");
        return;
    }
    uint16_t frame_control = read_le16(data);
    uint8_t type = (uint8_t)(frame_control & 0x07U);
    bool security = (frame_control & 0x0008U) != 0;
    bool ack_request = (frame_control & 0x0020U) != 0;
    bool pan_compression = (frame_control & 0x0040U) != 0;
    bool sequence_suppressed = (frame_control & 0x0100U) != 0;
    uint8_t destination_mode = (uint8_t)((frame_control >> 10) & 0x03U);
    uint8_t source_mode = (uint8_t)((frame_control >> 14) & 0x03U);
    size_t offset = sequence_suppressed ? 2U : 3U;
    if (offset > length) {
        details->malformed = true;
        snprintf(details->info, sizeof(details->info), "Missing sequence number");
        return;
    }

    uint16_t destination_pan = 0;
    uint16_t source_pan = 0;
    if (destination_mode != 0) {
        if (offset + 2 > length) goto malformed;
        destination_pan = read_le16(data + offset);
        offset += 2;
        char address[32] = {0};
        if (!parse_ieee802154_address(data, length, &offset, destination_mode,
                                     address, sizeof(address))) goto malformed;
        snprintf(details->destination, sizeof(details->destination), "PAN %04X / %s",
                 destination_pan, address);
    }
    if (source_mode != 0) {
        if (pan_compression && destination_mode != 0) {
            source_pan = destination_pan;
        } else {
            if (offset + 2 > length) goto malformed;
            source_pan = read_le16(data + offset);
            offset += 2;
        }
        char address[32] = {0};
        if (!parse_ieee802154_address(data, length, &offset, source_mode,
                                     address, sizeof(address))) goto malformed;
        snprintf(details->source, sizeof(details->source), "PAN %04X / %s",
                 source_pan, address);
    }
    snprintf(details->info, sizeof(details->info), "%s%s%s",
             ieee802154_frame_type(type), security ? " | secured" : "",
             ack_request ? " | ACK requested" : "");
    return;

malformed:
    details->malformed = true;
    snprintf(details->info, sizeof(details->info), "Malformed %s frame",
             ieee802154_frame_type(type));
}

static void describe_packet_bytes(uint32_t link_type, const uint8_t *data, size_t length,
                                  pcap_packet_details_t *details)
{
    switch (link_type) {
        case PCAP_LINKTYPE_ETHERNET:
            describe_ethernet(data, length, details);
            break;
        case PCAP_LINKTYPE_IEEE802_11:
            describe_ieee80211(data, length, details);
            break;
        case PCAP_LINKTYPE_IEEE802_15_4_NOFCS:
            describe_ieee802154(data, length, details);
            break;
        case PCAP_LINKTYPE_IEEE802_15_4_TAP: {
            snprintf(details->protocol, sizeof(details->protocol), "802.15.4 TAP");
            if (length < 4) {
                details->malformed = true;
                snprintf(details->info, sizeof(details->info), "Truncated TAP header");
                break;
            }
            uint16_t header_length = read_le16(data + 2);
            if (header_length < 4 || header_length > length) {
                details->malformed = true;
                snprintf(details->info, sizeof(details->info), "Invalid TAP header length %u",
                         header_length);
                break;
            }
            describe_ieee802154(data + header_length, length - header_length, details);
            break;
        }
        default:
            snprintf(details->protocol, sizeof(details->protocol), "Link %lu",
                     (unsigned long)link_type);
            snprintf(details->info, sizeof(details->info), "Raw packet data");
            break;
    }
}

pcap_reader_status_t pcap_reader_describe_packet(pcap_reader_t *reader,
                                                 const pcap_packet_index_t *packet,
                                                 pcap_packet_details_t *details_out)
{
    if (!reader || !packet || !details_out) {
        return PCAP_READER_INVALID_ARG;
    }
    memset(details_out, 0, sizeof(*details_out));
    details_out->payload_truncated = packet->captured_length < packet->original_length;

    uint8_t buffer[PCAP_DECODE_BYTES];
    size_t bytes_read = 0;
    pcap_reader_status_t status = pcap_reader_read_packet(reader, packet, buffer,
                                                          sizeof(buffer), &bytes_read);
    if (status != PCAP_READER_OK && status != PCAP_READER_LIMIT_REACHED) {
        return status;
    }
    describe_packet_bytes(reader->info.link_type, buffer, bytes_read, details_out);
    if (details_out->malformed) {
        details_out->flags |= PCAP_PACKET_FLAG_MALFORMED;
    }
    if (details_out->payload_truncated) {
        details_out->flags |= PCAP_PACKET_FLAG_TRUNCATED;
    }
    return status;
}

const char *pcap_reader_status_name(pcap_reader_status_t status)
{
    switch (status) {
        case PCAP_READER_OK: return "OK";
        case PCAP_READER_INVALID_ARG: return "Invalid argument";
        case PCAP_READER_IO_ERROR: return "File I/O error";
        case PCAP_READER_INVALID_FORMAT: return "Invalid PCAP";
        case PCAP_READER_UNSUPPORTED_FORMAT: return "Unsupported format";
        case PCAP_READER_TRUNCATED: return "Truncated PCAP";
        case PCAP_READER_LIMIT_REACHED: return "Index limit reached";
        case PCAP_READER_CANCELLED: return "Cancelled";
        case PCAP_READER_NO_MEMORY: return "Out of memory";
        default: return "Unknown error";
    }
}

const char *pcap_reader_link_type_name(uint32_t link_type)
{
    switch (link_type) {
        case PCAP_LINKTYPE_ETHERNET: return "Ethernet";
        case PCAP_LINKTYPE_IEEE802_11: return "IEEE 802.11";
        case PCAP_LINKTYPE_IEEE802_15_4_NOFCS: return "IEEE 802.15.4 no FCS";
        case PCAP_LINKTYPE_IEEE802_15_4_TAP: return "IEEE 802.15.4 TAP";
        default: return "Unknown link type";
    }
}

const char *pcap_reader_filter_name(pcap_packet_filter_t filter)
{
    switch (filter) {
        case PCAP_FILTER_ALL: return "ALL";
        case PCAP_FILTER_DNS: return "DNS";
        case PCAP_FILTER_TCP: return "TCP";
        case PCAP_FILTER_UDP: return "UDP";
        case PCAP_FILTER_HTTP: return "HTTP";
        case PCAP_FILTER_TLS: return "TLS";
        case PCAP_FILTER_ARP: return "ARP";
        case PCAP_FILTER_EAPOL: return "EAPOL";
        case PCAP_FILTER_WIFI_MGMT: return "802.11 MGMT";
        case PCAP_FILTER_MALFORMED: return "MALFORMED";
        default: return "UNKNOWN";
    }
}

uint32_t pcap_reader_filter_flag(pcap_packet_filter_t filter)
{
    switch (filter) {
        case PCAP_FILTER_DNS: return PCAP_PACKET_FLAG_DNS;
        case PCAP_FILTER_TCP: return PCAP_PACKET_FLAG_TCP;
        case PCAP_FILTER_UDP: return PCAP_PACKET_FLAG_UDP;
        case PCAP_FILTER_HTTP: return PCAP_PACKET_FLAG_HTTP;
        case PCAP_FILTER_TLS: return PCAP_PACKET_FLAG_TLS;
        case PCAP_FILTER_ARP: return PCAP_PACKET_FLAG_ARP;
        case PCAP_FILTER_EAPOL: return PCAP_PACKET_FLAG_EAPOL;
        case PCAP_FILTER_WIFI_MGMT: return PCAP_PACKET_FLAG_WIFI_MGMT;
        case PCAP_FILTER_MALFORMED: return PCAP_PACKET_FLAG_MALFORMED;
        case PCAP_FILTER_ALL:
        default: return 0;
    }
}

bool pcap_reader_packet_matches_filter(uint32_t packet_flags,
                                       pcap_packet_filter_t filter)
{
    if (filter == PCAP_FILTER_ALL) {
        return true;
    }
    uint32_t required_flag = pcap_reader_filter_flag(filter);
    return required_flag != 0 && (packet_flags & required_flag) != 0;
}
