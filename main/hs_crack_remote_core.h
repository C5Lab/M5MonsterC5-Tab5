#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HS_REMOTE_UNKNOWN = 0,
    HS_REMOTE_CAPABILITIES,
    HS_REMOTE_FILE,
    HS_REMOTE_READY,
    HS_REMOTE_SYNCED,
    HS_REMOTE_ACCEPTED,
    HS_REMOTE_STARTED,
    HS_REMOTE_CANCELLING,
    HS_REMOTE_STATUS,
    HS_REMOTE_DONE,
    HS_REMOTE_RESET,
    HS_REMOTE_REJECTED,
    HS_REMOTE_SYNC_ERROR,
    HS_REMOTE_DIAG,
    HS_REMOTE_END,
} hs_remote_message_type_t;

typedef enum {
    HS_REMOTE_RESULT_NONE = 0,
    HS_REMOTE_RESULT_RUNNING,
    HS_REMOTE_RESULT_FOUND,
    HS_REMOTE_RESULT_NOT_FOUND,
    HS_REMOTE_RESULT_CANCELLED,
    HS_REMOTE_RESULT_ERROR,
    HS_REMOTE_RESULT_IDLE,
} hs_remote_result_t;

typedef struct {
    hs_remote_message_type_t type;
    hs_remote_result_t result;
    unsigned protocol;
    bool file_present;
    char sync[12];
    char kind[12];
    char job[32];
    char code[32];
    char phase[24];
    char reason[32];
    uint64_t size;
    uint64_t received;
    uint64_t offset;
    uint64_t header_got;
    uint64_t header_expected;
    uint64_t payload_got;
    uint64_t payload_expected;
    uint64_t block_index;
    uint64_t checked;
    uint64_t elapsed_ms;
    uint64_t rate_milli;
    uint64_t progress_age_ms;
    bool progress_age_valid;
    uint64_t diag_read_calls;
    uint64_t diag_read_bytes;
    uint64_t diag_sd_write_calls;
    uint64_t diag_sd_write_bytes;
    uint64_t diag_ack_attempts;
    uint64_t diag_acks_sent;
    uint64_t diag_checkpoint_calls;
    uint64_t diag_stage_us;
    uint32_t crc32;
    uint32_t prefix_crc32;
    uint32_t block_size;
    uint32_t ack_size;
    bool ack_frame32;
    bool replay_last_block;
    bool finish_fin32;
    uint32_t ack_wait_ms;
    uint32_t next_header_ms;
    uint32_t prepare_ms;
    uint32_t finish_linger_ms;
    uint32_t timeout_ms;
    uint32_t baud;
    uint8_t password[63];
    size_t password_length;
    uint8_t ssid[32];
    size_t ssid_length;
} hs_remote_message_t;

#define HS_REMOTE_ACK32_SIZE 32U

typedef struct {
    uint32_t block_index;
    uint8_t status;
    uint64_t committed_offset;
} hs_remote_ack32_t;

typedef struct {
    uint8_t bytes[HS_REMOTE_ACK32_SIZE];
    size_t used;
} hs_remote_ack_stream_t;

typedef enum {
    HS_ACK_CURRENT,
    HS_ACK_RETRY_NAK,
    HS_ACK_STALE,
    HS_ACK_CAN,
    HS_ACK_INVALID
} hs_remote_ack_action_t;

typedef enum {
    HS_TX_WAIT,
    HS_TX_RETRANSMIT,
    HS_TX_ACCEPT,
    HS_TX_FAIL
} hs_remote_tx_action_t;

typedef struct {
    uint64_t start;
    uint64_t end;
} hs_remote_shard_t;

#define HS_REMOTE_MISSED_POLLS_LIMIT 3U
#define HS_REMOTE_STAGE_ATTEMPTS 3U

typedef struct {
    unsigned missed_polls;
    uint64_t confirmed_safe_offset;
} hs_remote_lease_t;

bool hs_remote_hex_decode(const char *hex, uint8_t *output, size_t capacity,
                          size_t *output_length);
uint32_t hs_remote_receive_block_size(bool usb_cdc);
uint32_t hs_remote_probe_timeout_ms(uint64_t size);
#define HS_REMOTE_PROBE_TIMEOUT_MS 5000U
#define HS_REMOTE_USB_PROBE_TIMEOUT_MS HS_REMOTE_PROBE_TIMEOUT_MS
bool hs_remote_retry_at_default_baud(bool fast_active, bool synchronized,
                                     bool cancelled);
bool hs_remote_stage_retry_allowed(unsigned completed_attempts,
                                   bool cancelled, bool boundary_safe);
uint32_t hs_remote_stage_backoff_ms(unsigned next_attempt);
bool hs_remote_frame_command(char *output, size_t output_size,
                             const char *command);
bool hs_remote_format_receive_command(char *output, size_t output_size,
                                      const char *kind, uint64_t size,
                                      uint32_t crc32, bool usb_cdc);
bool hs_remote_parse_line(const char *line, hs_remote_message_t *message);
/* NULL means valid; otherwise returns a static, printable validation reason. */
const char *hs_remote_ack32_parse_error(const uint8_t *frame, size_t size);
bool hs_remote_ack32_parse(const uint8_t *frame, size_t size,
                           hs_remote_ack32_t *reply);
bool hs_remote_ack32_matches(const hs_remote_ack32_t *reply,
                             uint32_t expected_index,
                             uint64_t expected_offset);
bool hs_remote_capabilities_v4_valid(const hs_remote_message_t *message);
bool hs_remote_ready_valid(const hs_remote_message_t *message, bool usb_ack32);
size_t hs_remote_ack_stream_push(hs_remote_ack_stream_t *stream,
                                 const uint8_t *input, size_t length,
                                 hs_remote_ack32_t *complete);
hs_remote_ack_action_t hs_remote_ack_classify(
    const hs_remote_ack32_t *ack, uint32_t current_index,
    uint64_t before_offset, uint64_t after_offset, bool allow_previous);
hs_remote_tx_action_t hs_remote_tx_on_deadline(unsigned transmissions);
bool hs_remote_make_shards(uint64_t start, uint64_t end, size_t count,
                           hs_remote_shard_t *shards);
void hs_remote_lease_init(hs_remote_lease_t *lease, uint64_t shard_start);
bool hs_remote_lease_note_timeout(hs_remote_lease_t *lease);
void hs_remote_lease_note_response(hs_remote_lease_t *lease,
                                   uint64_t safe_offset);
uint64_t hs_remote_lease_fallback_start(const hs_remote_lease_t *lease,
                                        uint64_t shard_start,
                                        uint64_t shard_end);
bool hs_remote_not_found_completes_shard(uint64_t safe_offset,
                                         uint64_t shard_end);
