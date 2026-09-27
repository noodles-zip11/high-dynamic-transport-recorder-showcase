#include <stdbool.h>
#include <string.h>

#include "bootloader_copy.h"
#include "bootloader_state_store.h"
#include "memory_layout.h"

static bool bootloader_state_store_can_read(const bootloader_storage_t *storage)
{
    return storage != NULL && storage->internal_flash.read != NULL;
}

static bool bootloader_state_store_can_write(const bootloader_storage_t *storage)
{
    return storage != NULL && storage->internal_flash.erase != NULL
           && storage->internal_flash.program != NULL;
}

ota_state_selection_t bootloader_state_store_load(const bootloader_storage_t *storage,
                                                  ota_state_record_t *state)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];

    if (state == NULL || !bootloader_state_store_can_read(storage)
        || !storage->internal_flash.read(storage->internal_flash.context,
                                         TRANSPORT_OTA_STATE_PRIMARY_BASE, primary,
                                         sizeof(primary))
        || !storage->internal_flash.read(storage->internal_flash.context,
                                         TRANSPORT_OTA_STATE_SECONDARY_BASE, secondary,
                                         sizeof(secondary)))
    {
        return OTA_STATE_SELECT_RECOVERY;
    }
    return ota_state_select_newest(primary, secondary, state);
}

bool bootloader_state_store_persist(const bootloader_storage_t *storage,
                                    ota_state_selection_t current_selection,
                                    const ota_state_record_t *next_state)
{
    _Alignas(BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES)
    uint8_t encoded[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES * 2U];
    ota_state_record_t decoded;
    uint32_t target_address;

    if (next_state == NULL || !bootloader_state_store_can_write(storage))
    {
        return false;
    }
    memset(encoded, 0xFF, sizeof(encoded));
    ota_state_record_encode(next_state, encoded);
    if (!ota_state_record_decode(encoded, &decoded))
    {
        return false;
    }

    target_address = current_selection == OTA_STATE_SELECT_PRIMARY
                         ? TRANSPORT_OTA_STATE_SECONDARY_BASE
                         : TRANSPORT_OTA_STATE_PRIMARY_BASE;
    if (!storage->internal_flash.erase(storage->internal_flash.context, target_address,
                                       BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES)
        || !storage->internal_flash.program(storage->internal_flash.context, target_address,
                                            encoded,
                                            BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES)
        || !storage->internal_flash.program(
            storage->internal_flash.context,
            target_address + BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES,
            &encoded[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES],
            BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES))
    {
        return false;
    }
    return true;
}
