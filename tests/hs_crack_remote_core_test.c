#include "hs_crack_remote_core.h"
#include "hs_crack_cache.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; \
} } while (0)

static void put_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void put_u64_le(uint8_t *output, uint64_t value)
{
    for (size_t i = 0; i < 8U; ++i)
        output[i] = (uint8_t)(value >> (i * 8U));
}

static void make_ack32(uint8_t frame[HS_REMOTE_ACK32_SIZE], uint32_t index,
                       uint8_t status, uint64_t offset)
{
    memset(frame, 0, HS_REMOTE_ACK32_SIZE);
    memcpy(frame, "FTA\x01", 4U);
    put_u32_le(frame + 4U, index);
    frame[8] = status;
    put_u64_le(frame + 12U, offset);
    put_u32_le(frame + 20U, hs_crack_cache_crc32_update(0, frame, 20U));
}

static void test_protocol_parser(void)
{
    hs_remote_message_t message;
    CHECK(hs_remote_parse_line(
        "[CRACK/1] CAPABILITIES protocol=2 sync=ftb1 storage=sd max_jobs=1", &message));
    CHECK(message.type == HS_REMOTE_CAPABILITIES);
    CHECK(message.protocol == 2);
    CHECK(strcmp(message.sync, "ftb1") == 0);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] FILE kind=wordlist state=present size=123456 crc32=A1B2C3D4 path=/sd/x", &message));
    CHECK(message.type == HS_REMOTE_FILE);
    CHECK(message.file_present);
    CHECK(message.size == 123456);
    CHECK(message.crc32 == 0xA1B2C3D4U);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=8192 rx_ms=1200 ack_size=32", &message));
    CHECK(message.type == HS_REMOTE_READY);
    CHECK(message.offset == 40);
    CHECK(message.prefix_crc32 == 0xABCDEF01U);
    CHECK(message.block_size == 8192);
    CHECK(message.timeout_ms == 1200);
    CHECK(message.ack_size == 32U);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=8192 rx_ms=1200", &message));
    CHECK(message.ack_size == 1U);
    CHECK(hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=8192 rx_ms=1200 ack_size=0", &message));
    CHECK(message.ack_size == 0U);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] DIAG phase=ack_sent reason=ok header_got=16 header_expected=16 payload_got=1024 payload_expected=1024 offset=508928 block=495 baud=115200 duplicates=0 rd=1485 rb=514800 sd=495 sb=506880 aa=495 as=495 cp=1 stage_us=472123000", &message));
    CHECK(message.type == HS_REMOTE_DIAG);
    CHECK(strcmp(message.phase, "ack_sent") == 0);
    CHECK(strcmp(message.reason, "ok") == 0);
    CHECK(message.header_got == 16);
    CHECK(message.header_expected == 16);
    CHECK(message.payload_got == 1024);
    CHECK(message.payload_expected == 1024);
    CHECK(message.offset == 508928);
    CHECK(message.block_index == 495);
    CHECK(message.baud == 115200);
    CHECK(message.diag_read_calls == 1485);
    CHECK(message.diag_read_bytes == 514800);
    CHECK(message.diag_sd_write_calls == 495);
    CHECK(message.diag_sd_write_bytes == 506880);
    CHECK(message.diag_ack_attempts == 495);
    CHECK(message.diag_acks_sent == 495);
    CHECK(message.diag_checkpoint_calls == 1);
    CHECK(message.diag_stage_us == 472123000);
    CHECK(!hs_remote_parse_line(
        "[CRACK/1] DIAG phase=payload reason=payload_timeout header_got=16", &message));

    CHECK(hs_remote_parse_line(
        "[CRACK/1] SYNC_ERROR code=block_timeout received=0 size=9101075", &message));
    CHECK(message.type == HS_REMOTE_SYNC_ERROR);
    CHECK(strcmp(message.code, "block_timeout") == 0);
    CHECK(message.received == 0);
    CHECK(message.size == 9101075);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] STATUS job=j-1 state=running checked=17 safe_offset=800 elapsed_ms=9000 rate_milli=1888", &message));
    CHECK(message.type == HS_REMOTE_STATUS);
    CHECK(strcmp(message.job, "j-1") == 0);
    CHECK(message.result == HS_REMOTE_RESULT_RUNNING);
    CHECK(message.checked == 17);
    CHECK(message.offset == 800);
    CHECK(message.phase[0] == '\0');
    CHECK(!message.progress_age_valid);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] STATUS job=j-1 state=running checked=18 safe_offset=812 elapsed_ms=9500 rate_milli=1894 phase=cracking progress_age_ms=37",
        &message));
    CHECK(strcmp(message.phase, "cracking") == 0);
    CHECK(message.progress_age_valid);
    CHECK(message.progress_age_ms == 37);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] CANCELLING job=j-1", &message));
    CHECK(message.type == HS_REMOTE_CANCELLING);
    CHECK(strcmp(message.job, "j-1") == 0);
    CHECK(message.result == HS_REMOTE_RESULT_NONE);
    CHECK(!hs_remote_parse_line("[CRACK/1] CANCELLING", &message));

    CHECK(hs_remote_parse_line(
        "[CRACK/1] DONE job=j-1 result=found checked=22 safe_offset=900 ssid_hex=54657374 password_hex=70617373776F7264", &message));
    CHECK(message.type == HS_REMOTE_DONE);
    CHECK(message.result == HS_REMOTE_RESULT_FOUND);
    CHECK(message.password_length == 8);
    CHECK(memcmp(message.password, "password", 8) == 0);

    CHECK(hs_remote_parse_line(
        "[CRACK/1] DONE job=j-2 result=not_found checked=42 safe_offset=1000", &message));
    CHECK(message.result == HS_REMOTE_RESULT_NOT_FOUND);
    CHECK(!hs_remote_parse_line("noise [CRACK/1] DONE result=found", &message));
    CHECK(!hs_remote_parse_line("[CRACK/2] STATUS job=x", &message));
    CHECK(!hs_remote_parse_line("[CRACK/1] READY size=oops", &message));
    CHECK(!hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=8192 rx_ms=1200 ack_size=oops", &message));
}

static void test_protocol_four_capabilities_require_exact_tokens(void)
{
    static const struct {
        const char *line;
        bool valid;
    } cases[] = {
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32", true},
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,xframe32 replay=last_block finish=fin32", false},
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame320 replay=last_block finish=fin32", false},
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32x replay=last_block finish=fin32", false},
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte replay=last_block finish=fin32", false},
        {"[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=frame32 replay=last_block finish=none", false},
        {"[CRACK/1] CAPABILITIES protocol=3 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32", false},
    };
    hs_remote_message_t message;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(hs_remote_parse_line(cases[i].line, &message));
        CHECK(hs_remote_capabilities_v4_valid(&message) == cases[i].valid);
    }

    CHECK(hs_remote_parse_line(
        "[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32",
        &message));
    CHECK(message.ack_frame32);
    CHECK(message.replay_last_block);
    CHECK(message.finish_fin32);
}

static void test_usb_ack32_ready_requires_fixed_protocol_four_timings(void)
{
    static const struct {
        const char *line;
        bool parse_ok;
        bool usb_valid;
    } cases[] = {
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2000 next_header_ms=7000 prepare_ms=10000 finish_linger_ms=7000", true, true},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2000 next_header_ms=7000 prepare_ms=600000 finish_linger_ms=7000", true, true},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=0 next_header_ms=7000 prepare_ms=10000 finish_linger_ms=7000", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2001 next_header_ms=7000 prepare_ms=10000 finish_linger_ms=7000", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2000 next_header_ms=7000 prepare_ms=9999 finish_linger_ms=7000", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2000 next_header_ms=7000 prepare_ms=600001 finish_linger_ms=7000", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=2000 next_header_ms=7000 prepare_ms=10000 finish_linger_ms=6999", true, false},
        {"[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=1024 rx_ms=1200 ack_size=32 ack_wait_ms=4294967296 next_header_ms=7000 prepare_ms=10000 finish_linger_ms=7000", false, false},
    };
    hs_remote_message_t message;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(hs_remote_parse_line(cases[i].line, &message) == cases[i].parse_ok);
        if (cases[i].parse_ok)
            CHECK(hs_remote_ready_valid(&message, true) == cases[i].usb_valid);
    }

    CHECK(hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=99 crc32=01020304 offset=40 prefix_crc=ABCDEF01 bsize=8192 rx_ms=1200",
        &message));
    CHECK(message.ack_size == 1U);
    CHECK(message.ack_wait_ms == 0U);
    CHECK(message.next_header_ms == 0U);
    CHECK(message.prepare_ms == 0U);
    CHECK(message.finish_linger_ms == 0U);
    CHECK(hs_remote_ready_valid(&message, false));

    CHECK(hs_remote_parse_line(
        "[CRACK/1] READY kind=wordlist size=15645263 crc32=01020304 offset=7176192 prefix_crc=30F79DB6 bsize=8192 rx_ms=1126 ack_size=1 prepare_ms=38000",
        &message));
    CHECK(message.ack_size == 1U);
    CHECK(message.prepare_ms == 38000U);
    CHECK(hs_remote_ready_valid(&message, false));
}

static void test_hex(void)
{
    unsigned char output[8];
    size_t length = 0;
    CHECK(hs_remote_hex_decode("410022FF", output, sizeof(output), &length));
    CHECK(length == 4 && output[0] == 'A' && output[1] == 0 &&
          output[2] == 0x22 && output[3] == 0xff);
    CHECK(!hs_remote_hex_decode("ABC", output, sizeof(output), &length));
    CHECK(!hs_remote_hex_decode("GG", output, sizeof(output), &length));
    CHECK(!hs_remote_hex_decode("001122334455667788", output, sizeof(output), &length));
}

static void test_receive_command_uses_smaller_blocks_only_for_usb_cdc(void)
{
    char command[128];
    CHECK(hs_remote_receive_block_size(true) == 1024U);
    CHECK(hs_remote_receive_block_size(false) == 8192U);

    CHECK(hs_remote_format_receive_command(
        command, sizeof(command), "capture", 11202, 0x57BF22AFU, true));
    CHECK(strcmp(command,
                 "crack_worker receive capture 11202 57BF22AF 1024 ack32") == 0);

    CHECK(hs_remote_format_receive_command(
        command, sizeof(command), "wordlist", 987654, 0x01020304U, false));
    CHECK(strcmp(command,
                 "crack_worker receive wordlist 987654 01020304 8192") == 0);
}

static void test_ack32_parser_and_matcher(void)
{
    uint8_t frame[HS_REMOTE_ACK32_SIZE];
    hs_remote_ack32_t reply;
    make_ack32(frame, 7U, 0x06U, 1024U);

    CHECK(hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(hs_remote_ack32_parse_error(frame, sizeof(frame)) == NULL);
    CHECK(strcmp(hs_remote_ack32_parse_error(NULL, sizeof(frame)), "null frame") == 0);
    CHECK(reply.block_index == 7U);
    CHECK(reply.status == 0x06U);
    CHECK(reply.committed_offset == 1024U);
    CHECK(hs_remote_ack32_matches(&reply, 7U, 1024U));
    CHECK(!hs_remote_ack32_matches(&reply, 6U, 1024U));
    CHECK(!hs_remote_ack32_matches(&reply, 7U, 2048U));

    frame[0] = 'X';
    CHECK(!hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(strcmp(hs_remote_ack32_parse_error(frame, sizeof(frame)), "magic/version") == 0);
    make_ack32(frame, 7U, 0x06U, 1024U);
    frame[20] ^= 1U;
    CHECK(!hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(strcmp(hs_remote_ack32_parse_error(frame, sizeof(frame)), "crc") == 0);
    make_ack32(frame, 7U, 0x07U, 1024U);
    CHECK(!hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(strcmp(hs_remote_ack32_parse_error(frame, sizeof(frame)), "status") == 0);

    /* Negative replies are valid frames, but must never advance progress. */
    make_ack32(frame, 7U, 0x15U, 0U);
    CHECK(hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(!hs_remote_ack32_matches(&reply, 7U, 1024U));
    make_ack32(frame, 7U, 0x18U, 0U);
    CHECK(hs_remote_ack32_parse(frame, sizeof(frame), &reply));
    CHECK(!hs_remote_ack32_matches(&reply, 7U, 1024U));

    make_ack32(frame, 7U, 0x06U, 1024U);
    CHECK(!hs_remote_ack32_parse(frame, sizeof(frame) - 1U, &reply));
    CHECK(!hs_remote_ack32_parse(frame, sizeof(frame) + 1U, &reply));
    CHECK(strcmp(hs_remote_ack32_parse_error(frame, sizeof(frame) - 1U), "size") == 0);
    /* Recompute CRC so reserved-byte rejection cannot pass via a bad CRC. */
    for (size_t i = 9U; i < sizeof(frame); ++i) {
        if (i >= 12U && i < 24U) continue;
        make_ack32(frame, 7U, 0x06U, 1024U);
        frame[i] = 1U;
        put_u32_le(frame + 20U, hs_crack_cache_crc32_update(0, frame, 20U));
        CHECK(!hs_remote_ack32_parse(frame, sizeof(frame), &reply));
        CHECK(strcmp(hs_remote_ack32_parse_error(frame, sizeof(frame)), "reserved bytes") == 0);
    }
}

static void test_ack_stream_retains_fragments_and_classifies_replies(void)
{
    uint8_t stale[HS_REMOTE_ACK32_SIZE];
    uint8_t current[HS_REMOTE_ACK32_SIZE];
    uint8_t malformed[HS_REMOTE_ACK32_SIZE];
    hs_remote_ack_stream_t stream = {0};
    hs_remote_ack32_t reply;
    make_ack32(stale, 6U, 0x06U, 1024U);
    make_ack32(current, 7U, 0x06U, 2048U);

    for (size_t split = 0; split <= HS_REMOTE_ACK32_SIZE; ++split) {
        stream.used = 0;
        memset(&reply, 0, sizeof(reply));
        CHECK(hs_remote_ack_stream_push(&stream, current, split, &reply) == split);
        CHECK(stream.used == (split == HS_REMOTE_ACK32_SIZE ? 0U : split));
        CHECK(hs_remote_ack_stream_push(&stream, current + split,
                                        HS_REMOTE_ACK32_SIZE - split, &reply) ==
              HS_REMOTE_ACK32_SIZE - split);
        CHECK(stream.used == 0U);
        CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
              HS_ACK_CURRENT);
    }

    stream.used = 0;
    memset(&reply, 0, sizeof(reply));
    CHECK(hs_remote_ack_stream_push(&stream, stale, HS_REMOTE_ACK32_SIZE, &reply) ==
          HS_REMOTE_ACK32_SIZE);
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) == HS_ACK_STALE);
    CHECK(hs_remote_ack_stream_push(&stream, current, 7U, &reply) == 7U);
    CHECK(stream.used == 7U);
    CHECK(hs_remote_tx_on_deadline(1U) == HS_TX_RETRANSMIT);
    CHECK(stream.used == 7U);
    CHECK(hs_remote_ack_stream_push(&stream, current + 7U,
                                    HS_REMOTE_ACK32_SIZE - 7U, &reply) ==
          HS_REMOTE_ACK32_SIZE - 7U);
    CHECK(stream.used == 0U);
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_CURRENT);

    make_ack32(malformed, 7U, 0x06U, 2048U);
    malformed[0] = 'X';
    memset(&reply, 0xA5, sizeof(reply));
    CHECK(hs_remote_ack_stream_push(&stream, malformed, sizeof(malformed), &reply) ==
          sizeof(malformed));
    CHECK(stream.used == 0U);
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_INVALID);

    make_ack32(current, 7U, 0x15U, 1024U);
    CHECK(hs_remote_ack32_parse(current, sizeof(current), &reply));
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_RETRY_NAK);
    make_ack32(current, 7U, 0x06U, 1024U);
    CHECK(hs_remote_ack32_parse(current, sizeof(current), &reply));
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_INVALID);

    for (unsigned i = 0; i < 5U; ++i) {
        make_ack32(stale, 6U, 0x06U, 1024U);
        CHECK(hs_remote_ack32_parse(stale, sizeof(stale), &reply));
        CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
              HS_ACK_STALE);
    }
    make_ack32(stale, 6U, 0x06U, 1023U);
    CHECK(hs_remote_ack32_parse(stale, sizeof(stale), &reply));
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_INVALID);
    make_ack32(stale, UINT32_MAX, 0x06U, 0U);
    CHECK(hs_remote_ack32_parse(stale, sizeof(stale), &reply));
    CHECK(hs_remote_ack_classify(&reply, 0U, 0U, 1024U, true) ==
          HS_ACK_INVALID);
    make_ack32(stale, 8U, 0x06U, 2048U);
    CHECK(hs_remote_ack32_parse(stale, sizeof(stale), &reply));
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) ==
          HS_ACK_INVALID);
    make_ack32(stale, 7U, 0x18U, 0U);
    CHECK(hs_remote_ack32_parse(stale, sizeof(stale), &reply));
    CHECK(hs_remote_ack_classify(&reply, 7U, 1024U, 2048U, true) == HS_ACK_CAN);
}

static void test_ack_deadline_allows_exactly_three_transmissions(void)
{
    CHECK(hs_remote_tx_on_deadline(0U) == HS_TX_RETRANSMIT);
    CHECK(hs_remote_tx_on_deadline(1U) == HS_TX_RETRANSMIT);
    CHECK(hs_remote_tx_on_deadline(2U) == HS_TX_RETRANSMIT);
    CHECK(hs_remote_tx_on_deadline(3U) == HS_TX_FAIL);
    CHECK(hs_remote_tx_on_deadline(4U) == HS_TX_FAIL);
}

static void test_probe_timeout_allows_slow_sd_validation(void)
{
    CHECK(hs_remote_probe_timeout_ms(0) == 30000U);
    CHECK(hs_remote_probe_timeout_ms(22690149U) == 362000U);
    CHECK(hs_remote_probe_timeout_ms(UINT64_MAX) == 600000U);
    CHECK(HS_REMOTE_PROBE_TIMEOUT_MS == 5000U);
    CHECK(hs_remote_retry_at_default_baud(true, false, false));
    CHECK(!hs_remote_retry_at_default_baud(false, false, false));
    CHECK(!hs_remote_retry_at_default_baud(true, true, false));
    CHECK(!hs_remote_retry_at_default_baud(true, false, true));
}

static void test_worker_stage_retry_is_bounded_and_backed_off(void)
{
    CHECK(HS_REMOTE_STAGE_ATTEMPTS == 3U);
    CHECK(hs_remote_stage_retry_allowed(1U, false, true));
    CHECK(hs_remote_stage_retry_allowed(2U, false, true));
    CHECK(!hs_remote_stage_retry_allowed(3U, false, true));
    CHECK(!hs_remote_stage_retry_allowed(1U, true, true));
    CHECK(!hs_remote_stage_retry_allowed(1U, false, false));
    CHECK(hs_remote_stage_backoff_ms(1U) == 0U);
    CHECK(hs_remote_stage_backoff_ms(2U) == 250U);
    CHECK(hs_remote_stage_backoff_ms(3U) == 1000U);
    CHECK(hs_remote_stage_backoff_ms(4U) == 0U);
}

static void test_command_frame_is_one_complete_cli_line(void)
{
    char framed[96];
    const char *command =
        "crack_worker probe wordlist 15645263 30F79DB6";
    CHECK(hs_remote_frame_command(framed, sizeof(framed), command));
    CHECK(strcmp(framed,
                 "crack_worker probe wordlist 15645263 30F79DB6\r\n") == 0);

    size_t exact = strlen(command) + 3U;
    CHECK(hs_remote_frame_command(framed, exact, command));
    CHECK(!hs_remote_frame_command(framed, exact - 1U, command));
    CHECK(!hs_remote_frame_command(framed, sizeof(framed), "bad\ncommand"));
    CHECK(!hs_remote_frame_command(NULL, sizeof(framed), command));
    CHECK(!hs_remote_frame_command(framed, sizeof(framed), NULL));
}

static void test_shards(void)
{
    hs_remote_shard_t shards[4];
    CHECK(hs_remote_make_shards(100, 1103, 4, shards));
    CHECK(shards[0].start == 100);
    CHECK(shards[3].end == 1103);
    for (int i = 0; i < 4; ++i) {
        CHECK(shards[i].start < shards[i].end);
        if (i > 0) CHECK(shards[i - 1].end == shards[i].start);
    }
    CHECK((shards[0].end - shards[0].start) == 251);
    CHECK((shards[3].end - shards[3].start) == 250);

    CHECK(hs_remote_make_shards(0, 2, 4, shards));
    CHECK(shards[0].start == 0 && shards[0].end == 1);
    CHECK(shards[1].start == 1 && shards[1].end == 2);
    CHECK(shards[2].start == 2 && shards[2].end == 2);
    CHECK(!hs_remote_make_shards(5, 4, 2, shards));
    CHECK(!hs_remote_make_shards(0, 1, 0, shards));
}

static void test_worker_lease_requires_three_consecutive_misses(void)
{
    hs_remote_lease_t lease;
    hs_remote_lease_init(&lease, 100);

    CHECK(!hs_remote_lease_note_timeout(&lease));
    CHECK(!hs_remote_lease_note_timeout(&lease));
    hs_remote_lease_note_response(&lease, 240);
    CHECK(lease.missed_polls == 0);
    CHECK(lease.confirmed_safe_offset == 240);

    CHECK(!hs_remote_lease_note_timeout(&lease));
    CHECK(!hs_remote_lease_note_timeout(&lease));
    CHECK(hs_remote_lease_note_timeout(&lease));
}

static void test_worker_lease_keeps_monotonic_safe_offset(void)
{
    hs_remote_lease_t lease;
    hs_remote_lease_init(&lease, 100);
    hs_remote_lease_note_response(&lease, 240);
    hs_remote_lease_note_response(&lease, 220);
    CHECK(lease.confirmed_safe_offset == 240);
    CHECK(hs_remote_lease_fallback_start(&lease, 100, 300) == 240);

    hs_remote_lease_note_response(&lease, 350);
    CHECK(hs_remote_lease_fallback_start(&lease, 100, 300) == 300);
    CHECK(!hs_remote_not_found_completes_shard(299, 300));
    CHECK(hs_remote_not_found_completes_shard(300, 300));
    CHECK(hs_remote_not_found_completes_shard(320, 300));
}

int main(void)
{
    test_protocol_parser();
    test_protocol_four_capabilities_require_exact_tokens();
    test_usb_ack32_ready_requires_fixed_protocol_four_timings();
    test_hex();
    test_receive_command_uses_smaller_blocks_only_for_usb_cdc();
    test_ack32_parser_and_matcher();
    test_ack_stream_retains_fragments_and_classifies_replies();
    test_ack_deadline_allows_exactly_three_transmissions();
    test_probe_timeout_allows_slow_sd_validation();
    test_worker_stage_retry_is_bounded_and_backed_off();
    test_command_frame_is_one_complete_cli_line();
    test_shards();
    test_worker_lease_requires_three_consecutive_misses();
    test_worker_lease_keeps_monotonic_safe_offset();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("hs_crack_remote_core_test: PASS");
    return 0;
}
