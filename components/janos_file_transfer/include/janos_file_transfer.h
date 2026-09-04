#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JANOS_FILE_TRANSFER_QUERYING = 0,
    JANOS_FILE_TRANSFER_DOWNLOADING,
    JANOS_FILE_TRANSFER_VERIFYING,
    JANOS_FILE_TRANSFER_COMMITTING,
    JANOS_FILE_TRANSFER_DONE,
    JANOS_FILE_TRANSFER_CANCELLED,
    JANOS_FILE_TRANSFER_ERROR,
} janos_file_transfer_state_t;

typedef struct {
    janos_file_transfer_state_t state;
    uint64_t bytes_received;
    uint64_t bytes_total;
    uint32_t crc32;
    int http_status;
    esp_err_t error;
    const char *message;
} janos_file_transfer_progress_t;

typedef void (*janos_file_transfer_progress_cb_t)(
    const janos_file_transfer_progress_t *progress,
    void *user_ctx);

typedef struct {
    const char *base_url;
    const char *remote_path;
    const char *local_path;
    const char *storage_mount_point;
    size_t io_buffer_size;
    int timeout_ms;
    const volatile bool *cancel_requested;
    janos_file_transfer_progress_cb_t progress_cb;
    void *progress_ctx;
} janos_file_transfer_config_t;

typedef struct {
    uint64_t remote_size_before;
    uint64_t remote_size_after;
    uint64_t bytes_written;
    uint32_t crc32;
    int http_status;
    char final_path[256];
    char part_path[272];
} janos_file_transfer_result_t;

/**
 * Query a regular file size through JanOS /api/list.
 * remote_path may be relative to /sdcard/lab or use a /sdcard/lab/... prefix.
 */
esp_err_t janos_file_transfer_query_size(const char *base_url,
                                         const char *remote_path,
                                         int timeout_ms,
                                         uint64_t *size_out);

/**
 * Download a JanOS file to local_path using an atomic .part -> final rename.
 * This is a blocking call intended to run in a worker task.
 */
esp_err_t janos_file_transfer_download(const janos_file_transfer_config_t *config,
                                       janos_file_transfer_result_t *result);

const char *janos_file_transfer_state_name(janos_file_transfer_state_t state);

#ifdef __cplusplus
}
#endif
