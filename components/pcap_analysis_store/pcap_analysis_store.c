#include "pcap_analysis_store.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ESPC_CACHE_ROOT "/sdcard/lab/pcaps/.espshark/cache"
#define ESPC_EXPORT_ROOT "/sdcard/lab/espshark/exports"
#define ESPC_PROFILE_ROOT "/sdcard/lab/espshark/profiles"
#define ESPC_LAST_PROFILE ESPC_PROFILE_ROOT "/last.espfilter.json"
#define ESPC_CACHE_MAGIC "ESPCACH1"
#define ESPC_CACHE_MAGIC_SIZE 8U
#define ESPC_COPY_BUFFER_SIZE 4096U
#define ESPC_QUICK_HASH_SIZE 4096U

typedef struct __attribute__((packed)) {
    char magic[ESPC_CACHE_MAGIC_SIZE];
    uint16_t schema_version;
    uint16_t header_size;
    uint32_t header_crc32;
    uint32_t payload_crc32;
    uint32_t source_path_hash;
    uint32_t source_quick_crc32;
    uint64_t source_size;
    int64_t source_mtime;
    uint32_t analyzer_index_capacity;
    uint32_t indexed_packets;
    uint32_t capture_info_size;
    uint32_t scan_summary_size;
    uint32_t packet_index_entry_size;
    uint32_t packet_flag_entry_size;
    uint32_t summary_size;
    uint32_t flow_analysis_size;
} espc_cache_header_t;

static uint32_t espc_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return crc;
}

static uint32_t espc_crc32(const void *data, size_t length)
{
    return ~espc_crc32_update(0xFFFFFFFFU, (const uint8_t *)data, length);
}

static uint32_t espc_hash_path(const char *path)
{
    uint32_t hash = 2166136261U;
    if (!path) {
        return hash;
    }
    while (*path) {
        unsigned char value = (unsigned char)*path++;
        if (value == '\\') value = '/';
        hash ^= (uint32_t)value;
        hash *= 16777619U;
    }
    return hash;
}

static bool espc_mkdir_one(const char *path)
{
    if (mkdir(path, 0775) == 0) {
        return true;
    }
    struct stat item;
    return stat(path, &item) == 0 && S_ISDIR(item.st_mode);
}

static bool espc_prepare_cache_dirs(void)
{
    return espc_mkdir_one("/sdcard/lab/pcaps/.espshark") &&
           espc_mkdir_one(ESPC_CACHE_ROOT);
}

static bool espc_prepare_artifact_dirs(void)
{
    return espc_mkdir_one("/sdcard/lab/espshark") &&
           espc_mkdir_one(ESPC_EXPORT_ROOT) &&
           espc_mkdir_one(ESPC_PROFILE_ROOT);
}

static const char *espc_basename(const char *path)
{
    const char *base = path ? strrchr(path, '/') : NULL;
    return base ? base + 1 : (path ? path : "capture");
}

static void espc_safe_stem(const char *path, char *output, size_t output_size)
{
    if (!output || output_size == 0) return;
    const char *base = espc_basename(path);
    size_t position = 0;
    while (*base && *base != '.' && position + 1U < output_size) {
        unsigned char c = (unsigned char)*base++;
        output[position++] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    if (position == 0) {
        snprintf(output, output_size, "capture");
    } else {
        output[position] = '\0';
    }
}

static bool espc_build_cache_path(const char *source_path, char *output, size_t output_size)
{
    char stem[64];
    espc_safe_stem(source_path, stem, sizeof(stem));
    int written = snprintf(output, output_size, "%s/%08lX_%s.espcache",
                           ESPC_CACHE_ROOT, (unsigned long)espc_hash_path(source_path), stem);
    return written > 0 && (size_t)written < output_size;
}

static bool espc_source_identity(const char *source_path, struct stat *source_stat,
                                 uint32_t *quick_crc_out)
{
    if (!source_path || !source_stat || stat(source_path, source_stat) != 0 ||
        !S_ISREG(source_stat->st_mode) || source_stat->st_size < 24 ||
        (uint64_t)source_stat->st_size > (uint64_t)LONG_MAX) {
        return false;
    }
    if (!quick_crc_out) {
        return true;
    }

    FILE *source = fopen(source_path, "rb");
    if (!source) return false;
    uint8_t buffer[ESPC_QUICK_HASH_SIZE];
    uint32_t crc = 0xFFFFFFFFU;
    size_t first_len = fread(buffer, 1, sizeof(buffer), source);
    if (first_len == 0 || ferror(source)) {
        fclose(source);
        return false;
    }
    crc = espc_crc32_update(crc, buffer, first_len);

    if ((uint64_t)source_stat->st_size > ESPC_QUICK_HASH_SIZE) {
        long tail_offset = source_stat->st_size > (off_t)ESPC_QUICK_HASH_SIZE
                               ? (long)(source_stat->st_size - (off_t)ESPC_QUICK_HASH_SIZE) : 0;
        if (fseek(source, tail_offset, SEEK_SET) != 0) {
            fclose(source);
            return false;
        }
        size_t tail_len = fread(buffer, 1, sizeof(buffer), source);
        if (tail_len == 0 || ferror(source)) {
            fclose(source);
            return false;
        }
        crc = espc_crc32_update(crc, buffer, tail_len);
    }
    fclose(source);
    crc = espc_crc32_update(crc, (const uint8_t *)&source_stat->st_size,
                            sizeof(source_stat->st_size));
    *quick_crc_out = ~crc;
    return true;
}

static uint32_t espc_header_crc(espc_cache_header_t *header)
{
    uint32_t saved = header->header_crc32;
    header->header_crc32 = 0;
    uint32_t crc = espc_crc32(header, sizeof(*header));
    header->header_crc32 = saved;
    return crc;
}

static bool espc_header_layout_valid(const espc_cache_header_t *header,
                                     uint32_t index_capacity)
{
    return header && memcmp(header->magic, ESPC_CACHE_MAGIC, ESPC_CACHE_MAGIC_SIZE) == 0 &&
           header->schema_version == PCAP_ANALYSIS_CACHE_SCHEMA_VERSION &&
           header->header_size == sizeof(espc_cache_header_t) &&
           header->analyzer_index_capacity == index_capacity &&
           header->indexed_packets <= index_capacity &&
           header->capture_info_size == sizeof(pcap_capture_info_t) &&
           header->scan_summary_size == sizeof(pcap_scan_summary_t) &&
           header->packet_index_entry_size == sizeof(pcap_packet_index_t) &&
           header->packet_flag_entry_size == sizeof(uint32_t) &&
           header->summary_size == sizeof(pcap_summary_t) &&
           header->flow_analysis_size == sizeof(pcap_flow_analysis_t);
}

static pcap_analysis_store_status_t espc_read_valid_header(
    const char *source_path, uint32_t index_capacity, bool verify_quick_crc,
    espc_cache_header_t *header_out, char *cache_path, size_t cache_path_size)
{
    if (!source_path || !header_out ||
        !espc_build_cache_path(source_path, cache_path, cache_path_size)) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    struct stat source_stat;
    uint32_t quick_crc = 0;
    if (!espc_source_identity(source_path, &source_stat,
                              verify_quick_crc ? &quick_crc : NULL)) {
        return PCAP_ANALYSIS_STORE_NOT_FOUND;
    }
    FILE *cache = fopen(cache_path, "rb");
    if (!cache) return PCAP_ANALYSIS_STORE_NOT_FOUND;
    espc_cache_header_t header;
    bool read_ok = fread(&header, 1, sizeof(header), cache) == sizeof(header);
    fclose(cache);
    if (!read_ok || !espc_header_layout_valid(&header, index_capacity) ||
        espc_header_crc(&header) != header.header_crc32) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    if (header.source_path_hash != espc_hash_path(source_path) ||
        header.source_size != (uint64_t)source_stat.st_size ||
        header.source_mtime != (int64_t)source_stat.st_mtime ||
        (verify_quick_crc && header.source_quick_crc32 != quick_crc)) {
        return PCAP_ANALYSIS_STORE_STALE;
    }
    *header_out = header;
    return PCAP_ANALYSIS_STORE_OK;
}

static void espc_fill_cache_info(const espc_cache_header_t *header, const char *path,
                                 pcap_analysis_cache_info_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->valid = true;
    info->schema_version = header->schema_version;
    info->indexed_packets = header->indexed_packets;
    info->source_size = header->source_size;
    info->source_mtime = header->source_mtime;
    size_t path_length = path ? strnlen(path, sizeof(info->cache_path) - 1U) : 0U;
    if (path_length > 0U) memcpy(info->cache_path, path, path_length);
    info->cache_path[path_length] = '\0';
}

pcap_analysis_store_status_t pcap_analysis_cache_probe(
    const char *source_path, uint32_t index_capacity, pcap_analysis_cache_info_t *info_out)
{
    if (info_out) memset(info_out, 0, sizeof(*info_out));
    espc_cache_header_t header;
    char cache_path[PCAP_ANALYSIS_STORE_PATH_MAX];
    pcap_analysis_store_status_t status = espc_read_valid_header(
        source_path, index_capacity, false, &header, cache_path, sizeof(cache_path));
    if (status == PCAP_ANALYSIS_STORE_OK) {
        espc_fill_cache_info(&header, cache_path, info_out);
    }
    return status;
}

static bool espc_read_crc(FILE *file, void *output, size_t size, uint32_t *crc)
{
    if (size == 0) return true;
    if (fread(output, 1, size, file) != size) return false;
    *crc = espc_crc32_update(*crc, (const uint8_t *)output, size);
    return true;
}

pcap_analysis_store_status_t pcap_analysis_cache_load(
    const char *source_path, uint32_t index_capacity,
    pcap_capture_info_t *capture_info_out, pcap_scan_summary_t *scan_summary_out,
    pcap_packet_index_t *packet_index_out, size_t packet_index_capacity,
    uint32_t *packet_flags_out, size_t packet_flags_capacity,
    pcap_summary_t *summary_out, pcap_flow_analysis_t *flow_analysis_out,
    pcap_analysis_cache_info_t *info_out)
{
    if (!capture_info_out || !scan_summary_out || !packet_index_out ||
        !packet_flags_out || !summary_out || !flow_analysis_out) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    if (info_out) memset(info_out, 0, sizeof(*info_out));
    espc_cache_header_t header;
    char cache_path[PCAP_ANALYSIS_STORE_PATH_MAX];
    pcap_analysis_store_status_t status = espc_read_valid_header(
        source_path, index_capacity, true, &header, cache_path, sizeof(cache_path));
    if (status != PCAP_ANALYSIS_STORE_OK) return status;
    if (header.indexed_packets > packet_index_capacity ||
        header.indexed_packets > packet_flags_capacity) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }

    FILE *cache = fopen(cache_path, "rb");
    if (!cache) return PCAP_ANALYSIS_STORE_NOT_FOUND;
    if (fseek(cache, (long)sizeof(header), SEEK_SET) != 0) {
        fclose(cache);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    uint32_t crc = 0xFFFFFFFFU;
    size_t index_bytes = (size_t)header.indexed_packets * sizeof(pcap_packet_index_t);
    size_t flag_bytes = (size_t)header.indexed_packets * sizeof(uint32_t);
    bool ok = espc_read_crc(cache, capture_info_out, sizeof(*capture_info_out), &crc) &&
              espc_read_crc(cache, scan_summary_out, sizeof(*scan_summary_out), &crc) &&
              espc_read_crc(cache, packet_index_out, index_bytes, &crc) &&
              espc_read_crc(cache, packet_flags_out, flag_bytes, &crc) &&
              espc_read_crc(cache, summary_out, sizeof(*summary_out), &crc) &&
              espc_read_crc(cache, flow_analysis_out, sizeof(*flow_analysis_out), &crc);
    int extra = fgetc(cache);
    fclose(cache);
    if (!ok || extra != EOF || ~crc != header.payload_crc32 ||
        scan_summary_out->indexed_packets != header.indexed_packets ||
        scan_summary_out->packet_count < header.indexed_packets ||
        capture_info_out->file_size != header.source_size ||
        summary_out->analyzed_packets != header.indexed_packets ||
        summary_out->protocol_count > PCAP_SUMMARY_MAX_PROTOCOLS ||
        summary_out->endpoint_count > PCAP_SUMMARY_MAX_ENDPOINTS ||
        summary_out->port_count > PCAP_SUMMARY_MAX_PORTS ||
        summary_out->dns_domain_count > PCAP_SUMMARY_MAX_DNS_DOMAINS ||
        summary_out->dns_answer_count > PCAP_SUMMARY_MAX_DNS_ANSWERS ||
        summary_out->dns_client_count > PCAP_SUMMARY_MAX_DNS_PEERS ||
        summary_out->dns_server_count > PCAP_SUMMARY_MAX_DNS_PEERS ||
        summary_out->dns_type_count > PCAP_SUMMARY_MAX_DNS_TYPES ||
        flow_analysis_out->analyzed_packets != header.indexed_packets ||
        flow_analysis_out->flow_count > PCAP_FLOW_MAX_FLOWS ||
        flow_analysis_out->device_count > PCAP_FLOW_MAX_DEVICES ||
        flow_analysis_out->alert_count > PCAP_FLOW_MAX_ALERTS ||
        flow_analysis_out->health_level > PCAP_HEALTH_CRITICAL) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    for (uint32_t i = 0; i < header.indexed_packets; i++) {
        if (packet_index_out[i].data_offset < 40U ||
            packet_index_out[i].data_offset > header.source_size ||
            packet_index_out[i].captured_length >
                header.source_size - packet_index_out[i].data_offset) {
            return PCAP_ANALYSIS_STORE_INVALID;
        }
        uint16_t flow_id = flow_analysis_out->packet_flow_id[i];
        if ((flow_id != PCAP_FLOW_ID_NONE && flow_id >= flow_analysis_out->flow_count) ||
            flow_analysis_out->packet_application[i] >= PCAP_APP_COUNT ||
            flow_analysis_out->packet_confidence[i] > PCAP_APP_CONFIDENCE_CONFIRMED ||
            flow_analysis_out->packet_direction[i] > 2U) {
            return PCAP_ANALYSIS_STORE_INVALID;
        }
    }
    for (uint32_t i = 0; i < flow_analysis_out->flow_count; i++) {
        if (flow_analysis_out->flows[i].app_protocol >= PCAP_APP_COUNT ||
            flow_analysis_out->flows[i].app_confidence >
                PCAP_APP_CONFIDENCE_CONFIRMED) {
            return PCAP_ANALYSIS_STORE_INVALID;
        }
    }
    for (uint32_t i = 0; i < flow_analysis_out->device_count; i++) {
        if (flow_analysis_out->devices[i].service_count > PCAP_FLOW_DEVICE_SERVICES) {
            return PCAP_ANALYSIS_STORE_INVALID;
        }
    }
    for (uint32_t i = 0; i < flow_analysis_out->alert_count; i++) {
        const pcap_security_alert_t *alert = &flow_analysis_out->alerts[i];
        if (alert->type > PCAP_ALERT_TCP_QUALITY ||
            alert->severity > PCAP_HEALTH_CRITICAL ||
            alert->confidence > PCAP_APP_CONFIDENCE_CONFIRMED ||
            alert->evidence_count > PCAP_FLOW_ALERT_EVIDENCE ||
            (alert->flow_id != PCAP_FLOW_ID_NONE &&
             alert->flow_id >= flow_analysis_out->flow_count)) {
            return PCAP_ANALYSIS_STORE_INVALID;
        }
    }
    espc_fill_cache_info(&header, cache_path, info_out);
    return PCAP_ANALYSIS_STORE_OK;
}

static bool espc_write_crc(FILE *file, const void *data, size_t size, uint32_t *crc)
{
    if (size == 0) return true;
    if (fwrite(data, 1, size, file) != size) return false;
    *crc = espc_crc32_update(*crc, (const uint8_t *)data, size);
    return true;
}

pcap_analysis_store_status_t pcap_analysis_cache_save(
    const char *source_path, uint32_t index_capacity,
    const pcap_capture_info_t *capture_info, const pcap_scan_summary_t *scan_summary,
    const pcap_packet_index_t *packet_index, const uint32_t *packet_flags,
    const pcap_summary_t *summary, const pcap_flow_analysis_t *flow_analysis,
    pcap_analysis_cache_info_t *info_out)
{
    if (!source_path || !capture_info || !scan_summary || !packet_index ||
        !packet_flags || !summary || !flow_analysis ||
        scan_summary->indexed_packets > index_capacity ||
        summary->analyzed_packets != scan_summary->indexed_packets ||
        flow_analysis->analyzed_packets != scan_summary->indexed_packets ||
        flow_analysis->flow_count > PCAP_FLOW_MAX_FLOWS) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    if (info_out) memset(info_out, 0, sizeof(*info_out));
    if (!espc_prepare_cache_dirs()) return PCAP_ANALYSIS_STORE_IO_ERROR;

    struct stat source_stat;
    uint32_t quick_crc = 0;
    if (!espc_source_identity(source_path, &source_stat, &quick_crc)) {
        return PCAP_ANALYSIS_STORE_NOT_FOUND;
    }
    char cache_path[PCAP_ANALYSIS_STORE_PATH_MAX];
    char temp_path[PCAP_ANALYSIS_STORE_PATH_MAX + 8U];
    if (!espc_build_cache_path(source_path, cache_path, sizeof(cache_path)) ||
        snprintf(temp_path, sizeof(temp_path), "%s.tmp", cache_path) >= (int)sizeof(temp_path)) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }

    espc_cache_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, ESPC_CACHE_MAGIC, ESPC_CACHE_MAGIC_SIZE);
    header.schema_version = PCAP_ANALYSIS_CACHE_SCHEMA_VERSION;
    header.header_size = sizeof(header);
    header.source_path_hash = espc_hash_path(source_path);
    header.source_quick_crc32 = quick_crc;
    header.source_size = (uint64_t)source_stat.st_size;
    header.source_mtime = (int64_t)source_stat.st_mtime;
    header.analyzer_index_capacity = index_capacity;
    header.indexed_packets = scan_summary->indexed_packets;
    header.capture_info_size = sizeof(*capture_info);
    header.scan_summary_size = sizeof(*scan_summary);
    header.packet_index_entry_size = sizeof(*packet_index);
    header.packet_flag_entry_size = sizeof(*packet_flags);
    header.summary_size = sizeof(*summary);
    header.flow_analysis_size = sizeof(*flow_analysis);

    FILE *cache = fopen(temp_path, "wb");
    if (!cache) return PCAP_ANALYSIS_STORE_IO_ERROR;
    bool ok = fwrite(&header, 1, sizeof(header), cache) == sizeof(header);
    uint32_t payload_crc = 0xFFFFFFFFU;
    size_t index_bytes = (size_t)scan_summary->indexed_packets * sizeof(*packet_index);
    size_t flag_bytes = (size_t)scan_summary->indexed_packets * sizeof(*packet_flags);
    ok = ok && espc_write_crc(cache, capture_info, sizeof(*capture_info), &payload_crc) &&
         espc_write_crc(cache, scan_summary, sizeof(*scan_summary), &payload_crc) &&
         espc_write_crc(cache, packet_index, index_bytes, &payload_crc) &&
         espc_write_crc(cache, packet_flags, flag_bytes, &payload_crc) &&
         espc_write_crc(cache, summary, sizeof(*summary), &payload_crc) &&
         espc_write_crc(cache, flow_analysis, sizeof(*flow_analysis), &payload_crc);
    header.payload_crc32 = ~payload_crc;
    header.header_crc32 = espc_header_crc(&header);
    if (ok && fseek(cache, 0, SEEK_SET) == 0) {
        ok = fwrite(&header, 1, sizeof(header), cache) == sizeof(header);
    } else {
        ok = false;
    }
    if (ok) ok = fflush(cache) == 0;
    if (ok) {
        int fd = fileno(cache);
        if (fd >= 0) ok = fsync(fd) == 0;
    }
    if (fclose(cache) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    remove(cache_path);
    if (rename(temp_path, cache_path) != 0) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    espc_fill_cache_info(&header, cache_path, info_out);
    return PCAP_ANALYSIS_STORE_OK;
}

pcap_analysis_store_status_t pcap_analysis_cache_delete(const char *source_path)
{
    char cache_path[PCAP_ANALYSIS_STORE_PATH_MAX];
    if (!source_path || !espc_build_cache_path(source_path, cache_path, sizeof(cache_path))) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    if (remove(cache_path) == 0) return PCAP_ANALYSIS_STORE_OK;
    return errno == ENOENT ? PCAP_ANALYSIS_STORE_NOT_FOUND : PCAP_ANALYSIS_STORE_IO_ERROR;
}

static bool espc_build_unique_artifact_path(const char *source_path, const char *suffix,
                                            char *output, size_t output_size)
{
    char stem[64];
    espc_safe_stem(source_path, stem, sizeof(stem));
    time_t now = time(NULL);
    struct tm local;
    bool have_time = now >= 1704067200 && localtime_r(&now, &local) != NULL;
    char stamp[32];
    if (have_time) {
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        snprintf(stamp, sizeof(stamp), "session");
    }
    for (int sequence = 0; sequence < 1000; sequence++) {
        int written = sequence == 0
                          ? snprintf(output, output_size, "%s/%s_%s%s",
                                     ESPC_EXPORT_ROOT, stem, stamp, suffix ? suffix : "")
                          : snprintf(output, output_size, "%s/%s_%s_%d%s",
                                     ESPC_EXPORT_ROOT, stem, stamp, sequence,
                                     suffix ? suffix : "");
        if (written <= 0 || (size_t)written >= output_size) return false;
        struct stat item;
        if (stat(output, &item) != 0) return true;
    }
    return false;
}

static void espc_json_string(FILE *file, const char *value)
{
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++) {
        switch (*p) {
            case '"': fputs("\\\"", file); break;
            case '\\': fputs("\\\\", file); break;
            case '\b': fputs("\\b", file); break;
            case '\f': fputs("\\f", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default:
                if (*p < 0x20) fprintf(file, "\\u%04x", (unsigned)*p);
                else fputc(*p, file);
                break;
        }
    }
    fputc('"', file);
}

static void espc_json_text_counters(FILE *file, const pcap_summary_text_counter_t *items,
                                    uint16_t count)
{
    fputc('[', file);
    for (uint16_t i = 0; i < count; i++) {
        if (i) fputc(',', file);
        fputs("{\"label\":", file);
        espc_json_string(file, items[i].label);
        fprintf(file, ",\"count\":%llu,\"bytes\":%llu}",
                (unsigned long long)items[i].count,
                (unsigned long long)items[i].bytes);
    }
    fputc(']', file);
}

pcap_analysis_store_status_t pcap_analysis_export_report_json(
    const char *source_path, const pcap_capture_info_t *capture_info,
    const pcap_scan_summary_t *scan_summary, const pcap_summary_t *summary,
    const pcap_flow_analysis_t *flow_analysis, const uint32_t *packet_flags,
    size_t packet_flag_count,
    pcap_packet_filter_t active_filter, const pcap_flow_filter_t *quick_filter,
    uint32_t selected_matches, bool loaded_from_cache,
    char *output_path, size_t output_path_size)
{
    if (!source_path || !capture_info || !scan_summary || !summary ||
        !flow_analysis || !packet_flags || !quick_filter ||
        active_filter < PCAP_FILTER_ALL || active_filter >= PCAP_FILTER_COUNT ||
        !output_path || output_path_size == 0 || !espc_prepare_artifact_dirs() ||
        !espc_build_unique_artifact_path(source_path, ".espreport.json",
                                         output_path, output_path_size)) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    char temp_path[PCAP_ANALYSIS_STORE_PATH_MAX + 8U];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_path) >= (int)sizeof(temp_path)) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }
    struct stat source_stat;
    uint32_t quick_crc = 0;
    if (!espc_source_identity(source_path, &source_stat, &quick_crc)) {
        return PCAP_ANALYSIS_STORE_NOT_FOUND;
    }
    uint32_t matches = 0;
    size_t count = packet_flag_count < scan_summary->indexed_packets
                       ? packet_flag_count : scan_summary->indexed_packets;
    for (size_t i = 0; i < count; i++) {
        if (pcap_reader_packet_matches_filter(packet_flags[i], active_filter)) matches++;
    }

    FILE *report = fopen(temp_path, "wb");
    if (!report) return PCAP_ANALYSIS_STORE_IO_ERROR;
    fprintf(report,
            "{\n  \"schema\":\"espshark-analysis\",\n  \"schema_version\":%u,\n",
            PCAP_ANALYSIS_REPORT_SCHEMA_VERSION);
    fprintf(report, "  \"generated_at_epoch\":%lld,\n", (long long)time(NULL));
    fputs("  \"source\":{\"path\":", report);
    espc_json_string(report, source_path);
    fprintf(report, ",\"size\":%llu,\"mtime\":%lld,\"quick_crc32\":\"%08lX\"},\n",
            (unsigned long long)source_stat.st_size, (long long)source_stat.st_mtime,
            (unsigned long)quick_crc);
    fprintf(report,
            "  \"capture\":{\"version\":\"%u.%u\",\"link_type\":%lu,\"snaplen\":%lu,"
            "\"big_endian\":%s,\"timestamp_resolution\":\"%s\"},\n",
            capture_info->version_major, capture_info->version_minor,
            (unsigned long)capture_info->link_type, (unsigned long)capture_info->snaplen,
            capture_info->big_endian ? "true" : "false",
            capture_info->timestamp_resolution == PCAP_TIMESTAMP_NANOSECONDS ? "ns" : "us");
    fprintf(report,
            "  \"analysis\":{\"total_packets\":%llu,\"indexed_packets\":%lu,"
            "\"captured_bytes\":%llu,\"index_limited\":%s,\"truncated_tail\":%s,"
            "\"loaded_from_cache\":%s,\"active_filter\":",
            (unsigned long long)scan_summary->packet_count,
            (unsigned long)scan_summary->indexed_packets,
            (unsigned long long)scan_summary->captured_bytes,
            scan_summary->index_limited ? "true" : "false",
            scan_summary->truncated_tail ? "true" : "false",
            loaded_from_cache ? "true" : "false");
    espc_json_string(report, pcap_reader_filter_name(active_filter));
    char quick_description[160];
    pcap_flow_filter_describe(quick_filter, quick_description, sizeof(quick_description));
    fputs(",\"quick_filter\":", report);
    espc_json_string(report, quick_description);
    fprintf(report, ",\"protocol_filter_matches\":%lu,\"selected_matches\":%lu},\n",
            (unsigned long)matches, (unsigned long)selected_matches);
    fputs("  \"protocols\":", report);
    espc_json_text_counters(report, summary->protocols, summary->protocol_count);
    fputs(",\n  \"endpoints\":", report);
    espc_json_text_counters(report, summary->endpoints, summary->endpoint_count);
    fputs(",\n  \"ports\":[", report);
    for (uint16_t i = 0; i < summary->port_count; i++) {
        if (i) fputc(',', report);
        fprintf(report, "{\"transport\":\"%s\",\"port\":%u,\"count\":%llu}",
                summary->ports[i].ip_protocol == 6 ? "TCP" : "UDP",
                summary->ports[i].port, (unsigned long long)summary->ports[i].count);
    }
    fputs("],\n  \"dns\":{", report);
    fprintf(report,
            "\"packets\":%llu,\"queries\":%llu,\"responses\":%llu,"
            "\"nxdomain\":%llu,\"servfail\":%llu,\"unique_domains_observed\":%llu,",
            (unsigned long long)summary->dns_packets,
            (unsigned long long)summary->dns_queries,
            (unsigned long long)summary->dns_responses,
            (unsigned long long)summary->dns_nxdomain,
            (unsigned long long)summary->dns_servfail,
            (unsigned long long)summary->dns_unique_domains_observed);
    fputs("\"domains\":", report);
    espc_json_text_counters(report, summary->dns_domains, summary->dns_domain_count);
    fputs(",\"answers\":", report);
    espc_json_text_counters(report, summary->dns_answers, summary->dns_answer_count);
    fputs(",\"clients\":", report);
    espc_json_text_counters(report, summary->dns_clients, summary->dns_client_count);
    fputs(",\"servers\":", report);
    espc_json_text_counters(report, summary->dns_servers, summary->dns_server_count);
    fputs(",\"query_types\":", report);
    espc_json_text_counters(report, summary->dns_types, summary->dns_type_count);
    fputs("},\n  \"applications\":[", report);
    bool first_application = true;
    for (uint8_t app = 0; app < PCAP_APP_COUNT; app++) {
        const pcap_app_summary_t *item = &flow_analysis->applications[app];
        if (item->packets == 0 && item->flows == 0) continue;
        if (!first_application) fputc(',', report);
        first_application = false;
        fputs("{\"name\":", report);
        espc_json_string(report, pcap_flow_application_name((pcap_application_t)app));
        fprintf(report,
                ",\"packets\":%llu,\"bytes\":%llu,\"flows\":%lu,"
                "\"confirmed_packets\":%lu,\"likely_packets\":%lu,"
                "\"confirmed_flows\":%lu,\"likely_flows\":%lu}",
                (unsigned long long)item->packets,
                (unsigned long long)item->bytes,
                (unsigned long)item->flows,
                (unsigned long)item->confirmed_packets,
                (unsigned long)item->likely_packets,
                (unsigned long)item->confirmed_flows,
                (unsigned long)item->likely_flows);
    }
    fputs("],\n  \"connections\":[", report);
    for (uint32_t i = 0; i < flow_analysis->flow_count; i++) {
        const pcap_flow_entry_t *flow = &flow_analysis->flows[i];
        if (i) fputc(',', report);
        fprintf(report, "{\"id\":%lu,\"originator\":", (unsigned long)i + 1U);
        espc_json_string(report, flow->originator);
        fprintf(report, ",\"originator_port\":%u,\"responder\":", flow->originator_port);
        espc_json_string(report, flow->responder);
        fprintf(report,
                ",\"responder_port\":%u,\"transport\":\"%s\",\"application\":",
                flow->responder_port, pcap_flow_transport_name(flow->ip_protocol));
        espc_json_string(report,
                         pcap_flow_application_name((pcap_application_t)flow->app_protocol));
        fputs(",\"confidence\":", report);
        espc_json_string(report,
                         pcap_flow_confidence_name(
                             (pcap_app_confidence_t)flow->app_confidence));
        fputs(",\"server_name\":", report);
        espc_json_string(report, flow->server_name);
        fputs(",\"application_detail\":", report);
        espc_json_string(report, flow->application_detail);
        uint64_t duration_us = flow->last_time_us >= flow->first_time_us
                                   ? flow->last_time_us - flow->first_time_us : 0;
        fprintf(report,
                ",\"first_packet\":%lu,\"last_packet\":%lu,"
                "\"duration_us\":%llu,\"tls_version\":%u,\"originator_packets\":%lu,"
                "\"responder_packets\":%lu,\"originator_bytes\":%llu,"
                "\"responder_bytes\":%llu,\"handshake_rtt_us\":%llu,"
                "\"originator_retransmissions\":%lu,"
                "\"responder_retransmissions\":%lu,\"zero_window_packets\":%lu}",
                (unsigned long)flow->first_packet + 1U,
                (unsigned long)flow->last_packet + 1U,
                (unsigned long long)duration_us,
                flow->tls_version,
                (unsigned long)flow->originator_packets,
                (unsigned long)flow->responder_packets,
                (unsigned long long)flow->originator_bytes,
                (unsigned long long)flow->responder_bytes,
                (unsigned long long)flow->handshake_rtt_us,
                (unsigned long)flow->originator_retransmissions,
                (unsigned long)flow->responder_retransmissions,
                (unsigned long)flow->zero_window_packets);
    }
    fprintf(report,
            "],\n  \"flow_limits\":{\"limited\":%s,\"overflow_packets\":%lu,"
            "\"devices_limited\":%s,\"alerts_limited\":%s},\n",
            flow_analysis->flow_limited ? "true" : "false",
            (unsigned long)flow_analysis->overflow_packets,
            flow_analysis->device_limited ? "true" : "false",
            flow_analysis->alert_limited ? "true" : "false");
    fputs("  \"devices\":[", report);
    for (uint32_t i = 0; i < flow_analysis->device_count; i++) {
        const pcap_device_entry_t *device = &flow_analysis->devices[i];
        if (i) fputc(',', report);
        fputs("{\"address\":", report);
        espc_json_string(report, device->address);
        fputs(",\"mac\":", report);
        espc_json_string(report, device->mac);
        fputs(",\"hostname\":", report);
        espc_json_string(report, device->hostname);
        fprintf(report,
                ",\"internal\":%s,\"first_time_us\":%llu,\"last_time_us\":%llu,"
                "\"sent_packets\":%lu,\"received_packets\":%lu,"
                "\"sent_bytes\":%llu,\"received_bytes\":%llu,\"services\":[",
                device->internal ? "true" : "false",
                (unsigned long long)device->first_time_us,
                (unsigned long long)device->last_time_us,
                (unsigned long)device->sent_packets,
                (unsigned long)device->received_packets,
                (unsigned long long)device->sent_bytes,
                (unsigned long long)device->received_bytes);
        for (uint16_t service_index = 0; service_index < device->service_count;
             service_index++) {
            const pcap_device_service_t *service = &device->services[service_index];
            if (service_index) fputc(',', report);
            fprintf(report, "{\"transport\":\"%s\",\"port\":%u,\"application\":",
                    pcap_flow_transport_name(service->ip_protocol), service->port);
            espc_json_string(report,
                             pcap_flow_application_name(
                                 (pcap_application_t)service->application));
            fprintf(report, ",\"packets\":%lu,\"bytes\":%llu}",
                    (unsigned long)service->packets,
                    (unsigned long long)service->bytes);
        }
        fprintf(report, "],\"services_limited\":%s}",
                device->service_limited ? "true" : "false");
    }
    fputs("],\n  \"network_health\":{\"level\":", report);
    espc_json_string(report,
                     pcap_flow_health_name((pcap_health_level_t)flow_analysis->health_level));
    fprintf(report,
            ",\"analyzed_packets\":%lu,\"dns_packets\":%lu,"
            "\"dns_error_packets\":%lu,\"dns_suspicious_names\":%lu,"
            "\"broadcast_packets\":%lu},\n  \"alerts\":[",
            (unsigned long)flow_analysis->analyzed_packets,
            (unsigned long)flow_analysis->dns_packets,
            (unsigned long)flow_analysis->dns_error_packets,
            (unsigned long)flow_analysis->dns_suspicious_names,
            (unsigned long)flow_analysis->broadcast_packets);
    for (uint32_t i = 0; i < flow_analysis->alert_count; i++) {
        const pcap_security_alert_t *alert = &flow_analysis->alerts[i];
        if (i) fputc(',', report);
        fputs("{\"type\":", report);
        espc_json_string(report, pcap_flow_alert_name((pcap_alert_type_t)alert->type));
        fputs(",\"severity\":", report);
        espc_json_string(report, pcap_flow_health_name((pcap_health_level_t)alert->severity));
        fputs(",\"confidence\":", report);
        espc_json_string(report,
                         pcap_flow_confidence_name(
                             (pcap_app_confidence_t)alert->confidence));
        fputs(",\"source\":", report);
        espc_json_string(report, alert->source);
        fputs(",\"target\":", report);
        espc_json_string(report, alert->target);
        fputs(",\"detail\":", report);
        espc_json_string(report, alert->detail);
        fprintf(report,
                ",\"service_port\":%u,\"flow_id\":%u,\"first_packet\":%lu,"
                "\"last_packet\":%lu,\"first_time_us\":%llu,\"last_time_us\":%llu,"
                "\"evidence_packets\":[",
                alert->service_port,
                alert->flow_id == PCAP_FLOW_ID_NONE ? 0U : alert->flow_id + 1U,
                (unsigned long)alert->first_packet + 1U,
                (unsigned long)alert->last_packet + 1U,
                (unsigned long long)alert->first_time_us,
                (unsigned long long)alert->last_time_us);
        for (uint8_t evidence = 0; evidence < alert->evidence_count; evidence++) {
            if (evidence) fputc(',', report);
            fprintf(report, "%lu", (unsigned long)alert->evidence_packets[evidence] + 1U);
        }
        fputs("]}", report);
    }
    fputs("]\n}\n", report);

    bool ok = !ferror(report) && fflush(report) == 0;
    if (ok) {
        int fd = fileno(report);
        if (fd >= 0) ok = fsync(fd) == 0;
    }
    if (fclose(report) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    remove(output_path);
    if (rename(temp_path, output_path) != 0) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    return PCAP_ANALYSIS_STORE_OK;
}

pcap_analysis_store_status_t pcap_analysis_export_filtered_pcap(
    const char *source_path, const pcap_packet_index_t *packet_index,
    const uint32_t *packet_flags, size_t packet_count,
    pcap_packet_filter_t active_filter, const uint8_t *packet_selection,
    size_t packet_selection_count, char *output_path, size_t output_path_size,
    uint32_t *exported_packets_out)
{
    if (exported_packets_out) *exported_packets_out = 0;
    if (!source_path || !packet_index || !packet_flags || !output_path ||
        active_filter < PCAP_FILTER_ALL || active_filter >= PCAP_FILTER_COUNT ||
        !espc_prepare_artifact_dirs()) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    char suffix[64];
    snprintf(suffix, sizeof(suffix), "_%s.pcap", pcap_reader_filter_name(active_filter));
    for (char *p = suffix; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.')) *p = '_';
    }
    if (!espc_build_unique_artifact_path(source_path, suffix, output_path, output_path_size)) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }
    char temp_path[PCAP_ANALYSIS_STORE_PATH_MAX + 8U];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_path) >= (int)sizeof(temp_path)) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }
    FILE *source = fopen(source_path, "rb");
    FILE *output = source ? fopen(temp_path, "wb") : NULL;
    if (!source || !output) {
        if (source) fclose(source);
        if (output) fclose(output);
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    uint8_t buffer[ESPC_COPY_BUFFER_SIZE];
    bool ok = fread(buffer, 1, 24, source) == 24 && fwrite(buffer, 1, 24, output) == 24;
    uint32_t exported = 0;
    for (size_t i = 0; ok && i < packet_count; i++) {
        if (!pcap_reader_packet_matches_filter(packet_flags[i], active_filter)) continue;
        if (packet_selection &&
            (i >= packet_selection_count || packet_selection[i] == 0U)) continue;
        if (packet_index[i].data_offset < 40U ||
            packet_index[i].data_offset - 16U > (uint64_t)LONG_MAX) {
            ok = false;
            break;
        }
        uint64_t bytes_left = 16ULL + packet_index[i].captured_length;
        if (fseek(source, (long)(packet_index[i].data_offset - 16U), SEEK_SET) != 0) {
            ok = false;
            break;
        }
        while (bytes_left > 0) {
            size_t chunk = bytes_left < sizeof(buffer) ? (size_t)bytes_left : sizeof(buffer);
            if (fread(buffer, 1, chunk, source) != chunk ||
                fwrite(buffer, 1, chunk, output) != chunk) {
                ok = false;
                break;
            }
            bytes_left -= chunk;
        }
        if (ok) exported++;
    }
    if (ok) ok = fflush(output) == 0;
    if (ok) {
        int fd = fileno(output);
        if (fd >= 0) ok = fsync(fd) == 0;
    }
    if (fclose(source) != 0) ok = false;
    if (fclose(output) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    remove(output_path);
    if (rename(temp_path, output_path) != 0) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    if (exported_packets_out) *exported_packets_out = exported;
    return PCAP_ANALYSIS_STORE_OK;
}

pcap_analysis_store_status_t pcap_analysis_filter_profile_save(
    pcap_packet_filter_t active_filter, const pcap_flow_filter_t *quick_filter,
    char *output_path, size_t output_path_size)
{
    if (active_filter < PCAP_FILTER_ALL || active_filter >= PCAP_FILTER_COUNT ||
        !quick_filter || !output_path || output_path_size == 0 ||
        !espc_prepare_artifact_dirs()) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    int profile_path_len = snprintf(output_path, output_path_size, "%s", ESPC_LAST_PROFILE);
    if (profile_path_len <= 0 || (size_t)profile_path_len >= output_path_size) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }
    char temp_path[PCAP_ANALYSIS_STORE_PATH_MAX + 8U];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_path) >= (int)sizeof(temp_path)) {
        return PCAP_ANALYSIS_STORE_LIMIT;
    }
    FILE *profile = fopen(temp_path, "wb");
    if (!profile) return PCAP_ANALYSIS_STORE_IO_ERROR;
    fprintf(profile,
            "{\n  \"schema\":\"espshark-filter\",\n  \"schema_version\":%u,\n"
            "  \"name\":\"Last saved filter\",\n  \"packet_filter\":%d,\n"
            "  \"packet_filter_name\":\"%s\",\n  \"quick_filter\":{"
            "\"enabled\":%s,\"has_any_address\":%s,\"any_address\":",
            PCAP_ANALYSIS_FILTER_SCHEMA_VERSION, (int)active_filter,
            pcap_reader_filter_name(active_filter),
            quick_filter->enabled ? "true" : "false",
            quick_filter->has_any_address ? "true" : "false");
    espc_json_string(profile, quick_filter->any_address);
    fprintf(profile, ",\"has_source_address\":%s,\"source_address\":",
            quick_filter->has_source_address ? "true" : "false");
    espc_json_string(profile, quick_filter->source_address);
    fprintf(profile, ",\"has_destination_address\":%s,\"destination_address\":",
            quick_filter->has_destination_address ? "true" : "false");
    espc_json_string(profile, quick_filter->destination_address);
    fprintf(profile, ",\"has_any_mac\":%s,\"any_mac\":",
            quick_filter->has_any_mac ? "true" : "false");
    espc_json_string(profile, quick_filter->any_mac);
    fprintf(profile,
            ",\"has_port\":%s,\"port\":%u,\"has_flow\":%s,\"flow_id\":%u,"
            "\"has_application\":%s,\"application\":%u,"
            "\"has_time_window\":%s,\"time_start_us\":%llu,\"time_end_us\":%llu}"
            "\n}\n",
            quick_filter->has_port ? "true" : "false", quick_filter->port,
            quick_filter->has_flow ? "true" : "false", quick_filter->flow_id,
            quick_filter->has_application ? "true" : "false", quick_filter->application,
            quick_filter->has_time_window ? "true" : "false",
            (unsigned long long)quick_filter->time_start_us,
            (unsigned long long)quick_filter->time_end_us);
    bool ok = !ferror(profile) && fflush(profile) == 0;
    if (ok) {
        int fd = fileno(profile);
        if (fd >= 0) ok = fsync(fd) == 0;
    }
    if (fclose(profile) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    remove(output_path);
    if (rename(temp_path, output_path) != 0) {
        remove(temp_path);
        return PCAP_ANALYSIS_STORE_IO_ERROR;
    }
    return PCAP_ANALYSIS_STORE_OK;
}

static bool espc_profile_long(const char *text, const char *key, long long *value_out)
{
    const char *field = strstr(text, key);
    field = field ? strchr(field, ':') : NULL;
    if (!field) return false;
    char *end = NULL;
    long long value = strtoll(field + 1, &end, 10);
    if (end == field + 1) return false;
    *value_out = value;
    return true;
}

static bool espc_profile_bool(const char *text, const char *key, bool *value_out)
{
    const char *field = strstr(text, key);
    field = field ? strchr(field, ':') : NULL;
    if (!field) return false;
    field++;
    while (*field && isspace((unsigned char)*field)) field++;
    if (strncmp(field, "true", 4U) == 0) {
        *value_out = true;
        return true;
    }
    if (strncmp(field, "false", 5U) == 0) {
        *value_out = false;
        return true;
    }
    return false;
}

static bool espc_profile_string(const char *text, const char *key,
                                char *output, size_t output_size)
{
    const char *field = strstr(text, key);
    field = field ? strchr(field, ':') : NULL;
    field = field ? strchr(field, '\"') : NULL;
    if (!field || !output || output_size == 0U) return false;
    field++;
    size_t length = 0U;
    while (field[length] && field[length] != '\"') length++;
    if (field[length] != '\"' || length >= output_size) return false;
    memcpy(output, field, length);
    output[length] = '\0';
    return true;
}

pcap_analysis_store_status_t pcap_analysis_filter_profile_load(
    pcap_packet_filter_t *active_filter_out, pcap_flow_filter_t *quick_filter_out,
    char *input_path, size_t input_path_size)
{
    if (!active_filter_out || !quick_filter_out) return PCAP_ANALYSIS_STORE_INVALID;
    if (input_path && input_path_size > 0) {
        int written = snprintf(input_path, input_path_size, "%s", ESPC_LAST_PROFILE);
        if (written <= 0 || (size_t)written >= input_path_size) {
            return PCAP_ANALYSIS_STORE_LIMIT;
        }
    }
    FILE *profile = fopen(ESPC_LAST_PROFILE, "rb");
    if (!profile) return PCAP_ANALYSIS_STORE_NOT_FOUND;
    char text[1536];
    size_t length = fread(text, 1, sizeof(text) - 1U, profile);
    bool read_ok = !ferror(profile) && feof(profile);
    fclose(profile);
    if (!read_ok) return PCAP_ANALYSIS_STORE_LIMIT;
    text[length] = '\0';
    if (!strstr(text, "\"schema\":\"espshark-filter\"")) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    const char *version_field = strstr(text, "\"schema_version\"");
    version_field = version_field ? strchr(version_field, ':') : NULL;
    if (!version_field) return PCAP_ANALYSIS_STORE_INVALID;
    char *version_end = NULL;
    long version = strtol(version_field + 1, &version_end, 10);
    if (version_end == version_field + 1 ||
        version != PCAP_ANALYSIS_FILTER_SCHEMA_VERSION) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    long long value = 0;
    if (!espc_profile_long(text, "\"packet_filter\"", &value) ||
        value < PCAP_FILTER_ALL || value >= PCAP_FILTER_COUNT) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    pcap_flow_filter_clear(quick_filter_out);
    long long number = 0;
    if (!espc_profile_bool(text, "\"enabled\"", &quick_filter_out->enabled) ||
        !espc_profile_bool(text, "\"has_any_address\"",
                           &quick_filter_out->has_any_address) ||
        !espc_profile_string(text, "\"any_address\"", quick_filter_out->any_address,
                             sizeof(quick_filter_out->any_address)) ||
        !espc_profile_bool(text, "\"has_source_address\"",
                           &quick_filter_out->has_source_address) ||
        !espc_profile_string(text, "\"source_address\"", quick_filter_out->source_address,
                             sizeof(quick_filter_out->source_address)) ||
        !espc_profile_bool(text, "\"has_destination_address\"",
                           &quick_filter_out->has_destination_address) ||
        !espc_profile_string(text, "\"destination_address\"",
                             quick_filter_out->destination_address,
                             sizeof(quick_filter_out->destination_address)) ||
        !espc_profile_bool(text, "\"has_any_mac\"", &quick_filter_out->has_any_mac) ||
        !espc_profile_string(text, "\"any_mac\"", quick_filter_out->any_mac,
                             sizeof(quick_filter_out->any_mac)) ||
        !espc_profile_bool(text, "\"has_port\"", &quick_filter_out->has_port) ||
        !espc_profile_long(text, "\"port\"", &number) || number < 0 || number > UINT16_MAX) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    quick_filter_out->port = (uint16_t)number;
    if (!espc_profile_bool(text, "\"has_flow\"", &quick_filter_out->has_flow) ||
        !espc_profile_long(text, "\"flow_id\"", &number) ||
        number < 0 || number > UINT16_MAX) return PCAP_ANALYSIS_STORE_INVALID;
    quick_filter_out->flow_id = (uint16_t)number;
    if (!espc_profile_bool(text, "\"has_application\"",
                           &quick_filter_out->has_application) ||
        !espc_profile_long(text, "\"application\"", &number) ||
        number < 0 || number >= PCAP_APP_COUNT) return PCAP_ANALYSIS_STORE_INVALID;
    quick_filter_out->application = (uint8_t)number;
    if (!espc_profile_bool(text, "\"has_time_window\"",
                           &quick_filter_out->has_time_window) ||
        !espc_profile_long(text, "\"time_start_us\"", &number) || number < 0) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    quick_filter_out->time_start_us = (uint64_t)number;
    if (!espc_profile_long(text, "\"time_end_us\"", &number) || number < 0) {
        return PCAP_ANALYSIS_STORE_INVALID;
    }
    quick_filter_out->time_end_us = (uint64_t)number;
    *active_filter_out = (pcap_packet_filter_t)value;
    return PCAP_ANALYSIS_STORE_OK;
}

const char *pcap_analysis_store_status_name(pcap_analysis_store_status_t status)
{
    switch (status) {
        case PCAP_ANALYSIS_STORE_OK: return "OK";
        case PCAP_ANALYSIS_STORE_NOT_FOUND: return "not found";
        case PCAP_ANALYSIS_STORE_STALE: return "stale";
        case PCAP_ANALYSIS_STORE_INVALID: return "invalid";
        case PCAP_ANALYSIS_STORE_IO_ERROR: return "I/O error";
        case PCAP_ANALYSIS_STORE_LIMIT: return "limit reached";
        default: return "unknown";
    }
}
