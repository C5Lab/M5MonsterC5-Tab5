#ifndef HS_CAPTURE_VALIDATION_H
#define HS_CAPTURE_VALIDATION_H

#include "hs_capture_analyzer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HS_CAPTURE_FILE_UNKNOWN = 0,
    HS_CAPTURE_FILE_PCAP = 1,
    HS_CAPTURE_FILE_HCCAPX = 2,
} hs_capture_file_format_t;

typedef struct {
    uint64_t size;
    uint32_t crc32;
    uint32_t validator_version;
    hs_capture_file_format_t format;
} hs_capture_validation_identity_t;

typedef struct {
    hs_capture_validation_identity_t identity;
    hs_capture_report_t report;
} hs_capture_validation_entry_t;

typedef enum {
    HS_CAPTURE_VALIDATION_OK = 0,
    HS_CAPTURE_VALIDATION_INVALID,
    HS_CAPTURE_VALIDATION_UNAVAILABLE,
    HS_CAPTURE_VALIDATION_CANCELLED,
    HS_CAPTURE_VALIDATION_NOT_FOUND,
    HS_CAPTURE_VALIDATION_STALE,
    HS_CAPTURE_VALIDATION_CORRUPT,
    HS_CAPTURE_VALIDATION_IO_ERROR,
    HS_CAPTURE_VALIDATION_INVALID_ARGUMENT,
} hs_capture_validation_result_t;

bool hs_capture_validation_sidecar_path(const char *capture_path,
                                        char *sidecar_path,
                                        size_t sidecar_capacity);
hs_capture_validation_result_t hs_capture_validation_save(
    const char *capture_path, const hs_capture_validation_entry_t *entry);
hs_capture_validation_result_t hs_capture_validation_load(
    const char *capture_path,
    const hs_capture_validation_identity_t *expected_identity,
    hs_capture_validation_entry_t *entry_out);

#endif
