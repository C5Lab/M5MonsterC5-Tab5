#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pcap_reader.h"
#include "pcap_summary.h"
#include "pcap_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCAP_ANALYSIS_STORE_PATH_MAX 384U
#define PCAP_ANALYSIS_CACHE_SCHEMA_VERSION 4U
#define PCAP_ANALYSIS_REPORT_SCHEMA_VERSION 4U
#define PCAP_ANALYSIS_FILTER_SCHEMA_VERSION 2U

typedef enum {
    PCAP_ANALYSIS_STORE_OK = 0,
    PCAP_ANALYSIS_STORE_NOT_FOUND,
    PCAP_ANALYSIS_STORE_STALE,
    PCAP_ANALYSIS_STORE_INVALID,
    PCAP_ANALYSIS_STORE_IO_ERROR,
    PCAP_ANALYSIS_STORE_LIMIT,
} pcap_analysis_store_status_t;

typedef struct {
    bool valid;
    uint16_t schema_version;
    uint32_t indexed_packets;
    uint64_t source_size;
    int64_t source_mtime;
    char cache_path[PCAP_ANALYSIS_STORE_PATH_MAX];
} pcap_analysis_cache_info_t;

pcap_analysis_store_status_t pcap_analysis_cache_probe(
    const char *source_path,
    uint32_t index_capacity,
    pcap_analysis_cache_info_t *info_out);

pcap_analysis_store_status_t pcap_analysis_cache_load(
    const char *source_path,
    uint32_t index_capacity,
    pcap_capture_info_t *capture_info_out,
    pcap_scan_summary_t *scan_summary_out,
    pcap_packet_index_t *packet_index_out,
    size_t packet_index_capacity,
    uint32_t *packet_flags_out,
    size_t packet_flags_capacity,
    pcap_summary_t *summary_out,
    pcap_flow_analysis_t *flow_analysis_out,
    pcap_analysis_cache_info_t *info_out);

pcap_analysis_store_status_t pcap_analysis_cache_save(
    const char *source_path,
    uint32_t index_capacity,
    const pcap_capture_info_t *capture_info,
    const pcap_scan_summary_t *scan_summary,
    const pcap_packet_index_t *packet_index,
    const uint32_t *packet_flags,
    const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis,
    pcap_analysis_cache_info_t *info_out);

pcap_analysis_store_status_t pcap_analysis_cache_delete(const char *source_path);

pcap_analysis_store_status_t pcap_analysis_export_report_json(
    const char *source_path,
    const pcap_capture_info_t *capture_info,
    const pcap_scan_summary_t *scan_summary,
    const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis,
    const uint32_t *packet_flags,
    size_t packet_flag_count,
    pcap_packet_filter_t active_filter,
    const pcap_flow_filter_t *quick_filter,
    uint32_t selected_matches,
    bool loaded_from_cache,
    char *output_path,
    size_t output_path_size);

pcap_analysis_store_status_t pcap_analysis_export_filtered_pcap(
    const char *source_path,
    const pcap_packet_index_t *packet_index,
    const uint32_t *packet_flags,
    size_t packet_count,
    pcap_packet_filter_t active_filter,
    const uint8_t *packet_selection,
    size_t packet_selection_count,
    char *output_path,
    size_t output_path_size,
    uint32_t *exported_packets_out);

pcap_analysis_store_status_t pcap_analysis_filter_profile_save(
    pcap_packet_filter_t active_filter,
    const pcap_flow_filter_t *quick_filter,
    char *output_path,
    size_t output_path_size);

pcap_analysis_store_status_t pcap_analysis_filter_profile_load(
    pcap_packet_filter_t *active_filter_out,
    pcap_flow_filter_t *quick_filter_out,
    char *input_path,
    size_t input_path_size);

const char *pcap_analysis_store_status_name(pcap_analysis_store_status_t status);

#ifdef __cplusplus
}
#endif
