#ifndef TRANSPORT_RECORDER_OTA_PACKAGE_H
#define TRANSPORT_RECORDER_OTA_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_sha256.h"

#define OTA_PACKAGE_MANIFEST_BYTES 196U
#define OTA_PACKAGE_IDENTIFIER_BYTES 32U
#define OTA_PACKAGE_RESERVED_BYTES 16U

#define OTA_PACKAGE_EXPECTED_PRODUCT_ID "transport-recorder"
#define OTA_PACKAGE_EXPECTED_HARDWARE_ID "stm32h743vit6"

#define OTA_PACKAGE_OFFSET_MAGIC 0U
#define OTA_PACKAGE_OFFSET_FORMAT_VERSION 4U
#define OTA_PACKAGE_OFFSET_HEADER_BYTES 6U
#define OTA_PACKAGE_OFFSET_PRODUCT_ID 8U
#define OTA_PACKAGE_OFFSET_HARDWARE_ID 40U
#define OTA_PACKAGE_OFFSET_FIRMWARE_VERSION 72U
#define OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION 104U
#define OTA_PACKAGE_OFFSET_TARGET_ADDRESS 136U
#define OTA_PACKAGE_OFFSET_IMAGE_LENGTH 140U
#define OTA_PACKAGE_OFFSET_IMAGE_CRC32 144U
#define OTA_PACKAGE_OFFSET_IMAGE_SHA256 148U
#define OTA_PACKAGE_OFFSET_RESERVED 180U

#define OTA_PACKAGE_VERSION_PACK(major, minor, patch) \
    ((((uint32_t)(major) & UINT32_C(0xFF)) << 16U) \
     | (((uint32_t)(minor) & UINT32_C(0xFF)) << 8U) \
     | ((uint32_t)(patch) & UINT32_C(0xFF)))

typedef enum
{
    OTA_PACKAGE_STATUS_OK = 0,
    OTA_PACKAGE_STATUS_INVALID_ARGUMENT,
    OTA_PACKAGE_STATUS_TOO_SHORT,
    OTA_PACKAGE_STATUS_BAD_MAGIC,
    OTA_PACKAGE_STATUS_BAD_FORMAT_VERSION,
    OTA_PACKAGE_STATUS_BAD_HEADER_BYTES,
    OTA_PACKAGE_STATUS_BAD_IDENTITY,
    OTA_PACKAGE_STATUS_UNEXPECTED_PRODUCT_ID,
    OTA_PACKAGE_STATUS_UNEXPECTED_HARDWARE_ID,
    OTA_PACKAGE_STATUS_BAD_TARGET_ADDRESS,
    OTA_PACKAGE_STATUS_BAD_IMAGE_LENGTH,
    OTA_PACKAGE_STATUS_EMPTY_IMAGE,
    OTA_PACKAGE_STATUS_SLOT_OVERFLOW,
    OTA_PACKAGE_STATUS_BAD_CRC32,
    OTA_PACKAGE_STATUS_BAD_SHA256,
    OTA_PACKAGE_STATUS_BAD_RESERVED,
} ota_package_status_t;

typedef struct
{
    uint8_t product_id[OTA_PACKAGE_IDENTIFIER_BYTES];
    uint8_t hardware_id[OTA_PACKAGE_IDENTIFIER_BYTES];
    uint8_t firmware_version[OTA_PACKAGE_IDENTIFIER_BYTES];
    uint8_t minimum_bootloader_version[OTA_PACKAGE_IDENTIFIER_BYTES];
    uint32_t target_address;
    uint32_t image_length;
    uint32_t image_crc32;
    uint8_t image_sha256[OTA_SHA256_DIGEST_BYTES];
} ota_package_manifest_t;

ota_package_status_t ota_package_validate_header(
    const uint8_t manifest_bytes[OTA_PACKAGE_MANIFEST_BYTES],
    uint32_t slot_size_bytes, ota_package_manifest_t *manifest);
bool ota_package_version_parse(const uint8_t identifier[OTA_PACKAGE_IDENTIFIER_BYTES],
                               uint32_t *packed_version);

ota_package_status_t ota_package_validate(const uint8_t *package_bytes,
                                          uint32_t package_length,
                                          uint32_t slot_size_bytes,
                                          ota_package_manifest_t *manifest);

#endif
