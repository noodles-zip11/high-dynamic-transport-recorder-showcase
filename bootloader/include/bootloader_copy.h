#ifndef TRANSPORT_RECORDER_BOOTLOADER_COPY_H
#define TRANSPORT_RECORDER_BOOTLOADER_COPY_H

#include <stdbool.h>
#include <stdint.h>

#include "bootloader_storage.h"
#include "ota_state.h"
#include "ota_sha256.h"

#define BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES UINT32_C(0x00020000)
#define BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES UINT32_C(32)

typedef struct
{
    uint32_t qspi_image_offset;
    uint32_t image_length;
    uint32_t maximum_copy_bytes;
    uint32_t expected_image_crc32;
    uint8_t expected_image_sha256[OTA_SHA256_DIGEST_BYTES];
} bootloader_copy_request_t;

typedef struct
{
    ota_state_record_t next_state;
    bool complete;
} bootloader_copy_result_t;

bool bootloader_copy_engine_run(const bootloader_storage_t *storage,
                                const bootloader_copy_request_t *request,
                                const ota_state_record_t *installing_state,
                                uint8_t *scratch, uint32_t scratch_size,
                                bootloader_copy_result_t *result);

#endif
