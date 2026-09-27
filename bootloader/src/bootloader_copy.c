#include <stdbool.h>
#include <string.h>

#include "bootloader_copy.h"
#include "memory_layout.h"
#include "ota_crc32.h"

static bool bootloader_storage_is_copyable(const bootloader_storage_ops_t *storage)
{
    return storage != NULL && storage->read != NULL && storage->erase != NULL
           && storage->program != NULL;
}

static bool bootloader_range_is_within(uint32_t base, uint32_t size,
                                       uint32_t address, uint32_t length)
{
    return address >= base && length <= size && address - base <= size - length;
}

static uint32_t bootloader_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

static bool bootloader_copy_request_is_valid(const bootloader_storage_t *storage,
                                             const bootloader_copy_request_t *request,
                                             const ota_state_record_t *state,
                                             uint32_t scratch_size)
{
    if (storage == NULL || request == NULL || state == NULL
        || !bootloader_storage_is_copyable(&storage->internal_flash)
        || !bootloader_storage_is_copyable(&storage->qspi)
        || request->image_length == 0U
        || request->image_length > TRANSPORT_OTA_APPLICATION_SIZE_BYTES
        || request->maximum_copy_bytes == 0U
        || request->maximum_copy_bytes % BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES != 0U
        || scratch_size < BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES
        || state->state != OTA_STATE_INSTALLING
        || state->copy_offset > request->image_length
        || state->copy_offset % BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES != 0U
        || !bootloader_range_is_within(0U, TRANSPORT_OTA_QSPI_SIZE_BYTES,
                                       request->qspi_image_offset,
                                       request->image_length))
    {
        return false;
    }
    return true;
}

static bool bootloader_erase_target(const bootloader_storage_t *storage,
                                    uint32_t image_length)
{
    uint32_t address = TRANSPORT_OTA_APPLICATION_BASE;
    uint32_t remaining = bootloader_align_up(image_length,
                                             BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES);

    if (remaining > TRANSPORT_OTA_APPLICATION_SIZE_BYTES)
    {
        return false;
    }
    while (remaining > 0U)
    {
        if (!storage->internal_flash.erase(storage->internal_flash.context, address,
                                            BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES))
        {
            return false;
        }
        address += BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES;
        remaining -= BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES;
    }
    return true;
}

static bool bootloader_verify_copied_image(const bootloader_storage_t *storage,
                                           const bootloader_copy_request_t *request,
                                           uint8_t *scratch)
{
    ota_crc32_t crc32;
    ota_sha256_t sha256;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    uint32_t offset = 0U;

    ota_crc32_init(&crc32);
    ota_sha256_init(&sha256);
    while (offset < request->image_length)
    {
        const uint32_t remaining = request->image_length - offset;
        const uint32_t bytes_this_time = remaining < BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES
                                             ? remaining
                                             : BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES;

        if (!storage->internal_flash.read(
                storage->internal_flash.context,
                TRANSPORT_OTA_APPLICATION_BASE + offset,
                scratch, bytes_this_time))
        {
            return false;
        }
        ota_crc32_update(&crc32, scratch, bytes_this_time);
        ota_sha256_update(&sha256, scratch, bytes_this_time);
        offset += bytes_this_time;
    }
    ota_sha256_final(&sha256, digest);
    return ota_crc32_final(&crc32) == request->expected_image_crc32
           && memcmp(digest, request->expected_image_sha256, sizeof(digest)) == 0;
}

bool bootloader_copy_engine_run(const bootloader_storage_t *storage,
                                const bootloader_copy_request_t *request,
                                const ota_state_record_t *installing_state,
                                uint8_t *scratch, uint32_t scratch_size,
                                bootloader_copy_result_t *result)
{
    ota_state_record_t next_state;
    uint32_t offset;
    uint32_t budget;
    uint8_t readback[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES];

    if (scratch == NULL || result == NULL
        || !bootloader_copy_request_is_valid(storage, request, installing_state,
                                             scratch_size))
    {
        return false;
    }
    result->complete = false;
    result->next_state = *installing_state;
    if (installing_state->copy_offset == 0U
        && !bootloader_erase_target(storage, request->image_length))
    {
        return false;
    }

    offset = installing_state->copy_offset;
    budget = request->maximum_copy_bytes;
    while (offset < request->image_length && budget > 0U)
    {
        const uint32_t remaining = request->image_length - offset;
        const uint32_t bytes_to_read = remaining < BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES
                                           ? remaining
                                           : BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES;

        memset(scratch, 0xFF, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES);
        if (!storage->qspi.read(storage->qspi.context,
                                request->qspi_image_offset + offset,
                                scratch, bytes_to_read)
            || !storage->internal_flash.program(
                storage->internal_flash.context, TRANSPORT_OTA_APPLICATION_BASE + offset,
                scratch, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES)
            || !storage->internal_flash.read(
                storage->internal_flash.context, TRANSPORT_OTA_APPLICATION_BASE + offset,
                readback, sizeof(readback))
            || memcmp(scratch, readback, sizeof(readback)) != 0)
        {
            return false;
        }
        offset += bytes_to_read;
        budget -= BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES;
    }

    if (offset == request->image_length)
    {
        if (!bootloader_verify_copied_image(storage, request, scratch)
            || ota_state_transition(installing_state, OTA_STATE_TRIAL, &next_state)
            != OTA_STATE_TRANSITION_OK)
        {
            return false;
        }
        result->complete = true;
    }
    else
    {
        if (installing_state->generation == UINT32_MAX)
        {
            return false;
        }
        next_state = *installing_state;
        next_state.generation++;
        next_state.copy_offset = offset;
        result->complete = false;
    }
    result->next_state = next_state;
    return true;
}
