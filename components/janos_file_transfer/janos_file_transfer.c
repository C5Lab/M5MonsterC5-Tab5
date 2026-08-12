#include "janos_file_transfer.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

#define JANOS_TRANSFER_DEFAULT_BUFFER_SIZE 8192U
#define JANOS_TRANSFER_MIN_BUFFER_SIZE     1024U
#define JANOS_TRANSFER_MAX_BUFFER_SIZE     (64U * 1024U)
#define JANOS_TRANSFER_DEFAULT_TIMEOUT_MS  15000
#define JANOS_TRANSFER_MAX_LIST_BYTES      (256U * 1024U)
#define JANOS_TRANSFER_FREE_SPACE_RESERVE  (64U * 1024U)

static const char *TAG = "janos_transfer";

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} janos_http_buffer_t;

static bool transfer_cancelled(const janos_file_transfer_config_t *config)
{
    return config && config->cancel_requested && *config->cancel_requested;
}

static void emit_progress(const janos_file_transfer_config_t *config,
                          janos_file_transfer_state_t state,
                          uint64_t received,
                          uint64_t total,
                          uint32_t crc32,
                          int http_status,
                          esp_err_t error,
                          const char *message)
{
    if (!config || !config->progress_cb) {
        return;
    }

    janos_file_transfer_progress_t progress = {
        .state = state,
        .bytes_received = received,
        .bytes_total = total,
        .crc32 = crc32,
        .http_status = http_status,
        .error = error,
        .message = message,
    };
    config->progress_cb(&progress, config->progress_ctx);
}

static bool path_has_parent_reference(const char *path)
{
    if (!path) {
        return true;
    }

    const char *segment = path;
    while (*segment) {
        while (*segment == '/') {
            segment++;
        }
        const char *end = strchr(segment, '/');
        size_t len = end ? (size_t)(end - segment) : strlen(segment);
        if (len == 2 && segment[0] == '.' && segment[1] == '.') {
            return true;
        }
        if (!end) {
            break;
        }
        segment = end + 1;
    }
    return false;
}

static esp_err_t normalize_remote_path(const char *input, char *output, size_t output_size)
{
    if (!input || !output || output_size < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = input;
    static const char sd_lab_prefix[] = "/sdcard/lab/";
    static const char lab_prefix[] = "/lab/";

    if (strncmp(path, sd_lab_prefix, sizeof(sd_lab_prefix) - 1) == 0) {
        path += sizeof(sd_lab_prefix) - 1;
    } else if (strncmp(path, lab_prefix, sizeof(lab_prefix) - 1) == 0) {
        path += sizeof(lab_prefix) - 1;
    } else {
        while (*path == '/') {
            path++;
        }
    }

    if (*path == '\0' || path_has_parent_reference(path) || strchr(path, '\\')) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(path);
    if (len >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, path, len + 1);
    return ESP_OK;
}

static bool is_url_unreserved(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

static esp_err_t url_encode(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!input || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; p++) {
        if (is_url_unreserved(*p)) {
            if (pos + 1 >= output_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            output[pos++] = (char)*p;
        } else {
            if (pos + 3 >= output_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            output[pos++] = '%';
            output[pos++] = hex[*p >> 4];
            output[pos++] = hex[*p & 0x0F];
        }
    }
    output[pos] = '\0';
    return ESP_OK;
}

static esp_err_t build_url(char *output, size_t output_size,
                           const char *base_url, const char *endpoint,
                           const char *encoded_path)
{
    if (!output || !base_url || !endpoint || !encoded_path) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t base_len = strlen(base_url);
    while (base_len > 0 && base_url[base_len - 1] == '/') {
        base_len--;
    }

    int written = snprintf(output, output_size, "%.*s%s?path=%s",
                           (int)base_len, base_url, endpoint, encoded_path);
    if (written < 0 || (size_t)written >= output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t http_read_all(const char *url, int timeout_ms,
                               janos_http_buffer_t *body, int *status_out)
{
    if (!url || !body) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(body, 0, sizeof(*body));
    if (status_out) {
        *status_out = 0;
    }

    esp_http_client_config_t client_config = {
        .url = url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : JANOS_TRANSFER_DEFAULT_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status_out) {
        *status_out = status;
    }
    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return status == 404 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    body->capacity = 4096;
    body->data = malloc(body->capacity);
    if (!body->data) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        if (body->capacity - body->len < 2048 + 1) {
            size_t next_capacity = body->capacity * 2;
            if (next_capacity > JANOS_TRANSFER_MAX_LIST_BYTES) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            char *next = realloc(body->data, next_capacity);
            if (!next) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            body->data = next;
            body->capacity = next_capacity;
        }

        int read = esp_http_client_read(client, body->data + body->len,
                                        body->capacity - body->len - 1);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            if (!esp_http_client_is_complete_data_received(client)) {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }
        body->len += (size_t)read;
        if (body->len >= JANOS_TRANSFER_MAX_LIST_BYTES) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
    }

    body->data[body->len] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(body->data);
        memset(body, 0, sizeof(*body));
    }
    return err;
}

esp_err_t janos_file_transfer_query_size(const char *base_url,
                                         const char *remote_path,
                                         int timeout_ms,
                                         uint64_t *size_out)
{
    if (!base_url || !remote_path || !size_out) {
        return ESP_ERR_INVALID_ARG;
    }
    *size_out = 0;

    char normalized[256];
    esp_err_t err = normalize_remote_path(remote_path, normalized, sizeof(normalized));
    if (err != ESP_OK) {
        return err;
    }

    char *slash = strrchr(normalized, '/');
    const char *filename = slash ? slash + 1 : normalized;
    char directory[256] = {0};
    if (slash) {
        size_t dir_len = (size_t)(slash - normalized);
        if (dir_len >= sizeof(directory)) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(directory, normalized, dir_len);
        directory[dir_len] = '\0';
    }
    if (*filename == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char encoded_dir[768];
    err = url_encode(directory, encoded_dir, sizeof(encoded_dir));
    if (err != ESP_OK) {
        return err;
    }

    char url[1024];
    err = build_url(url, sizeof(url), base_url, "/api/list", encoded_dir);
    if (err != ESP_OK) {
        return err;
    }

    janos_http_buffer_t body;
    int http_status = 0;
    err = http_read_all(url, timeout_ms, &body, &http_status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "List request failed (%d): %s", http_status, esp_err_to_name(err));
        return err;
    }

    cJSON *root = cJSON_ParseWithLength(body.data, body.len);
    free(body.data);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool found = false;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        cJSON *is_dir = cJSON_GetObjectItemCaseSensitive(item, "dir");
        cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "size");
        if (cJSON_IsString(name) && name->valuestring && strcmp(name->valuestring, filename) == 0 &&
            cJSON_IsBool(is_dir) && !cJSON_IsTrue(is_dir) && cJSON_IsNumber(size) &&
            size->valuedouble >= 0) {
            *size_out = (uint64_t)size->valuedouble;
            found = true;
            break;
        }
    }
    cJSON_Delete(root);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t validate_local_path(const char *local_path, const char *mount_point)
{
    if (!local_path || !mount_point || local_path[0] != '/' || mount_point[0] != '/' ||
        path_has_parent_reference(local_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t mount_len = strlen(mount_point);
    if (strncmp(local_path, mount_point, mount_len) != 0 ||
        (local_path[mount_len] != '/' && local_path[mount_len] != '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t create_parent_directories(const char *local_path, const char *mount_point)
{
    char temp[256];
    size_t len = strlen(local_path);
    if (len == 0 || len >= sizeof(temp)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(temp, local_path, len + 1);

    char *last_slash = strrchr(temp, '/');
    if (!last_slash || last_slash == temp) {
        return ESP_ERR_INVALID_ARG;
    }
    *last_slash = '\0';

    size_t mount_len = strlen(mount_point);
    for (char *p = temp + mount_len + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
            *p = '/';
            return ESP_FAIL;
        }
        *p = '/';
    }
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

/* Folds an already downloaded .part prefix into the running CRC, so a resumed
 * transfer still verifies the whole file and not only the bytes fetched now. */
static esp_err_t crc32_of_prefix(const char *path, uint64_t length, uint32_t *crc_out)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_FAIL;
    }
    uint8_t chunk[1024];
    uint32_t crc = 0;
    uint64_t remaining = length;
    esp_err_t err = ESP_OK;
    while (remaining > 0) {
        size_t want = remaining > sizeof(chunk) ? sizeof(chunk) : (size_t)remaining;
        size_t got = fread(chunk, 1, want, file);
        if (got == 0) {
            err = ESP_FAIL;
            break;
        }
        crc = crc32_update(crc, chunk, got);
        remaining -= got;
    }
    fclose(file);
    if (err == ESP_OK) {
        *crc_out = crc;
    }
    return err;
}

static esp_err_t check_free_space(const char *mount_point, uint64_t needed)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(mount_point, &total, &free_bytes);
    if (err != ESP_OK) {
        return err;
    }
    (void)total;
    if (free_bytes < needed || free_bytes - needed < JANOS_TRANSFER_FREE_SPACE_RESERVE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static size_t normalized_buffer_size(size_t requested)
{
    if (requested == 0) {
        return JANOS_TRANSFER_DEFAULT_BUFFER_SIZE;
    }
    if (requested < JANOS_TRANSFER_MIN_BUFFER_SIZE) {
        return JANOS_TRANSFER_MIN_BUFFER_SIZE;
    }
    if (requested > JANOS_TRANSFER_MAX_BUFFER_SIZE) {
        return JANOS_TRANSFER_MAX_BUFFER_SIZE;
    }
    return requested;
}

esp_err_t janos_file_transfer_download(const janos_file_transfer_config_t *config,
                                       janos_file_transfer_result_t *result)
{
    if (!config || !result || !config->base_url || !config->remote_path ||
        !config->local_path || !config->storage_mount_point) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    esp_err_t err = validate_local_path(config->local_path, config->storage_mount_point);
    if (err != ESP_OK) {
        return err;
    }
    if (strlen(config->local_path) >= sizeof(result->final_path) ||
        strlen(config->local_path) + 5 >= sizeof(result->part_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    snprintf(result->final_path, sizeof(result->final_path), "%s", config->local_path);
    snprintf(result->part_path, sizeof(result->part_path), "%s.part", config->local_path);

    /* A finished file is never overwritten. A leftover .part, on the other hand,
     * is a previous attempt that was cut short and is resumed further down. */
    if (access(result->final_path, F_OK) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (transfer_cancelled(config)) {
        emit_progress(config, JANOS_FILE_TRANSFER_CANCELLED, 0, 0, 0, 0,
                      ESP_ERR_INVALID_STATE, "Cancelled");
        return ESP_ERR_INVALID_STATE;
    }

    emit_progress(config, JANOS_FILE_TRANSFER_QUERYING, 0, 0, 0, 0, ESP_OK,
                  "Reading remote file metadata");
    err = janos_file_transfer_query_size(config->base_url, config->remote_path,
                                         config->timeout_ms, &result->remote_size_before);
    if (err != ESP_OK) {
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, 0, 0, 0, 0, err,
                      "Remote file was not found");
        return err;
    }

    /* Resume a partial download. JanOS serves HTTP Range, and a large capture
     * rarely survives one uninterrupted attempt, so continuing beats restarting.
     * A .part that is not shorter than the remote file belongs to something
     * else entirely and is discarded rather than trusted. */
    uint64_t resume_from = 0;
    struct stat part_info;
    if (stat(result->part_path, &part_info) == 0 && part_info.st_size > 0) {
        resume_from = (uint64_t)part_info.st_size;
        if (resume_from >= result->remote_size_before) {
            ESP_LOGW(TAG, "Discarding stale %s (%llu B vs remote %llu B)",
                     result->part_path, (unsigned long long)resume_from,
                     (unsigned long long)result->remote_size_before);
            unlink(result->part_path);
            resume_from = 0;
        } else if (crc32_of_prefix(result->part_path, resume_from,
                                   &result->crc32) != ESP_OK) {
            ESP_LOGW(TAG, "Could not re-read %s; restarting the download",
                     result->part_path);
            unlink(result->part_path);
            result->crc32 = 0;
            resume_from = 0;
        } else {
            result->bytes_written = resume_from;
            ESP_LOGI(TAG, "Resuming %s at %llu of %llu bytes", result->part_path,
                     (unsigned long long)resume_from,
                     (unsigned long long)result->remote_size_before);
        }
    }

    err = check_free_space(config->storage_mount_point,
                           result->remote_size_before - resume_from);
    if (err != ESP_OK) {
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, 0, result->remote_size_before,
                      0, 0, err, "Not enough free space on local SD");
        return err;
    }

    err = create_parent_directories(result->final_path, config->storage_mount_point);
    if (err != ESP_OK) {
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, 0, result->remote_size_before,
                      0, 0, err, "Could not create local directory");
        return err;
    }

    char normalized[256];
    err = normalize_remote_path(config->remote_path, normalized, sizeof(normalized));
    if (err != ESP_OK) {
        return err;
    }
    char encoded_path[768];
    err = url_encode(normalized, encoded_path, sizeof(encoded_path));
    if (err != ESP_OK) {
        return err;
    }
    char url[1024];
    err = build_url(url, sizeof(url), config->base_url, "/api/download", encoded_path);
    if (err != ESP_OK) {
        return err;
    }

    size_t buffer_size = normalized_buffer_size(config->io_buffer_size);
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, 0, result->remote_size_before,
                      0, 0, ESP_ERR_NO_MEM, "Could not allocate transfer buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t client_config = {
        .url = url,
        .timeout_ms = config->timeout_ms > 0 ? config->timeout_ms
                                             : JANOS_TRANSFER_DEFAULT_TIMEOUT_MS,
        .buffer_size = (int)buffer_size,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (!client) {
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    if (resume_from > 0) {
        char range[48];
        snprintf(range, sizeof(range), "bytes=%llu-", (unsigned long long)resume_from);
        esp_http_client_set_header(client, "Range", range);
    }

    err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        result->http_status = esp_http_client_get_status_code(client);
        if (result->http_status == 200 && resume_from > 0) {
            /* Firmware without Range support answers with the whole file, so the
             * partial prefix has to be thrown away rather than appended to. */
            ESP_LOGW(TAG, "Monster ignored Range; restarting from zero");
            resume_from = 0;
            result->bytes_written = 0;
            result->crc32 = 0;
        } else if (result->http_status != 200 && result->http_status != 206) {
            err = result->http_status == 404 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        }
    }

    /* Opened only once the server has agreed, so that a request the Monster
     * refused never truncates a .part that is still worth resuming. */
    FILE *file = NULL;
    if (err == ESP_OK) {
        file = fopen(result->part_path, resume_from > 0 ? "ab" : "wb");
        if (!file) {
            err = ESP_FAIL;
        }
    }
    if (!file) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(buffer);
        if (err == ESP_OK) {
            err = ESP_FAIL;
        }
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      err, "Monster refused the download or the .part could not be opened");
        return err;
    }

    int64_t last_progress_us = 0;
    if (err == ESP_OK) {
        emit_progress(config, JANOS_FILE_TRANSFER_DOWNLOADING, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      ESP_OK, resume_from > 0 ? "Resuming download from Monster"
                                              : "Downloading from Monster");
    }

    while (err == ESP_OK) {
        if (transfer_cancelled(config)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }

        int read = esp_http_client_read(client, (char *)buffer, buffer_size);
        if (read < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read == 0) {
            if (!esp_http_client_is_complete_data_received(client)) {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }

        size_t written = fwrite(buffer, 1, (size_t)read, file);
        if (written != (size_t)read) {
            err = ESP_FAIL;
            break;
        }
        result->bytes_written += written;
        result->crc32 = crc32_update(result->crc32, buffer, written);

        int64_t now = esp_timer_get_time();
        if (now - last_progress_us >= 100000) {
            emit_progress(config, JANOS_FILE_TRANSFER_DOWNLOADING,
                          result->bytes_written, result->remote_size_before,
                          result->crc32, result->http_status, ESP_OK,
                          "Downloading from Monster");
            last_progress_us = now;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);

    if (fflush(file) != 0) {
        err = ESP_FAIL;
    }
    int fd = fileno(file);
    if (fd >= 0 && fsync(fd) != 0) {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0) {
        err = ESP_FAIL;
    }

    if (transfer_cancelled(config)) {
        emit_progress(config, JANOS_FILE_TRANSFER_CANCELLED, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      ESP_ERR_INVALID_STATE, "Transfer cancelled; .part was kept");
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      err, "Download or local SD write failed; .part was kept");
        return err;
    }

    emit_progress(config, JANOS_FILE_TRANSFER_VERIFYING, result->bytes_written,
                  result->remote_size_before, result->crc32, result->http_status,
                  ESP_OK, "Verifying file size");

    err = janos_file_transfer_query_size(config->base_url, config->remote_path,
                                         config->timeout_ms, &result->remote_size_after);
    if (err != ESP_OK || result->remote_size_before != result->remote_size_after ||
        result->bytes_written != result->remote_size_before) {
        if (err == ESP_OK) {
            err = ESP_ERR_INVALID_SIZE;
        }
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      err, "Remote or local file size changed; .part was kept");
        return err;
    }

    emit_progress(config, JANOS_FILE_TRANSFER_COMMITTING, result->bytes_written,
                  result->remote_size_before, result->crc32, result->http_status,
                  ESP_OK, "Finalizing local file");
    if (rename(result->part_path, result->final_path) != 0) {
        err = ESP_FAIL;
        emit_progress(config, JANOS_FILE_TRANSFER_ERROR, result->bytes_written,
                      result->remote_size_before, result->crc32, result->http_status,
                      err, "Could not rename .part file");
        return err;
    }

    emit_progress(config, JANOS_FILE_TRANSFER_DONE, result->bytes_written,
                  result->remote_size_before, result->crc32, result->http_status,
                  ESP_OK, "Transfer complete");
    ESP_LOGI(TAG, "Saved %s (%llu bytes, CRC32 %08lx)", result->final_path,
             (unsigned long long)result->bytes_written, (unsigned long)result->crc32);
    return ESP_OK;
}

const char *janos_file_transfer_state_name(janos_file_transfer_state_t state)
{
    switch (state) {
        case JANOS_FILE_TRANSFER_QUERYING: return "Querying";
        case JANOS_FILE_TRANSFER_DOWNLOADING: return "Downloading";
        case JANOS_FILE_TRANSFER_VERIFYING: return "Verifying";
        case JANOS_FILE_TRANSFER_COMMITTING: return "Committing";
        case JANOS_FILE_TRANSFER_DONE: return "Done";
        case JANOS_FILE_TRANSFER_CANCELLED: return "Cancelled";
        case JANOS_FILE_TRANSFER_ERROR: return "Error";
        default: return "Unknown";
    }
}
