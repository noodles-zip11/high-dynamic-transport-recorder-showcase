#ifndef TRANSPORT_RECORDER_OTA_SHA256_H
#define TRANSPORT_RECORDER_OTA_SHA256_H

#include <stdint.h>

#define OTA_SHA256_BLOCK_BYTES 64U
#define OTA_SHA256_DIGEST_BYTES 32U

typedef struct
{
    uint32_t state[8];
    uint64_t total_length_bytes;
    uint8_t block[OTA_SHA256_BLOCK_BYTES];
    uint32_t block_length;
} ota_sha256_t;

void ota_sha256_init(ota_sha256_t *context);
void ota_sha256_update(ota_sha256_t *context, const uint8_t *data, uint32_t length);
void ota_sha256_final(ota_sha256_t *context, uint8_t digest[OTA_SHA256_DIGEST_BYTES]);

#endif
