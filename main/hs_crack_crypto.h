#ifndef HS_CRACK_CRYPTO_H
#define HS_CRACK_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef bool (*hs_crack_checkpoint_fn)(void *arg);
/* 0 success, -1 cancelled, -2 invalid input/crypto error. */
int hs_crack_derive_pmk(const char *password, const uint8_t *ssid, size_t ssid_len,
                       uint8_t pmk[32], hs_crack_checkpoint_fn checkpoint, void *arg);
int hs_crack_hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len, uint8_t out[20]);
#endif
