#ifndef TRANSPORT_RECORDER_BOOTLOADER_APP_H
#define TRANSPORT_RECORDER_BOOTLOADER_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "bootloader_storage.h"
#include "ota_package.h"

typedef struct
{
    uint32_t initial_msp;
    uint32_t reset_handler;
} bootloader_application_vectors_t;

bool bootloader_application_vectors_read_and_validate(
    const bootloader_storage_t *storage, bootloader_application_vectors_t *vectors);
bool bootloader_package_validate_from_qspi(const bootloader_storage_t *storage,
                                           uint32_t package_offset,
                                           uint32_t slot_size_bytes,
                                           ota_package_manifest_t *manifest);

#endif
