#include <stdbool.h>
#include <string.h>

#include "bootloader_app.h"
#include "bootloader_version.h"
#include "memory_layout.h"
#include "ota_crc32.h"
#include "ota_sha256.h"

#define BOOTLOADER_AXI_SRAM_BASE UINT32_C(0x24000000)
#define BOOTLOADER_AXI_SRAM_END UINT32_C(0x24080000)
#define BOOTLOADER_PACKAGE_STREAM_CHUNK_BYTES UINT32_C(256)

typedef struct
{
    uint8_t header[OTA_PACKAGE_MANIFEST_BYTES];
    uint8_t chunk[BOOTLOADER_PACKAGE_STREAM_CHUNK_BYTES];
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_sha256_t sha256;
    ota_package_manifest_t parsed;
} bootloader_package_validation_scratch_t;

/* The Bootloader is single-threaded, so keep validation buffers out of its
 * constrained startup stack. */
static bootloader_package_validation_scratch_t bootloader_package_validation_scratch;

static uint32_t bootloader_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static bool bootloader_storage_is_readable(const bootloader_storage_ops_t *storage)
{
    return storage != NULL && storage->read != NULL;
}

static bool bootloader_range_is_within(uint32_t base, uint32_t size,
                                       uint32_t address, uint32_t length)
{
    return address >= base && length <= size && address - base <= size - length;
}

bool bootloader_application_vectors_read_and_validate(
    const bootloader_storage_t *storage, bootloader_application_vectors_t *vectors)
{
    uint8_t encoded[8];
    bootloader_application_vectors_t parsed;
    const uint32_t application_end = TRANSPORT_OTA_APPLICATION_BASE
                                     + TRANSPORT_OTA_APPLICATION_SIZE_BYTES;
    uint32_t reset_address_masked;

    if (storage == NULL || vectors == NULL
        || !bootloader_storage_is_readable(&storage->internal_flash)
        || !storage->internal_flash.read(storage->internal_flash.context,
                                         TRANSPORT_OTA_APPLICATION_BASE,
                                         encoded, sizeof(encoded)))
    {
        return false;
    }

    parsed.initial_msp = bootloader_read_u32_le(&encoded[0]);
    parsed.reset_handler = bootloader_read_u32_le(&encoded[4]);
    reset_address_masked = parsed.reset_handler & ~UINT32_C(1);
    if (parsed.initial_msp % 8U != 0U
        || parsed.initial_msp < BOOTLOADER_AXI_SRAM_BASE
        || parsed.initial_msp >= BOOTLOADER_AXI_SRAM_END
        || (parsed.reset_handler & 1U) == 0U
        || reset_address_masked < TRANSPORT_OTA_APPLICATION_BASE
        || reset_address_masked >= application_end)
    {
        return false;
    }

    *vectors = parsed;
    return true;
}

bool bootloader_package_validate_from_qspi(const bootloader_storage_t *storage,
                                           uint32_t package_offset,
                                           uint32_t slot_size_bytes,
                                           ota_package_manifest_t *manifest)
{
    bootloader_package_validation_scratch_t *const scratch =
        &bootloader_package_validation_scratch;
    uint32_t image_length;
    ota_crc32_t crc32;
    uint32_t image_crc32;
    uint32_t offset;
    uint32_t remaining;
    uint32_t minimum_bootloader_version;

    if (storage == NULL || manifest == NULL || slot_size_bytes < OTA_PACKAGE_MANIFEST_BYTES
        || !bootloader_storage_is_readable(&storage->qspi)
        || !bootloader_range_is_within(0U, TRANSPORT_OTA_QSPI_SIZE_BYTES,
                                       package_offset, OTA_PACKAGE_MANIFEST_BYTES)
        || !storage->qspi.read(storage->qspi.context, package_offset, scratch->header,
                               sizeof(scratch->header)))
    {
        return false;
    }

    memset(&scratch->parsed, 0, sizeof(scratch->parsed));
    if (ota_package_validate_header(scratch->header, slot_size_bytes, &scratch->parsed)
            != OTA_PACKAGE_STATUS_OK
        || !ota_package_version_parse(scratch->parsed.minimum_bootloader_version,
                                      &minimum_bootloader_version)
        || minimum_bootloader_version > BOOTLOADER_VERSION_CURRENT)
    {
        return false;
    }
    image_length = scratch->parsed.image_length;
    if (!bootloader_range_is_within(0U, TRANSPORT_OTA_QSPI_SIZE_BYTES,
                                   package_offset + OTA_PACKAGE_MANIFEST_BYTES,
                                   image_length))
    {
        return false;
    }

    ota_sha256_init(&scratch->sha256);
    ota_crc32_init(&crc32);
    offset = 0U;
    remaining = image_length;
    while (remaining > 0U)
    {
        const uint32_t bytes_this_time = remaining > sizeof(scratch->chunk)
                                            ? sizeof(scratch->chunk)
                                            : remaining;

        if (!storage->qspi.read(storage->qspi.context,
                                package_offset + OTA_PACKAGE_MANIFEST_BYTES + offset,
                                scratch->chunk, bytes_this_time))
        {
            return false;
        }
        ota_crc32_update(&crc32, scratch->chunk, bytes_this_time);
        ota_sha256_update(&scratch->sha256, scratch->chunk, bytes_this_time);
        offset += bytes_this_time;
        remaining -= bytes_this_time;
    }
    image_crc32 = ota_crc32_final(&crc32);
    ota_sha256_final(&scratch->sha256, scratch->digest);
    if (image_crc32 != scratch->parsed.image_crc32
        || memcmp(scratch->digest, scratch->parsed.image_sha256, sizeof(scratch->digest)) != 0)
    {
        return false;
    }

    *manifest = scratch->parsed;
    return true;
}
