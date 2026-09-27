#ifndef TRANSPORT_RECORDER_OTA_DOWNLOAD_SERVICE_H
#define TRANSPORT_RECORDER_OTA_DOWNLOAD_SERVICE_H

#include <stdint.h>

typedef int (*ota_download_erase_fn)(void *context, uint32_t offset, uint32_t length);
typedef int (*ota_download_write_fn)(void *context, uint32_t offset,
                                     const uint8_t *data, uint32_t length);
typedef int (*ota_download_read_fn)(void *context, uint32_t offset,
                                    uint8_t *data, uint32_t length);
typedef int (*ota_download_mark_pending_install_fn)(void *context);
typedef int (*ota_download_validate_fn)(void *context);

typedef struct
{
    ota_download_erase_fn erase;
    ota_download_write_fn write;
    ota_download_read_fn read;
    ota_download_validate_fn validate;
    ota_download_mark_pending_install_fn mark_pending_install;
    void *context;
    uint32_t capacity_bytes;
    /* Zero keeps legacy full-capacity erase behavior. */
    uint32_t erase_block_bytes;
} ota_download_storage_t;

typedef enum
{
    OTA_DOWNLOAD_STATUS_OK = 0,
    OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT,
    OTA_DOWNLOAD_STATUS_NOT_STARTED,
    OTA_DOWNLOAD_STATUS_OUT_OF_ORDER,
    OTA_DOWNLOAD_STATUS_REPLAY_MISMATCH,
    OTA_DOWNLOAD_STATUS_STORAGE_ERROR,
    OTA_DOWNLOAD_STATUS_INCOMPLETE,
    OTA_DOWNLOAD_STATUS_INVALID_PACKAGE,
} ota_download_status_t;

typedef struct
{
    uint32_t total_bytes;
    uint32_t verified_bytes;
    uint8_t pending_install;
} ota_download_progress_t;

typedef struct
{
    ota_download_storage_t storage;
    uint32_t total_bytes;
    uint32_t verified_bytes;
    uint8_t started;
    uint8_t pending_install;
} ota_download_service_t;

void ota_download_service_init(ota_download_service_t *service,
                               const ota_download_storage_t *storage);
ota_download_status_t ota_download_begin(ota_download_service_t *service,
                                         uint32_t total_bytes);
ota_download_status_t ota_download_write(ota_download_service_t *service,
                                         uint32_t offset, const uint8_t *data,
                                         uint32_t length, uint32_t crc32);
void ota_download_query(const ota_download_service_t *service,
                        ota_download_progress_t *progress);
ota_download_status_t ota_download_finalize(ota_download_service_t *service);
ota_download_status_t ota_download_cancel(ota_download_service_t *service);

#endif
