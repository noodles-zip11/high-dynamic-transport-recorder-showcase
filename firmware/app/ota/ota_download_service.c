#include "ota_download_service.h"

#include <string.h>

#include "ota_crc32.h"
#include "ota_package.h"
#include "ota_sha256.h"

static int ota_download_storage_is_valid(const ota_download_storage_t *storage)
{
    return storage != NULL && storage->erase != NULL && storage->write != NULL
           && storage->read != NULL && storage->capacity_bytes != 0U
           && (storage->erase_block_bytes == 0U
               || storage->erase_block_bytes <= storage->capacity_bytes);
}

static uint32_t ota_download_erase_length(const ota_download_service_t *service,
                                          uint32_t content_length)
{
    uint32_t block;
    uint32_t rounded;

    if (service == NULL || service->storage.erase_block_bytes == 0U)
    {
        return service == NULL ? 0U : service->storage.capacity_bytes;
    }
    block = service->storage.erase_block_bytes;
    if (content_length == 0U || content_length > service->storage.capacity_bytes
        || content_length > UINT32_MAX - (block - 1U))
    {
        return 0U;
    }
    rounded = (content_length + block - 1U) / block;
    if (rounded > UINT32_MAX / block)
    {
        return 0U;
    }
    rounded *= block;
    return rounded <= service->storage.capacity_bytes ? rounded : 0U;
}

void ota_download_service_init(ota_download_service_t *service,
                               const ota_download_storage_t *storage)
{
    if (service == NULL)
    {
        return;
    }
    memset(service, 0, sizeof(*service));
    if (ota_download_storage_is_valid(storage))
    {
        service->storage = *storage;
    }
}

ota_download_status_t ota_download_begin(ota_download_service_t *service,
                                         uint32_t total_bytes)
{
    uint32_t erase_length;

    if (service == NULL || !ota_download_storage_is_valid(&service->storage)
        || total_bytes == 0U || total_bytes > service->storage.capacity_bytes)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    erase_length = ota_download_erase_length(service, total_bytes);
    if (erase_length == 0U)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (service->storage.erase(service->storage.context, 0U,
                               erase_length) != 0)
    {
        return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
    }
    service->total_bytes = total_bytes;
    service->verified_bytes = 0U;
    service->started = 1U;
    service->pending_install = 0U;
    return OTA_DOWNLOAD_STATUS_OK;
}

ota_download_status_t ota_download_write(ota_download_service_t *service,
                                         uint32_t offset, const uint8_t *data,
                                         uint32_t length, uint32_t crc32)
{
    uint8_t readback[256];

    if (service == NULL || data == NULL || length == 0U
        || length > sizeof(readback) || !service->started)
    {
        return service != NULL && !service->started ? OTA_DOWNLOAD_STATUS_NOT_STARTED
                                                     : OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (length > service->total_bytes || offset > service->total_bytes - length
        || ota_crc32_compute(data, length) != crc32)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (offset == service->verified_bytes)
    {
        if (service->storage.write(service->storage.context, offset, data, length) != 0)
        {
            return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
        }
        service->verified_bytes += length;
        return OTA_DOWNLOAD_STATUS_OK;
    }
    if (offset < service->verified_bytes && length <= service->verified_bytes - offset
        && service->storage.read(service->storage.context, offset, readback, length) == 0
        && memcmp(readback, data, length) == 0)
    {
        return OTA_DOWNLOAD_STATUS_OK;
    }
    return offset < service->verified_bytes ? OTA_DOWNLOAD_STATUS_REPLAY_MISMATCH
                                            : OTA_DOWNLOAD_STATUS_OUT_OF_ORDER;
}

void ota_download_query(const ota_download_service_t *service,
                        ota_download_progress_t *progress)
{
    if (progress == NULL)
    {
        return;
    }
    memset(progress, 0, sizeof(*progress));
    if (service != NULL)
    {
        progress->total_bytes = service->total_bytes;
        progress->verified_bytes = service->verified_bytes;
        progress->pending_install = service->pending_install;
    }
}

ota_download_status_t ota_download_finalize(ota_download_service_t *service)
{
    uint8_t manifest_bytes[OTA_PACKAGE_MANIFEST_BYTES];
    uint8_t read_buffer[256];
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_package_manifest_t manifest;
    ota_crc32_t crc32;
    ota_sha256_t sha256;
    uint32_t offset;

    if (service == NULL || !service->started)
    {
        return OTA_DOWNLOAD_STATUS_NOT_STARTED;
    }
    if (service->pending_install)
    {
        return OTA_DOWNLOAD_STATUS_OK;
    }
    if (service->verified_bytes != service->total_bytes)
    {
        return OTA_DOWNLOAD_STATUS_INCOMPLETE;
    }
    if (service->storage.validate != NULL)
    {
        if (service->storage.validate(service->storage.context) != 0)
        {
            return OTA_DOWNLOAD_STATUS_INVALID_PACKAGE;
        }
        if (service->storage.mark_pending_install != NULL
            && service->storage.mark_pending_install(service->storage.context) != 0)
        {
            return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
        }
        service->pending_install =
            service->storage.mark_pending_install != NULL ? 1U : 0U;
        return OTA_DOWNLOAD_STATUS_OK;
    }
    if (service->storage.read(service->storage.context, 0U, manifest_bytes,
                              sizeof(manifest_bytes)) != 0
        || ota_package_validate_header(manifest_bytes, service->storage.capacity_bytes,
                                       &manifest) != OTA_PACKAGE_STATUS_OK
        || manifest.image_length != service->total_bytes - OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_PACKAGE;
    }
    ota_crc32_init(&crc32);
    ota_sha256_init(&sha256);
    for (offset = 0U; offset < manifest.image_length;)
    {
        const uint32_t remaining = manifest.image_length - offset;
        const uint32_t length = remaining < sizeof(read_buffer) ? remaining
                                                                 : sizeof(read_buffer);

        if (service->storage.read(service->storage.context,
                                  OTA_PACKAGE_MANIFEST_BYTES + offset,
                                  read_buffer, length) != 0)
        {
            return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
        }
        ota_crc32_update(&crc32, read_buffer, length);
        ota_sha256_update(&sha256, read_buffer, length);
        offset += length;
    }
    ota_sha256_final(&sha256, digest);
    if (ota_crc32_final(&crc32) != manifest.image_crc32
        || memcmp(digest, manifest.image_sha256, sizeof(digest)) != 0)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_PACKAGE;
    }
    if (service->storage.mark_pending_install != NULL
        && service->storage.mark_pending_install(service->storage.context) != 0)
    {
        return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
    }
    service->pending_install = service->storage.mark_pending_install != NULL ? 1U : 0U;
    return OTA_DOWNLOAD_STATUS_OK;
}

ota_download_status_t ota_download_cancel(ota_download_service_t *service)
{
    uint32_t erase_length;

    if (service == NULL || !ota_download_storage_is_valid(&service->storage))
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    erase_length = service->started
                       ? ota_download_erase_length(service, service->total_bytes)
                       : service->storage.capacity_bytes;
    if (erase_length == 0U)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (service->storage.erase(service->storage.context, 0U,
                               erase_length) != 0)
    {
        return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
    }
    service->total_bytes = 0U;
    service->verified_bytes = 0U;
    service->started = 0U;
    service->pending_install = 0U;
    return OTA_DOWNLOAD_STATUS_OK;
}
