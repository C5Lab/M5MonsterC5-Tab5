#include "hs_crack_remote_core.h"
#include "hs_crack_cache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t hs_remote_receive_block_size(bool usb_cdc)
{
    return usb_cdc ? 1024U : 8192U;
}

bool hs_remote_frame_command(char *output, size_t output_size,
                             const char *command)
{
    if (output == NULL || command == NULL || output_size < 3U ||
        strchr(command, '\r') != NULL || strchr(command, '\n') != NULL) {
        return false;
    }
    size_t length = strlen(command);
    if (length > output_size - 3U) return false;
    memcpy(output, command, length);
    output[length] = '\r';
    output[length + 1U] = '\n';
    output[length + 2U] = '\0';
    return true;
}

bool hs_remote_retry_at_default_baud(bool fast_active, bool synchronized,
                                     bool cancelled)
{
    return fast_active && !synchronized && !cancelled;
}

bool hs_remote_stage_retry_allowed(unsigned completed_attempts,
                                   bool cancelled, bool boundary_safe)
{
    return completed_attempts < HS_REMOTE_STAGE_ATTEMPTS &&
           !cancelled && boundary_safe;
}

uint32_t hs_remote_stage_backoff_ms(unsigned next_attempt)
{
    if (next_attempt == 2U) return 250U;
    if (next_attempt == 3U) return 1000U;
    return 0U;
}

uint32_t hs_remote_probe_timeout_ms(uint64_t size)
{
    /* A cold JanOS cache may validate the whole file from SD before replying.
     * Budget for 64 KiB/s, keep tiny probes responsive, and still bound a
     * completely silent worker so cancellation/recovery remain possible. */
    uint64_t seconds = size / 65536U;
    if ((size % 65536U) != 0) seconds++;
    seconds += 15U;
    if (seconds < 30U) seconds = 30U;
    if (seconds > 600U) seconds = 600U;
    return (uint32_t)(seconds * 1000U);
}

bool hs_remote_format_receive_command(char *output, size_t output_size,
                                      const char *kind, uint64_t size,
                                      uint32_t crc32, bool usb_cdc)
{
    if (!output || output_size == 0 || !kind || kind[0] == '\0') return false;

    /* Grove and M-BUS switch their hardware UARTs to 2 Mbaud for binary
     * transfer. USB is CDC, has no baud switch, and needs a smaller block so
     * JanOS can consume and acknowledge it through the console path. */
    const uint32_t block_size = hs_remote_receive_block_size(usb_cdc);
    int length = snprintf(output, output_size,
                          "crack_worker receive %s %llu %08lX %u%s", kind,
                          (unsigned long long)size, (unsigned long)crc32,
                          (unsigned)block_size, usb_cdc ? " ack32" : "");
    return length > 0 && (size_t)length < output_size;
}

static uint32_t read_u32_le(const uint8_t *input)
{
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) |
           ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t *input)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8U; ++i)
        value |= (uint64_t)input[i] << (i * 8U);
    return value;
}

const char *hs_remote_ack32_parse_error(const uint8_t *frame, size_t size)
{
    if (!frame) return "null frame";
    if (size != HS_REMOTE_ACK32_SIZE) return "size";
    if (memcmp(frame, "FTA\x01", 4U) != 0) return "magic/version";
    if (frame[9] != 0 ||
        frame[10] != 0 || frame[11] != 0 ||
        memcmp(frame + 24U, "\0\0\0\0\0\0\0\0", 8U) != 0)
        return "reserved bytes";
    if (frame[8] != 0x06U && frame[8] != 0x15U && frame[8] != 0x18U)
        return "status";
    if (read_u32_le(frame + 20U) != hs_crack_cache_crc32_update(0, frame, 20U))
        return "crc";
    return NULL;
}

bool hs_remote_ack32_parse(const uint8_t *frame, size_t size,
                           hs_remote_ack32_t *reply)
{
    if (!reply || hs_remote_ack32_parse_error(frame, size)) return false;
    reply->block_index = read_u32_le(frame + 4U);
    reply->status = frame[8];
    reply->committed_offset = read_u64_le(frame + 12U);
    return true;
}

bool hs_remote_ack32_matches(const hs_remote_ack32_t *reply,
                             uint32_t expected_index,
                             uint64_t expected_offset)
{
    return reply && reply->status == 0x06U &&
           reply->block_index == expected_index &&
           reply->committed_offset == expected_offset;
}

bool hs_remote_capabilities_v4_valid(const hs_remote_message_t *message)
{
    return message && message->type == HS_REMOTE_CAPABILITIES &&
           message->protocol == 4U && strcmp(message->sync, "ftb1") == 0 &&
           message->ack_frame32 && message->replay_last_block &&
           message->finish_fin32;
}

bool hs_remote_ready_valid(const hs_remote_message_t *message, bool usb_ack32)
{
    if (!message || message->type != HS_REMOTE_READY) return false;
    if (!usb_ack32) return message->ack_size == 0U || message->ack_size == 1U;
    return message->ack_size == HS_REMOTE_ACK32_SIZE &&
           message->ack_wait_ms == 2000U &&
           message->next_header_ms == 7000U &&
           message->prepare_ms >= 10000U && message->prepare_ms <= 600000U &&
           message->finish_linger_ms == 7000U;
}

size_t hs_remote_ack_stream_push(hs_remote_ack_stream_t *stream,
                                 const uint8_t *input, size_t length,
                                 hs_remote_ack32_t *complete)
{
    if (!stream || (!input && length != 0U)) return 0U;
    if (stream->used > HS_REMOTE_ACK32_SIZE) {
        stream->used = 0U;
        if (complete) memset(complete, 0, sizeof(*complete));
        return 0U;
    }

    size_t needed = HS_REMOTE_ACK32_SIZE - stream->used;
    size_t consumed = length < needed ? length : needed;
    if (consumed != 0U) {
        memcpy(stream->bytes + stream->used, input, consumed);
        stream->used += consumed;
    }
    if (stream->used != HS_REMOTE_ACK32_SIZE) return consumed;

    hs_remote_ack32_t parsed;
    bool valid = hs_remote_ack32_parse(stream->bytes, sizeof(stream->bytes),
                                       &parsed);
    stream->used = 0U;
    if (complete) {
        if (valid) *complete = parsed;
        else memset(complete, 0, sizeof(*complete));
    }
    return consumed;
}

hs_remote_ack_action_t hs_remote_ack_classify(
    const hs_remote_ack32_t *ack, uint32_t current_index,
    uint64_t before_offset, uint64_t after_offset, bool allow_previous)
{
    if (!ack) return HS_ACK_INVALID;
    if (ack->status == 0x18U) return HS_ACK_CAN;
    if (ack->block_index == current_index) {
        if (ack->status == 0x06U && ack->committed_offset == after_offset)
            return HS_ACK_CURRENT;
        if (ack->status == 0x15U && ack->committed_offset == before_offset)
            return HS_ACK_RETRY_NAK;
        return HS_ACK_INVALID;
    }
    if (allow_previous && current_index != 0U && ack->status == 0x06U &&
        ack->block_index == current_index - 1U &&
        ack->committed_offset == before_offset)
        return HS_ACK_STALE;
    return HS_ACK_INVALID;
}

hs_remote_tx_action_t hs_remote_tx_on_deadline(unsigned transmissions)
{
    return transmissions < 3U ? HS_TX_RETRANSMIT : HS_TX_FAIL;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hs_remote_hex_decode(const char *hex, uint8_t *output, size_t capacity,
                          size_t *output_length)
{
    if (!hex || !output || !output_length) return false;
    size_t length = strlen(hex);
    if ((length & 1U) != 0 || length / 2U > capacity) return false;
    for (size_t i = 0; i < length / 2U; ++i) {
        int high = hex_nibble(hex[i * 2U]);
        int low = hex_nibble(hex[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    *output_length = length / 2U;
    return true;
}

static bool token(const char *line, const char *key, char *output, size_t size)
{
    size_t key_length = strlen(key);
    const char *cursor = line;
    while ((cursor = strstr(cursor, key)) != NULL) {
        if ((cursor == line || cursor[-1] == ' ') && cursor[key_length] == '=') {
            const char *value = cursor + key_length + 1U;
            size_t length = strcspn(value, " \r\n");
            if (length == 0 || length >= size) return false;
            memcpy(output, value, length);
            output[length] = '\0';
            return true;
        }
        cursor += key_length;
    }
    return false;
}

static bool has_token_key(const char *line, const char *key)
{
    size_t key_length = strlen(key);
    const char *cursor = line;
    while ((cursor = strstr(cursor, key)) != NULL) {
        if ((cursor == line || cursor[-1] == ' ') && cursor[key_length] == '=')
            return true;
        cursor += key_length;
    }
    return false;
}

static bool comma_token(const char *list, const char *wanted)
{
    size_t wanted_length = strlen(wanted);
    const char *cursor = list;
    while (*cursor != '\0') {
        size_t length = strcspn(cursor, ",");
        if (length == wanted_length && memcmp(cursor, wanted, length) == 0)
            return true;
        if (cursor[length] == '\0') break;
        cursor += length + 1U;
    }
    return false;
}

static bool number(const char *line, const char *key, int base, uint64_t *value)
{
    char text[32];
    if (!token(line, key, text, sizeof(text)) || text[0] == '-') return false;
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, base);
    if (errno == ERANGE || end == text || *end != '\0') return false;
    *value = (uint64_t)parsed;
    return true;
}

static hs_remote_result_t parse_result(const char *value)
{
    if (strcmp(value, "running") == 0) return HS_REMOTE_RESULT_RUNNING;
    if (strcmp(value, "found") == 0) return HS_REMOTE_RESULT_FOUND;
    if (strcmp(value, "not_found") == 0) return HS_REMOTE_RESULT_NOT_FOUND;
    if (strcmp(value, "cancelled") == 0) return HS_REMOTE_RESULT_CANCELLED;
    if (strcmp(value, "error") == 0) return HS_REMOTE_RESULT_ERROR;
    if (strcmp(value, "idle") == 0) return HS_REMOTE_RESULT_IDLE;
    return HS_REMOTE_RESULT_NONE;
}

bool hs_remote_parse_line(const char *line, hs_remote_message_t *message)
{
    static const char prefix[] = "[CRACK/1] ";
    if (!line || !message || strncmp(line, prefix, sizeof(prefix) - 1U) != 0) return false;
    memset(message, 0, sizeof(*message));
    const char *body = line + sizeof(prefix) - 1U;
    const char *space = strchr(body, ' ');
    size_t command_length = space ? (size_t)(space - body) : strcspn(body, "\r\n");

#define COMMAND(name, value) \
    (command_length == sizeof(name) - 1U && memcmp(body, name, sizeof(name) - 1U) == 0 ? \
        (message->type = value, true) : false)
    if (!(COMMAND("CAPABILITIES", HS_REMOTE_CAPABILITIES) ||
          COMMAND("FILE", HS_REMOTE_FILE) || COMMAND("READY", HS_REMOTE_READY) ||
          COMMAND("SYNCED", HS_REMOTE_SYNCED) || COMMAND("ACCEPTED", HS_REMOTE_ACCEPTED) ||
          COMMAND("STARTED", HS_REMOTE_STARTED) || COMMAND("STATUS", HS_REMOTE_STATUS) ||
          COMMAND("DONE", HS_REMOTE_DONE) || COMMAND("RESET", HS_REMOTE_RESET) ||
          COMMAND("REJECTED", HS_REMOTE_REJECTED) ||
          COMMAND("SYNC_ERROR", HS_REMOTE_SYNC_ERROR) ||
          COMMAND("DIAG", HS_REMOTE_DIAG) || COMMAND("END", HS_REMOTE_END))) {
        return false;
    }
#undef COMMAND

    uint64_t value;
    char text[128];
    if (message->type == HS_REMOTE_CAPABILITIES) {
        if (!number(line, "protocol", 10, &value) || value > UINT32_MAX ||
            !token(line, "sync", message->sync, sizeof(message->sync))) return false;
        message->protocol = (unsigned)value;
        if (token(line, "ack", text, sizeof(text)))
            message->ack_frame32 = comma_token(text, "frame32");
        if (token(line, "replay", text, sizeof(text)))
            message->replay_last_block = strcmp(text, "last_block") == 0;
        if (token(line, "finish", text, sizeof(text)))
            message->finish_fin32 = strcmp(text, "fin32") == 0;
        return true;
    }
    if (message->type == HS_REMOTE_END) return true;
    (void)token(line, "kind", message->kind, sizeof(message->kind));
    (void)token(line, "job", message->job, sizeof(message->job));
    (void)token(line, "code", message->code, sizeof(message->code));
    if (number(line, "size", 10, &value)) message->size = value;
    if (number(line, "received", 10, &value)) message->received = value;
    if (number(line, "offset", 10, &value)) message->offset = value;
    if (number(line, "checked", 10, &value)) message->checked = value;
    if (number(line, "elapsed_ms", 10, &value)) message->elapsed_ms = value;
    if (number(line, "rate_milli", 10, &value)) message->rate_milli = value;
    (void)token(line, "phase", message->phase, sizeof(message->phase));
    if (number(line, "progress_age_ms", 10, &value)) {
        message->progress_age_ms = value;
        message->progress_age_valid = true;
    }
    if (number(line, "crc32", 16, &value) && value <= UINT32_MAX) message->crc32 = (uint32_t)value;
    if (number(line, "prefix_crc", 16, &value) && value <= UINT32_MAX)
        message->prefix_crc32 = (uint32_t)value;
    if (number(line, "bsize", 10, &value) && value <= UINT32_MAX)
        message->block_size = (uint32_t)value;
    if (number(line, "rx_ms", 10, &value) && value <= UINT32_MAX)
        message->timeout_ms = (uint32_t)value;

    if (message->type == HS_REMOTE_DIAG) {
        if (!token(line, "phase", message->phase, sizeof(message->phase)) ||
            !token(line, "reason", message->reason, sizeof(message->reason)) ||
            !number(line, "header_got", 10, &message->header_got) ||
            !number(line, "header_expected", 10, &message->header_expected) ||
            !number(line, "payload_got", 10, &message->payload_got) ||
            !number(line, "payload_expected", 10, &message->payload_expected) ||
            !number(line, "offset", 10, &message->offset) ||
            !number(line, "block", 10, &message->block_index) ||
            !number(line, "baud", 10, &value) || value > UINT32_MAX) {
            return false;
        }
        message->baud = (uint32_t)value;
        (void)number(line, "rd", 10, &message->diag_read_calls);
        (void)number(line, "rb", 10, &message->diag_read_bytes);
        (void)number(line, "sd", 10, &message->diag_sd_write_calls);
        (void)number(line, "sb", 10, &message->diag_sd_write_bytes);
        (void)number(line, "aa", 10, &message->diag_ack_attempts);
        (void)number(line, "as", 10, &message->diag_acks_sent);
        (void)number(line, "cp", 10, &message->diag_checkpoint_calls);
        (void)number(line, "stage_us", 10, &message->diag_stage_us);
        return true;
    }

    if (message->type == HS_REMOTE_FILE) {
        if (!token(line, "state", text, sizeof(text)) || message->kind[0] == '\0' ||
            !number(line, "size", 10, &message->size) ||
            !number(line, "crc32", 16, &value) || value > UINT32_MAX) return false;
        message->crc32 = (uint32_t)value;
        if (strcmp(text, "present") == 0) message->file_present = true;
        else if (strcmp(text, "missing") != 0) return false;
    } else if (message->type == HS_REMOTE_READY) {
        if (message->kind[0] == '\0' || !number(line, "size", 10, &message->size) ||
            !number(line, "crc32", 16, &value) || value > UINT32_MAX) return false;
        message->crc32 = (uint32_t)value;
        if (!number(line, "offset", 10, &message->offset) ||
            !number(line, "prefix_crc", 16, &value) || value > UINT32_MAX)
            return false;
        message->prefix_crc32 = (uint32_t)value;
        if (!number(line, "bsize", 10, &value) || value > UINT32_MAX || value == 0)
            return false;
        message->block_size = (uint32_t)value;
        if (!number(line, "rx_ms", 10, &value) || value > UINT32_MAX)
            return false;
        message->timeout_ms = (uint32_t)value;
        if (strstr(line, " ack_size=") != NULL) {
            if (!number(line, "ack_size", 10, &value) || value > UINT32_MAX)
                return false;
            message->ack_size = (uint32_t)value;
        } else {
            message->ack_size = 1U;
        }
        if (has_token_key(line, "ack_wait_ms")) {
            if (!number(line, "ack_wait_ms", 10, &value) || value > UINT32_MAX)
                return false;
            message->ack_wait_ms = (uint32_t)value;
        }
        if (has_token_key(line, "next_header_ms")) {
            if (!number(line, "next_header_ms", 10, &value) || value > UINT32_MAX)
                return false;
            message->next_header_ms = (uint32_t)value;
        }
        if (has_token_key(line, "prepare_ms")) {
            if (!number(line, "prepare_ms", 10, &value) || value > UINT32_MAX)
                return false;
            message->prepare_ms = (uint32_t)value;
        }
        if (has_token_key(line, "finish_linger_ms")) {
            if (!number(line, "finish_linger_ms", 10, &value) || value > UINT32_MAX)
                return false;
            message->finish_linger_ms = (uint32_t)value;
        }
    } else if (message->type == HS_REMOTE_STATUS || message->type == HS_REMOTE_DONE) {
        if (message->job[0] == '\0' || !token(line, message->type == HS_REMOTE_STATUS ?
                                               "state" : "result", text, sizeof(text))) return false;
        message->result = parse_result(text);
        if (message->result == HS_REMOTE_RESULT_NONE ||
            !number(line, "checked", 10, &message->checked) ||
            !number(line, "safe_offset", 10, &message->offset)) return false;
        if (message->result == HS_REMOTE_RESULT_FOUND) {
            if (!token(line, "password_hex", text, sizeof(text)) ||
                !hs_remote_hex_decode(text, message->password, sizeof(message->password),
                                      &message->password_length)) return false;
            if (token(line, "ssid_hex", text, sizeof(text)) &&
                !hs_remote_hex_decode(text, message->ssid, sizeof(message->ssid),
                                      &message->ssid_length)) return false;
        }
    }
    return true;
}

bool hs_remote_make_shards(uint64_t start, uint64_t end, size_t count,
                           hs_remote_shard_t *shards)
{
    if (!shards || count == 0 || end < start) return false;
    uint64_t total = end - start;
    uint64_t base = total / count;
    uint64_t remainder = total % count;
    uint64_t cursor = start;
    for (size_t i = 0; i < count; ++i) {
        uint64_t length = base + (i < remainder ? 1U : 0U);
        shards[i].start = cursor;
        shards[i].end = cursor + length;
        cursor += length;
    }
    return cursor == end;
}

void hs_remote_lease_init(hs_remote_lease_t *lease, uint64_t shard_start)
{
    if (!lease) return;
    lease->missed_polls = 0;
    lease->confirmed_safe_offset = shard_start;
}

bool hs_remote_lease_note_timeout(hs_remote_lease_t *lease)
{
    if (!lease) return true;
    if (lease->missed_polls < HS_REMOTE_MISSED_POLLS_LIMIT)
        lease->missed_polls++;
    return lease->missed_polls >= HS_REMOTE_MISSED_POLLS_LIMIT;
}

void hs_remote_lease_note_response(hs_remote_lease_t *lease,
                                   uint64_t safe_offset)
{
    if (!lease) return;
    lease->missed_polls = 0;
    if (safe_offset > lease->confirmed_safe_offset)
        lease->confirmed_safe_offset = safe_offset;
}

uint64_t hs_remote_lease_fallback_start(const hs_remote_lease_t *lease,
                                        uint64_t shard_start,
                                        uint64_t shard_end)
{
    if (!lease || lease->confirmed_safe_offset < shard_start)
        return shard_start;
    if (lease->confirmed_safe_offset > shard_end)
        return shard_end;
    return lease->confirmed_safe_offset;
}

bool hs_remote_not_found_completes_shard(uint64_t safe_offset,
                                         uint64_t shard_end)
{
    return safe_offset >= shard_end;
}
