#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct sha256_state {uint64_t length;uint32_t state[8],curlen;unsigned char buf[64];} mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *ctx);
void mbedtls_sha256_free(mbedtls_sha256_context *ctx);
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx,int is224);
int mbedtls_sha256_update(mbedtls_sha256_context *ctx,const unsigned char *data,size_t length);
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,unsigned char out[32]);
