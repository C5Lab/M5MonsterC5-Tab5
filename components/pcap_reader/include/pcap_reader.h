#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_LINKTYPE_ETHERNET               1U
#define PCAP_LINKTYPE_IEEE802_11           105U
#define PCAP_LINKTYPE_IEEE802_15_4_NOFCS   230U
#define PCAP_LINKTYPE_IEEE802_15_4_TAP     283U

#define PCAP_PACKET_FLAG_ETHERNET    (1UL << 0)
#define PCAP_PACKET_FLAG_IPV4        (1UL << 1)
#define PCAP_PACKET_FLAG_IPV6        (1UL << 2)
#define PCAP_PACKET_FLAG_TCP         (1UL << 3)
#define PCAP_PACKET_FLAG_UDP         (1UL << 4)
#define PCAP_PACKET_FLAG_DNS         (1UL << 5)
#define PCAP_PACKET_FLAG_HTTP        (1UL << 6)
#define PCAP_PACKET_FLAG_TLS         (1UL << 7)
#define PCAP_PACKET_FLAG_ARP         (1UL << 8)
#define PCAP_PACKET_FLAG_EAPOL       (1UL << 9)
#define PCAP_PACKET_FLAG_WIFI        (1UL << 10)
#define PCAP_PACKET_FLAG_WIFI_MGMT   (1UL << 11)
#define PCAP_PACKET_FLAG_IEEE802154  (1UL << 12)
#define PCAP_PACKET_FLAG_MALFORMED   (1UL << 13)
#define PCAP_PACKET_FLAG_TRUNCATED   (1UL << 14)

#define PCAP_DNS_MAX_PACKET_ADDRESSES 4U

typedef enum {
    PCAP_FILTER_ALL = 0,
    PCAP_FILTER_DNS,
    PCAP_FILTER_TCP,
    PCAP_FILTER_UDP,
    PCAP_FILTER_HTTP,
    PCAP_FILTER_TLS,
    PCAP_FILTER_ARP,
    PCAP_FILTER_EAPOL,
    PCAP_FILTER_WIFI_MGMT,
    PCAP_FILTER_MALFORMED,
    PCAP_FILTER_COUNT,
} pcap_packet_filter_t;

typedef enum {
    PCAP_READER_OK = 0,
    PCAP_READER_INVALID_ARG,
    PCAP_READER_IO_ERROR,
    PCAP_READER_INVALID_FORMAT,
    PCAP_READER_UNSUPPORTED_FORMAT,
    PCAP_READER_TRUNCATED,
    PCAP_READER_LIMIT_REACHED,
    PCAP_READER_CANCELLED,
    PCAP_READER_NO_MEMORY,
} pcap_reader_status_t;

typedef enum {
    PCAP_TIMESTAMP_MICROSECONDS = 0,
    PCAP_TIMESTAMP_NANOSECONDS,
} pcap_timestamp_resolution_t;

typedef struct pcap_reader pcap_reader_t;

typedef struct {
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t snaplen;
    uint32_t link_type;
    uint64_t file_size;
    bool big_endian;
    pcap_timestamp_resolution_t timestamp_resolution;
} pcap_capture_info_t;

typedef struct {
    uint64_t data_offset;
    uint32_t timestamp_seconds;
    uint32_t timestamp_fraction;
    uint32_t captured_length;
    uint32_t original_length;
} pcap_packet_index_t;

typedef struct {
    uint64_t packet_count;
    uint64_t captured_bytes;
    uint64_t original_bytes;
    uint32_t indexed_packets;
    uint32_t malformed_records;
    uint32_t first_timestamp_seconds;
    uint32_t first_timestamp_fraction;
    uint32_t last_timestamp_seconds;
    uint32_t last_timestamp_fraction;
    bool index_limited;
    bool truncated_tail;
} pcap_scan_summary_t;

typedef struct {
    char protocol[24];
    char source[64];
    char destination[64];
    char source_mac[18];
    char destination_mac[18];
    char arp_sender_ip[64];
    char arp_target_ip[64];
    char arp_sender_mac[18];
    char arp_target_mac[18];
    char info[128];
    char dns_query[96];
    char dns_first_answer[80];
    char dns_first_address[64];
    char dns_first_address_owner[96];
    char dns_addresses[PCAP_DNS_MAX_PACKET_ADDRESSES][64];
    uint32_t flags;
    uint32_t tcp_sequence;
    uint32_t tcp_acknowledgment;
    uint32_t payload_offset;
    uint32_t payload_captured_length;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t tcp_window;
    uint16_t ether_type;
    uint16_t arp_operation;
    uint16_t dns_id;
    uint16_t dns_qtype;
    uint16_t dns_question_count;
    uint16_t dns_answer_count;
    uint8_t ip_protocol;
    uint8_t tcp_flags;
    uint8_t dns_rcode;
    uint8_t dns_address_count;
    uint8_t wifi_frame_type;
    uint8_t wifi_frame_subtype;
    bool dns_valid;
    bool dns_response;
    bool dns_address_limited;
    bool malformed;
    bool payload_truncated;
} pcap_packet_details_t;

typedef void (*pcap_reader_progress_cb_t)(uint64_t offset, uint64_t file_size,
                                         uint64_t packet_count, void *user_ctx);

pcap_reader_status_t pcap_reader_open(const char *path, pcap_reader_t **reader_out,
                                      pcap_capture_info_t *capture_info_out);

void pcap_reader_close(pcap_reader_t *reader);

pcap_reader_status_t pcap_reader_scan(pcap_reader_t *reader,
                                      pcap_packet_index_t *index,
                                      size_t index_capacity,
                                      pcap_scan_summary_t *summary_out,
                                      const volatile bool *cancel_requested,
                                      pcap_reader_progress_cb_t progress_cb,
                                      void *progress_ctx);

pcap_reader_status_t pcap_reader_read_packet(pcap_reader_t *reader,
                                             const pcap_packet_index_t *packet,
                                             uint8_t *buffer,
                                             size_t buffer_capacity,
                                             size_t *bytes_read_out);

pcap_reader_status_t pcap_reader_describe_packet(pcap_reader_t *reader,
                                                 const pcap_packet_index_t *packet,
                                                 pcap_packet_details_t *details_out);

/* Sequential iteration over the whole file. Unlike pcap_reader_scan() this does
 * not need an index array, so a caller can walk every record of a capture that
 * is far larger than the indexed-packet ceiling used by the packet table. */
pcap_reader_status_t pcap_reader_iterate_begin(pcap_reader_t *reader);

/* Reads the next record. On success *have_packet_out is true and buffer holds
 * up to buffer_capacity bytes of the frame; PCAP_READER_LIMIT_REACHED means the
 * frame was longer than the buffer and only its prefix was copied. At the end
 * of the file the call returns PCAP_READER_OK with *have_packet_out false. */
pcap_reader_status_t pcap_reader_iterate_next(pcap_reader_t *reader,
                                              pcap_packet_index_t *packet_out,
                                              uint8_t *buffer,
                                              size_t buffer_capacity,
                                              size_t *bytes_read_out,
                                              bool *have_packet_out);

/* Decodes a frame that the caller already holds in memory. */
void pcap_reader_describe_bytes(uint32_t link_type, const uint8_t *data, size_t length,
                                bool payload_truncated,
                                pcap_packet_details_t *details_out);

uint32_t pcap_reader_link_type(const pcap_reader_t *reader);
pcap_timestamp_resolution_t pcap_reader_timestamp_resolution(const pcap_reader_t *reader);

/* Record timestamp in microseconds since the epoch. Nanosecond captures are
 * reduced to microseconds, which is the resolution every aggregate uses. */
uint64_t pcap_reader_packet_time_us(const pcap_packet_index_t *packet,
                                    pcap_timestamp_resolution_t resolution);

const char *pcap_reader_status_name(pcap_reader_status_t status);
const char *pcap_reader_link_type_name(uint32_t link_type);
const char *pcap_reader_filter_name(pcap_packet_filter_t filter);
uint32_t pcap_reader_filter_flag(pcap_packet_filter_t filter);
bool pcap_reader_packet_matches_filter(uint32_t packet_flags,
                                       pcap_packet_filter_t filter);
const char *pcap_reader_dns_type_name(uint16_t qtype);
const char *pcap_reader_dns_rcode_name(uint8_t rcode);

#ifdef __cplusplus
}
#endif
