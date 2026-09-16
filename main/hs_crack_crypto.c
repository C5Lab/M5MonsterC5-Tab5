/* Compile the installed mbedTLS implementation with a private configuration
 * and namespace. Its upstream license remains in sha1.c. No SHA hardware lock
 * is acquired by this instance; each caller owns its entire state. */
#undef MBEDTLS_CONFIG_FILE
#define MBEDTLS_CONFIG_FILE "hs_crack_sha1_config.h"
#define mbedtls_sha1_context hs_sw_sha1_context
#define mbedtls_sha1_init hs_sw_sha1_init
#define mbedtls_sha1_free hs_sw_sha1_free
#define mbedtls_sha1_clone hs_sw_sha1_clone
#define mbedtls_sha1_starts hs_sw_sha1_starts
#define mbedtls_sha1_update hs_sw_sha1_update
#define mbedtls_sha1_finish hs_sw_sha1_finish
#define mbedtls_internal_sha1_process hs_sw_internal_sha1_process
#define mbedtls_sha1 hs_sw_sha1
#define mbedtls_sha1_self_test hs_sw_sha1_self_test
#include "sha1.c"
#include "hs_crack_crypto.h"

typedef struct {
    mbedtls_sha1_context inner, outer;
} hs_hmac_seed;

static int hs_seed(hs_hmac_seed *seed, const uint8_t *key, size_t len)
{
    uint8_t pad[64] = {0};
    int rc = 0;
    if (len > sizeof(pad)) rc = mbedtls_sha1(key, len, pad);
    else if (len) memcpy(pad, key, len);
    if (rc) goto done;
    for (size_t i = 0; i < sizeof(pad); i++) pad[i] ^= 0x36;
    mbedtls_sha1_init(&seed->inner);
    mbedtls_sha1_init(&seed->outer);
    rc = mbedtls_sha1_starts(&seed->inner);
    if (!rc) rc = mbedtls_sha1_update(&seed->inner, pad, sizeof(pad));
    for (size_t i = 0; i < sizeof(pad); i++) pad[i] ^= 0x36 ^ 0x5c;
    if (!rc) rc = mbedtls_sha1_starts(&seed->outer);
    if (!rc) rc = mbedtls_sha1_update(&seed->outer, pad, sizeof(pad));
done:
    mbedtls_platform_zeroize(pad, sizeof(pad));
    return rc;
}

static int hs_seed_hmac(const hs_hmac_seed *seed, const uint8_t *data,
                        size_t len, uint8_t out[20])
{
    mbedtls_sha1_context ctx;
    uint8_t digest[20];
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_clone(&ctx, &seed->inner);
    int rc = mbedtls_sha1_update(&ctx, data, len);
    if (!rc) rc = mbedtls_sha1_finish(&ctx, digest);
    mbedtls_sha1_clone(&ctx, &seed->outer);
    if (!rc) rc = mbedtls_sha1_update(&ctx, digest, sizeof(digest));
    if (!rc) rc = mbedtls_sha1_finish(&ctx, out);
    mbedtls_sha1_free(&ctx);
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return rc;
}

int hs_crack_hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len, uint8_t out[20])
{
    if (!out) return -2;
    if ((!key && key_len) || (!data && data_len)) {
        mbedtls_platform_zeroize(out, 20);
        return -2;
    }
    hs_hmac_seed seed = {0};
    int rc = hs_seed(&seed, key, key_len);
    if (!rc) rc = hs_seed_hmac(&seed, data, data_len, out);
    mbedtls_platform_zeroize(&seed, sizeof(seed));
    if (rc) mbedtls_platform_zeroize(out, 20);
    return rc ? -2 : 0;
}

int hs_crack_derive_pmk(const char *password, const uint8_t *ssid, size_t ssid_len,
                       uint8_t pmk[32], hs_crack_checkpoint_fn checkpoint, void *arg)
{
    if (!pmk) return -2;
    memset(pmk, 0, 32);
    if (!password || ssid_len > 32 || (!ssid && ssid_len)) return -2;
    size_t len = 0;
    while (len < 64 && password[len]) len++;
    if (len < 8 || len > 63) return -2;
    hs_hmac_seed seed = {0};
    uint8_t salt[36] = {0}, u[20] = {0}, sum[20] = {0};
    int rc = -1;
    if (checkpoint && !checkpoint(arg)) goto done;
    rc = -2;
    if (hs_seed(&seed, (const uint8_t *)password, len)) goto done;
    if (ssid_len) memcpy(salt, ssid, ssid_len);
    for (unsigned block = 1; block <= 2; block++) {
        salt[ssid_len + 3] = (uint8_t)block;
        if (hs_seed_hmac(&seed, salt, ssid_len + 4, u)) goto done;
        memcpy(sum, u, sizeof(sum));
        for (unsigned iteration = 2; iteration <= 4096; iteration++) {
            if (hs_seed_hmac(&seed, u, sizeof(u), u)) goto done;
            for (unsigned j = 0; j < sizeof(sum); j++) sum[j] ^= u[j];
            if ((iteration % 32) == 0 && checkpoint && !checkpoint(arg)) {
                rc = -1;
                goto done;
            }
        }
        memcpy(pmk + (block - 1) * 20, sum, block == 1 ? 20 : 12);
    }
    rc = 0;
done:
    mbedtls_platform_zeroize(&seed, sizeof(seed));
    mbedtls_platform_zeroize(salt, sizeof(salt));
    mbedtls_platform_zeroize(u, sizeof(u));
    mbedtls_platform_zeroize(sum, sizeof(sum));
    if (rc) mbedtls_platform_zeroize(pmk, 32);
    return rc;
}
