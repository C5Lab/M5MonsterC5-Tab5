#define _POSIX_C_SOURCE 200809L
#include "hs_capture_validation.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char capture_path[] = "/tmp/hs-validation-test-XXXXXX";

int main(void)
{
    int fd = mkstemp(capture_path);
    assert(fd >= 0 && close(fd) == 0);

    char sidecar[384];
    assert(hs_capture_validation_sidecar_path(
        capture_path, sidecar, sizeof(sidecar)));

    hs_capture_validation_entry_t saved = {
        .identity = {
            .size = 11202,
            .crc32 = 0x57BF22AF,
            .validator_version = HS_CAPTURE_VALIDATOR_VERSION,
            .format = HS_CAPTURE_FILE_PCAP,
        },
        .report = {
            .state = HS_CAPTURE_READY,
            .reason = HS_CAPTURE_REASON_OK,
            .packet_count = 9,
            .eapol_count = 2,
            .ap_nonce_count = 1,
            .sta_response_count = 1,
            .record_count = 1,
        },
    };
    assert(hs_capture_validation_save(capture_path, &saved) ==
           HS_CAPTURE_VALIDATION_OK);

    hs_capture_validation_entry_t loaded = {0};
    assert(hs_capture_validation_load(capture_path, &saved.identity, &loaded) ==
           HS_CAPTURE_VALIDATION_OK);
    assert(memcmp(&loaded, &saved, sizeof(saved)) == 0);

    hs_capture_validation_identity_t changed = saved.identity;
    changed.crc32++;
    assert(hs_capture_validation_load(capture_path, &changed, &loaded) ==
           HS_CAPTURE_VALIDATION_STALE);

    FILE *file = fopen(sidecar, "r+b");
    assert(file && fputc('X', file) != EOF && fclose(file) == 0);
    assert(hs_capture_validation_load(capture_path, &saved.identity, &loaded) ==
           HS_CAPTURE_VALIDATION_CORRUPT);

    assert(hs_capture_validation_save(capture_path, &saved) ==
           HS_CAPTURE_VALIDATION_OK);
    memset(&loaded, 0, sizeof(loaded));
    assert(hs_capture_validation_load(capture_path, &saved.identity, &loaded) ==
           HS_CAPTURE_VALIDATION_OK);
    assert(memcmp(&loaded, &saved, sizeof(saved)) == 0);

    assert(unlink(sidecar) == 0);
    assert(unlink(capture_path) == 0);
    puts("hs_capture_validation_test: PASS");
    return 0;
}
