#ifndef TRANSPORT_RECORDER_OTA_CRC32_H
#define TRANSPORT_RECORDER_OTA_CRC32_H

#include <stdint.h>

typedef struct
{
    uint32_t value;
} ota_crc32_t;

void ota_crc32_init(ota_crc32_t *context);
void ota_crc32_update(ota_crc32_t *context, const uint8_t *data, uint32_t length);
uint32_t ota_crc32_final(const ota_crc32_t *context);
uint32_t ota_crc32_compute(const uint8_t *data, uint32_t length);

#endif
