#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_crc32.h"
#include "ota/ota_download_service.h"
#include "memory_layout.h"
#include "ota_package.h"
#include "ota_sha256.h"

typedef struct
{
    uint8_t bytes[1024];
    uint32_t erase_count;
    uint32_t pending_count;
} fake_download_storage_t;

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t build_valid_package(uint8_t *package, const uint8_t *image,
                                    uint32_t image_length)
{
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_sha256_t sha256;

    memset(package, 0, OTA_PACKAGE_MANIFEST_BYTES + image_length);
    memcpy(&package[OTA_PACKAGE_OFFSET_MAGIC], "TRFW", 4U);
    put_u16_le(&package[OTA_PACKAGE_OFFSET_FORMAT_VERSION], 1U);
    put_u16_le(&package[OTA_PACKAGE_OFFSET_HEADER_BYTES], OTA_PACKAGE_MANIFEST_BYTES);
    memcpy(&package[OTA_PACKAGE_OFFSET_PRODUCT_ID], "transport-recorder", 18U);
    memcpy(&package[OTA_PACKAGE_OFFSET_HARDWARE_ID], "stm32h743vit6", 14U);
    memcpy(&package[OTA_PACKAGE_OFFSET_FIRMWARE_VERSION], "1.2.3", 5U);
    memcpy(&package[OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION], "1.0.0", 5U);
    put_u32_le(&package[OTA_PACKAGE_OFFSET_TARGET_ADDRESS],
               TRANSPORT_OTA_APPLICATION_BASE);
    put_u32_le(&package[OTA_PACKAGE_OFFSET_IMAGE_LENGTH], image_length);
    put_u32_le(&package[OTA_PACKAGE_OFFSET_IMAGE_CRC32],
               ota_crc32_compute(image, image_length));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, image, image_length);
    ota_sha256_final(&sha256, digest);
    memcpy(&package[OTA_PACKAGE_OFFSET_IMAGE_SHA256], digest, sizeof(digest));
    memcpy(&package[OTA_PACKAGE_MANIFEST_BYTES], image, image_length);
    return OTA_PACKAGE_MANIFEST_BYTES + image_length;
}

static int fake_erase(void *context, uint32_t offset, uint32_t length)
{
    fake_download_storage_t *storage = context;

    if (storage == NULL || offset != 0U || length != sizeof(storage->bytes))
    {
        return -1;
    }
    memset(storage->bytes, 0xFF, sizeof(storage->bytes));
    storage->erase_count++;
    return 0;
}

static int fake_write(void *context, uint32_t offset, const uint8_t *data, uint32_t length)
{
    fake_download_storage_t *storage = context;

    if (storage == NULL || data == NULL || offset > sizeof(storage->bytes)
        || length > sizeof(storage->bytes) - offset)
    {
        return -1;
    }
    memcpy(&storage->bytes[offset], data, length);
    return 0;
}

static int fake_read(void *context, uint32_t offset, uint8_t *data, uint32_t length)
{
    fake_download_storage_t *storage = context;

    if (storage == NULL || data == NULL || offset > sizeof(storage->bytes)
        || length > sizeof(storage->bytes) - offset)
    {
        return -1;
    }
    memcpy(data, &storage->bytes[offset], length);
    return 0;
}

static int fake_mark_pending(void *context)
{
    fake_download_storage_t *storage = context;

    if (storage == NULL)
    {
        return -1;
    }
    storage->pending_count++;
    return 0;
}

static void test_download_replay_is_idempotent_and_out_of_order_is_rejected(void)
{
    static const uint8_t chunk[] = {0x10U, 0x20U, 0x30U, 0x40U};
    static const uint8_t different_chunk[] = {0x10U, 0x20U, 0x30U, 0x41U};
    fake_download_storage_t storage = {0};
    ota_download_storage_t storage_ops = {
        .erase = fake_erase,
        .write = fake_write,
        .read = fake_read,
        .mark_pending_install = fake_mark_pending,
        .context = &storage,
        .capacity_bytes = sizeof(storage.bytes),
    };
    ota_download_service_t service;
    ota_download_progress_t progress = {0};
    const uint32_t chunk_crc32 = ota_crc32_compute(chunk, sizeof(chunk));

    ota_download_service_init(&service, &storage_ops);
    assert(ota_download_begin(&service, 16U) == OTA_DOWNLOAD_STATUS_OK);
    assert(storage.erase_count == 1U);
    assert(ota_download_write(&service, 0U, chunk, sizeof(chunk), chunk_crc32)
           == OTA_DOWNLOAD_STATUS_OK);
    ota_download_query(&service, &progress);
    assert(progress.total_bytes == 16U);
    assert(progress.verified_bytes == sizeof(chunk));
    assert(progress.pending_install == 0U);

    assert(ota_download_write(&service, 0U, chunk, sizeof(chunk), chunk_crc32)
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_write(&service, 8U, chunk, sizeof(chunk), chunk_crc32)
           == OTA_DOWNLOAD_STATUS_OUT_OF_ORDER);
    assert(ota_download_write(&service, 0U, different_chunk, sizeof(different_chunk),
                              ota_crc32_compute(different_chunk, sizeof(different_chunk)))
           == OTA_DOWNLOAD_STATUS_REPLAY_MISMATCH);
    assert(ota_download_finalize(&service) == OTA_DOWNLOAD_STATUS_INCOMPLETE);
    assert(storage.pending_count == 0U);
}

static void test_complete_valid_package_sets_pending_install_once(void)
{
    static const uint8_t image[] = {0xA1U, 0xB2U, 0xC3U, 0xD4U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    fake_download_storage_t storage = {0};
    ota_download_storage_t storage_ops = {
        .erase = fake_erase,
        .write = fake_write,
        .read = fake_read,
        .mark_pending_install = fake_mark_pending,
        .context = &storage,
        .capacity_bytes = sizeof(storage.bytes),
    };
    ota_download_service_t service;
    const uint32_t package_length = build_valid_package(package, image, sizeof(image));

    ota_download_service_init(&service, &storage_ops);
    assert(ota_download_begin(&service, package_length) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_write(&service, 0U, package, package_length,
                              ota_crc32_compute(package, package_length))
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_finalize(&service) == OTA_DOWNLOAD_STATUS_OK);
    assert(storage.pending_count == 1U);
    assert(ota_download_finalize(&service) == OTA_DOWNLOAD_STATUS_OK);
    assert(storage.pending_count == 1U);
}

static void test_complete_valid_recovery_package_does_not_request_install(void)
{
    static const uint8_t image[] = {0xD1U, 0xE2U, 0xF3U, 0x04U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    fake_download_storage_t storage = {0};
    ota_download_storage_t storage_ops = {
        .erase = fake_erase,
        .write = fake_write,
        .read = fake_read,
        .mark_pending_install = NULL,
        .context = &storage,
        .capacity_bytes = sizeof(storage.bytes),
    };
    ota_download_service_t service;
    ota_download_progress_t progress = {0};
    const uint32_t package_length = build_valid_package(package, image, sizeof(image));

    ota_download_service_init(&service, &storage_ops);
    assert(ota_download_begin(&service, package_length) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_write(&service, 0U, package, package_length,
                              ota_crc32_compute(package, package_length))
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_finalize(&service) == OTA_DOWNLOAD_STATUS_OK);
    ota_download_query(&service, &progress);
    assert(progress.pending_install == 0U);
    assert(storage.pending_count == 0U);
}

static void test_cancel_erases_candidate_and_discards_progress(void)
{
    static const uint8_t chunk[] = {0x10U, 0x20U, 0x30U, 0x40U};
    fake_download_storage_t storage = {0};
    ota_download_storage_t storage_ops = {
        .erase = fake_erase,
        .write = fake_write,
        .read = fake_read,
        .mark_pending_install = fake_mark_pending,
        .context = &storage,
        .capacity_bytes = sizeof(storage.bytes),
    };
    ota_download_service_t service;
    ota_download_progress_t progress;

    ota_download_service_init(&service, &storage_ops);
    assert(ota_download_begin(&service, 16U) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_write(&service, 0U, chunk, sizeof(chunk),
                              ota_crc32_compute(chunk, sizeof(chunk)))
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_download_cancel(&service) == OTA_DOWNLOAD_STATUS_OK);
    ota_download_query(&service, &progress);
    assert(storage.erase_count == 2U);
    assert(progress.total_bytes == 0U);
    assert(progress.verified_bytes == 0U);
    assert(progress.pending_install == 0U);
    assert(ota_download_finalize(&service) == OTA_DOWNLOAD_STATUS_NOT_STARTED);
}

int main(void)
{
    test_download_replay_is_idempotent_and_out_of_order_is_rejected();
    test_complete_valid_package_sets_pending_install_once();
    test_complete_valid_recovery_package_does_not_request_install();
    test_cancel_erases_candidate_and_discards_progress();
    puts("ota download service: PASS");
    return 0;
}
