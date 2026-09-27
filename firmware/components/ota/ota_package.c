#include <stdbool.h>
#include <string.h>

#include "memory_layout.h"
#include "ota_crc32.h"
#include "ota_package.h"

static uint16_t ota_package_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ota_package_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static bool ota_package_identifier_is_valid(const uint8_t *identifier)
{
    bool has_non_whitespace = false;
    bool seen_nul = false;
    uint32_t index;

    for (index = 0U; index < OTA_PACKAGE_IDENTIFIER_BYTES; index++)
    {
        const uint8_t value = identifier[index];

        if (value == 0U)
        {
            seen_nul = true;
        }
        else
        {
            if (seen_nul)
            {
                return false;
            }
            if (value != ' ' && value != '\t' && value != '\n'
                && value != '\r' && value != '\v' && value != '\f')
            {
                has_non_whitespace = true;
            }
        }
    }

    return has_non_whitespace;
}

static bool ota_package_identifier_matches(const uint8_t *identifier,
                                           const char *expected,
                                           uint32_t expected_length)
{
    uint32_t index;

    if (memcmp(identifier, expected, expected_length) != 0)
    {
        return false;
    }
    for (index = expected_length; index < OTA_PACKAGE_IDENTIFIER_BYTES; index++)
    {
        if (identifier[index] != 0U)
        {
            return false;
        }
    }
    return true;
}

static bool ota_package_reserved_is_zero(const uint8_t *reserved)
{
    uint32_t index;

    for (index = 0U; index < OTA_PACKAGE_RESERVED_BYTES; index++)
    {
        if (reserved[index] != 0U)
        {
            return false;
        }
    }
    return true;
}

bool ota_package_version_parse(const uint8_t identifier[OTA_PACKAGE_IDENTIFIER_BYTES],
                               uint32_t *packed_version)
{
    uint32_t components[3] = {0U};
    uint32_t component_index = 0U;
    uint32_t index;
    uint32_t value = 0U;
    bool saw_digit = false;

    if (identifier == NULL || packed_version == NULL
        || !ota_package_identifier_is_valid(identifier))
    {
        return false;
    }
    for (index = 0U; index < OTA_PACKAGE_IDENTIFIER_BYTES; index++)
    {
        const uint8_t character = identifier[index];

        if (character == 0U)
        {
            break;
        }
        if (character >= '0' && character <= '9')
        {
            const uint32_t digit = (uint32_t)(character - '0');

            if (saw_digit && value == 0U)
            {
                return false;
            }
            if (value > 25U || (value == 25U && digit > 5U))
            {
                return false;
            }
            value = value * 10U + digit;
            saw_digit = true;
        }
        else if (character == '.' && saw_digit && component_index < 2U)
        {
            components[component_index++] = value;
            value = 0U;
            saw_digit = false;
        }
        else
        {
            return false;
        }
    }
    if (index == OTA_PACKAGE_IDENTIFIER_BYTES || !saw_digit || component_index != 2U)
    {
        return false;
    }
    components[component_index] = value;
    *packed_version = OTA_PACKAGE_VERSION_PACK(components[0], components[1],
                                               components[2]);
    return true;
}

ota_package_status_t ota_package_validate_header(
    const uint8_t manifest_bytes[OTA_PACKAGE_MANIFEST_BYTES],
    uint32_t slot_size_bytes, ota_package_manifest_t *manifest)
{
    ota_package_manifest_t parsed = {0};
    uint32_t declared_image_length;

    if (manifest_bytes == NULL || slot_size_bytes < OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_PACKAGE_STATUS_INVALID_ARGUMENT;
    }
    if (memcmp(&manifest_bytes[OTA_PACKAGE_OFFSET_MAGIC], "TRFW", 4U) != 0)
    {
        return OTA_PACKAGE_STATUS_BAD_MAGIC;
    }
    if (ota_package_read_u16_le(&manifest_bytes[OTA_PACKAGE_OFFSET_FORMAT_VERSION]) != 1U)
    {
        return OTA_PACKAGE_STATUS_BAD_FORMAT_VERSION;
    }
    if (ota_package_read_u16_le(&manifest_bytes[OTA_PACKAGE_OFFSET_HEADER_BYTES])
        != OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_PACKAGE_STATUS_BAD_HEADER_BYTES;
    }
    if (!ota_package_identifier_is_valid(&manifest_bytes[OTA_PACKAGE_OFFSET_PRODUCT_ID])
        || !ota_package_identifier_is_valid(&manifest_bytes[OTA_PACKAGE_OFFSET_HARDWARE_ID])
        || !ota_package_identifier_is_valid(&manifest_bytes[OTA_PACKAGE_OFFSET_FIRMWARE_VERSION])
        || !ota_package_identifier_is_valid(
            &manifest_bytes[OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION]))
    {
        return OTA_PACKAGE_STATUS_BAD_IDENTITY;
    }
    if (!ota_package_identifier_matches(&manifest_bytes[OTA_PACKAGE_OFFSET_PRODUCT_ID],
                                        OTA_PACKAGE_EXPECTED_PRODUCT_ID,
                                        sizeof(OTA_PACKAGE_EXPECTED_PRODUCT_ID) - 1U))
    {
        return OTA_PACKAGE_STATUS_UNEXPECTED_PRODUCT_ID;
    }
    if (!ota_package_identifier_matches(&manifest_bytes[OTA_PACKAGE_OFFSET_HARDWARE_ID],
                                        OTA_PACKAGE_EXPECTED_HARDWARE_ID,
                                        sizeof(OTA_PACKAGE_EXPECTED_HARDWARE_ID) - 1U))
    {
        return OTA_PACKAGE_STATUS_UNEXPECTED_HARDWARE_ID;
    }
    if (ota_package_read_u32_le(&manifest_bytes[OTA_PACKAGE_OFFSET_TARGET_ADDRESS])
        != TRANSPORT_OTA_APPLICATION_BASE)
    {
        return OTA_PACKAGE_STATUS_BAD_TARGET_ADDRESS;
    }
    declared_image_length = ota_package_read_u32_le(
        &manifest_bytes[OTA_PACKAGE_OFFSET_IMAGE_LENGTH]);
    if (declared_image_length == 0U)
    {
        return OTA_PACKAGE_STATUS_EMPTY_IMAGE;
    }
    if (declared_image_length > TRANSPORT_OTA_APPLICATION_SIZE_BYTES
        || declared_image_length > TRANSPORT_OTA_PACKAGE_MAX_IMAGE_BYTES)
    {
        return OTA_PACKAGE_STATUS_BAD_IMAGE_LENGTH;
    }
    if (declared_image_length > slot_size_bytes - OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_PACKAGE_STATUS_SLOT_OVERFLOW;
    }
    if (!ota_package_reserved_is_zero(&manifest_bytes[OTA_PACKAGE_OFFSET_RESERVED]))
    {
        return OTA_PACKAGE_STATUS_BAD_RESERVED;
    }

    memcpy(parsed.product_id, &manifest_bytes[OTA_PACKAGE_OFFSET_PRODUCT_ID],
           sizeof(parsed.product_id));
    memcpy(parsed.hardware_id, &manifest_bytes[OTA_PACKAGE_OFFSET_HARDWARE_ID],
           sizeof(parsed.hardware_id));
    memcpy(parsed.firmware_version, &manifest_bytes[OTA_PACKAGE_OFFSET_FIRMWARE_VERSION],
           sizeof(parsed.firmware_version));
    memcpy(parsed.minimum_bootloader_version,
           &manifest_bytes[OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION],
           sizeof(parsed.minimum_bootloader_version));
    parsed.target_address = TRANSPORT_OTA_APPLICATION_BASE;
    parsed.image_length = declared_image_length;
    parsed.image_crc32 = ota_package_read_u32_le(
        &manifest_bytes[OTA_PACKAGE_OFFSET_IMAGE_CRC32]);
    memcpy(parsed.image_sha256, &manifest_bytes[OTA_PACKAGE_OFFSET_IMAGE_SHA256],
           sizeof(parsed.image_sha256));
    if (manifest != NULL)
    {
        *manifest = parsed;
    }
    return OTA_PACKAGE_STATUS_OK;
}

ota_package_status_t ota_package_validate(const uint8_t *package_bytes,
                                          uint32_t package_length,
                                          uint32_t slot_size_bytes,
                                          ota_package_manifest_t *manifest)
{
    ota_package_manifest_t parsed = {0};
    ota_sha256_t sha256;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_package_status_t header_status;

    if (package_bytes == NULL)
    {
        return OTA_PACKAGE_STATUS_INVALID_ARGUMENT;
    }
    if (package_length < OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_PACKAGE_STATUS_TOO_SHORT;
    }
    header_status = ota_package_validate_header(package_bytes, slot_size_bytes, &parsed);
    if (header_status != OTA_PACKAGE_STATUS_OK)
    {
        return header_status;
    }
    if (parsed.image_length != package_length - OTA_PACKAGE_MANIFEST_BYTES)
    {
        return OTA_PACKAGE_STATUS_BAD_IMAGE_LENGTH;
    }
    if (ota_crc32_compute(&package_bytes[OTA_PACKAGE_MANIFEST_BYTES], parsed.image_length)
        != parsed.image_crc32)
    {
        return OTA_PACKAGE_STATUS_BAD_CRC32;
    }
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &package_bytes[OTA_PACKAGE_MANIFEST_BYTES],
                      parsed.image_length);
    ota_sha256_final(&sha256, digest);
    if (memcmp(digest, parsed.image_sha256, sizeof(digest)) != 0)
    {
        return OTA_PACKAGE_STATUS_BAD_SHA256;
    }

    if (manifest != NULL)
    {
        *manifest = parsed;
    }
    return OTA_PACKAGE_STATUS_OK;
}
