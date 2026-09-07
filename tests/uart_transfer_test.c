#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "janos_file_transfer.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_INVALID_RESPONSE 5
#define ESP_ERR_INVALID_CRC 6
#define TAG "test"
static void test_log(const char *tag, const char *fmt, ...) {}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define JANOS_UART_FT_BLOCK_MAX 4096
#define JANOS_UART_FT_HEADER_BYTES 16
#define JANOS_UART_FT_RETRIES 3
#define JANOS_UART_FT_ACK 6
#define JANOS_UART_FT_NAK 21
#define JANOS_UART_FT_CAN 24
#define JANOS_TRANSFER_LOCAL_ROOT "storage/imported"
#define pdMS_TO_TICKS(x) (x)
typedef int tab_id_t;
typedef int uart_port_t;
enum { TAB_GROVE, TAB_USB, TAB_MBUS };
static int sent, cancelled, drained, fail_alloc, scenario, failures;
static int64_t tick;
static char command[400];
static int reads;
static volatile bool *cancel_on_header;
static int64_t esp_timer_get_time(void) { return tick += 1000000; }
static void compromised_transport_flush(int t, int p) {}
static int transport_write_bytes_tab(int t, int p, const char *s, size_t n) {
    if (n == 1 && *s == JANOS_UART_FT_CAN) cancelled++;
    if (n > 9 && !strncmp(s, "send_file", 9)) { sent++; snprintf(command, sizeof(command), "%s", s); }
    return n;
}
static bool janos_uart_wait_marker(int t, int p, const char *m, const char *prefix,
                                  char *out, size_t n, uint32_t timeout) {
    if (cancelled) { drained++; return true; }
    if (cancel_on_header) *cancel_on_header = true;
    if (out) snprintf(out, n, prefix && !strcmp(prefix, "[FT] done")
                     ? "[FT] done crc32=9BE3E0A3" : "[FT] begin size=4 crc32=9BE3E0A3");
    return scenario != 2;
}
static bool janos_uart_sync_magic(int t, int p, uint8_t *h, uint32_t ms) {
    if (scenario < 3) return false;
    memcpy(h, "FTB\1", 4); return true;
}
static bool janos_uart_read_line(int t, int p, char *s, size_t n, uint32_t ms) { *s=0; return false; }
static int transport_read_bytes_tab(int t, int p, uint8_t *b, size_t n, int ms) {
    if (reads++ == 0) {
        /* index=0, length=4, CRC32 of the literal payload 1234. */
        const uint8_t header[] = {0,0,0,0,4,0,0,0,0xA3,0xE0,0xE3,0x9B};
        memcpy(b, header, n);
        if (scenario == 4) b[4]=0;
        if (scenario == 5) {
            const uint8_t suffix_header[] = {0,0,0,0,2,0,0,0,0x7A,0x83,0x06,0x94};
            memcpy(b, suffix_header, n);
        }
    } else memcpy(b, scenario == 5 ? "34" : "1234", n);
    return n;
}
static void janos_uart_emit_progress(janos_file_transfer_state_t s, uint64_t a,
                                    uint64_t b, uint32_t c, esp_err_t e, const char *m) {}
static void *test_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
#define malloc test_malloc
/* PRODUCTION */
#undef malloc
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } } while (0)
static void reset(void) { sent=cancelled=drained=fail_alloc=scenario=reads=0; tick=0; cancel_on_header=NULL; }
int main(void) {
    janos_file_transfer_result_t result;
    reset();
    int err = janos_uart_download(TAB_MBUS, 0, "/remote", "new/sub/file", 4, NULL, &result);
    CHECK(err == ESP_ERR_INVALID_RESPONSE && sent == 1, "missing directories must be created before receiving");
    CHECK(cancelled == 1 && drained == 1, "broken stream must cancel and drain before returning");
    reset();
    FILE *f = fopen("obstacle", "wb"); fclose(f);
    err = janos_uart_download(TAB_MBUS, 0, "/remote", "obstacle/file", 4, NULL, &result);
    CHECK(err == ESP_FAIL && sent == 0, "filesystem error must not be NO_MEM or start remote transfer");
    reset(); fail_alloc=1;
    err = janos_uart_download(TAB_MBUS, 0, "/remote", "allocation", 4, NULL, &result);
    CHECK(err == ESP_ERR_NO_MEM && sent == 0, "allocation failure must happen before send_file");
    reset(); scenario=2;
    err = janos_uart_download(TAB_MBUS, 0, "/remote", "timeout", 4, NULL, &result);
    CHECK(err != ESP_OK && cancelled == 1 && drained == 1, "header timeout must cancel and drain");
    reset(); volatile bool cancel=true;
    err = janos_uart_download(TAB_MBUS, 0, "/remote", "cancel", 4, &cancel, &result);
    CHECK(err == ESP_ERR_INVALID_STATE && (sent == 0 || (cancelled == 1 && drained == 1)), "user cancellation must leave remote idle");
    mkdir("storage", 0755); mkdir("storage/imported", 0755); mkdir("storage/imported/mbus", 0755);
    f=fopen("storage/imported/mbus/capture.pcap.part", "wb"); fputs("ab", f); fclose(f);
    char path[256];
    CHECK(janos_transfer_build_unique_local_path(TAB_MBUS, "capture.pcap", path, sizeof(path)), "select resume path");
    CHECK(!strcmp(path, "storage/imported/mbus/capture.pcap"), "reuse partial file instead of adding suffix");
    reset();
    janos_uart_download(TAB_MBUS, 0, "/remote", path, 4, NULL, &result);
    CHECK(strstr(command, "/remote 2\r\n") && result.bytes_written == 2, "resume offset and retained count must match disk");
    reset(); scenario=3;
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "complete", 4, NULL, &result);
    CHECK(err == ESP_OK && result.bytes_written == 4 && !cancelled, "valid transfer must commit without cancelling");
    char data[5]={0}; f=fopen("complete", "rb");
    if (f) { fread(data, 1, 4, f); fclose(f); }
    CHECK(!strcmp(data, "1234") && access("complete.part", F_OK) != 0, "commit exact bytes and remove part by rename");
    reset(); scenario=4;
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "malformed", 4, NULL, &result);
    CHECK(err == ESP_ERR_INVALID_RESPONSE && cancelled == 1 && drained == 1, "malformed block must cancel and drain");
    reset(); cancel=false; cancel_on_header=&cancel;
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "active_cancel", 4, &cancel, &result);
    CHECK(err == ESP_ERR_INVALID_STATE && sent == 1 && cancelled == 1 && drained == 1, "active user cancellation must finish protocol");
    reset(); scenario=3;
    f=fopen("stale.part", "wb"); fputs("corrupt", f); fclose(f);
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "stale", 4, NULL, &result);
    CHECK(err == ESP_OK && strstr(command, "/remote 0\r\n"), "oversized partial must restart instead of trapping retries");
    reset(); scenario=3;
    f=fopen("badcrc.part", "wb"); fputs("bad!", f); fclose(f);
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "badcrc", 4, NULL, &result);
    CHECK(err == ESP_OK && strstr(command, "/remote 0\r\n"), "full partial after CRC failure must restart");
    reset(); scenario=5;
    f=fopen("resume.part", "wb"); fputs("12", f); fclose(f);
    err=janos_uart_download(TAB_MBUS, 0, "/remote", "resume", 4, NULL, &result);
    CHECK(err == ESP_OK && result.bytes_written == 4 && strstr(command, "/remote 2\r\n"), "valid partial must append suffix and verify full-file CRC");
    memset(data, 0, sizeof(data)); f=fopen("resume", "rb");
    if (f) { fread(data, 1, 4, f); fclose(f); }
    CHECK(!strcmp(data, "1234"), "resumed file must preserve prefix and append suffix exactly once");
    printf("UART transfer regression tests: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
