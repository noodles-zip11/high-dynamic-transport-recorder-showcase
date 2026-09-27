#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "ota_crc32.h"
#include "ota_package.h"
#include "ota_sha256.h"

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

static void put_identifier(uint8_t *data, const char *value)
{
    const size_t length = strlen(value);

    assert(length <= OTA_PACKAGE_IDENTIFIER_BYTES);
    memcpy(data, value, length);
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
    put_identifier(&package[OTA_PACKAGE_OFFSET_PRODUCT_ID], "transport-recorder");
    put_identifier(&package[OTA_PACKAGE_OFFSET_HARDWARE_ID], "stm32h743vit6");
    put_identifier(&package[OTA_PACKAGE_OFFSET_FIRMWARE_VERSION], "1.2.3");
    put_identifier(&package[OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION], "1.0.0");
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

static void test_crc32_and_streaming_sha256_known_vectors(void)
{
    static const uint8_t sha256_abc[OTA_SHA256_DIGEST_BYTES] = {
        0xBAU, 0x78U, 0x16U, 0xBFU, 0x8FU, 0x01U, 0xCFU, 0xEAU,
        0x41U, 0x41U, 0x40U, 0xDEU, 0x5DU, 0xAEU, 0x22U, 0x23U,
        0xB0U, 0x03U, 0x61U, 0xA3U, 0x96U, 0x17U, 0x7AU, 0x9CU,
        0xB4U, 0x10U, 0xFFU, 0x61U, 0xF2U, 0x00U, 0x15U, 0xADU,
    };
    ota_sha256_t sha256;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];

    assert(ota_crc32_compute((const uint8_t *)"123456789", 9U)
           == UINT32_C(0xCBF43926));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, (const uint8_t *)"a", 1U);
    ota_sha256_update(&sha256, (const uint8_t *)"b", 1U);
    ota_sha256_update(&sha256, (const uint8_t *)"c", 1U);
    ota_sha256_final(&sha256, digest);
    assert(memcmp(digest, sha256_abc, sizeof(digest)) == 0);
}

static void test_sha256_handles_padding_boundaries_with_independent_vectors(void)
{
    static const uint8_t expected_digests[][OTA_SHA256_DIGEST_BYTES] = {
        {0x46U, 0x3EU, 0xB2U, 0x8EU, 0x72U, 0xF8U, 0x2EU, 0x0AU,
         0x96U, 0xC0U, 0xA4U, 0xCCU, 0x53U, 0x69U, 0x0CU, 0x57U,
         0x12U, 0x81U, 0x13U, 0x1FU, 0x67U, 0x2AU, 0xA2U, 0x29U,
         0xE0U, 0xD4U, 0x5AU, 0xE5U, 0x9BU, 0x59U, 0x8BU, 0x59U},
        {0xDAU, 0x2AU, 0xE4U, 0xD6U, 0xB3U, 0x67U, 0x48U, 0xF2U,
         0xA3U, 0x18U, 0xF2U, 0x3EU, 0x7AU, 0xB1U, 0xDFU, 0xDFU,
         0x45U, 0xACU, 0xDCU, 0x9DU, 0x04U, 0x9BU, 0xD8U, 0x0EU,
         0x59U, 0xDEU, 0x82U, 0xA6U, 0x08U, 0x95U, 0xF5U, 0x62U},
        {0x29U, 0xAFU, 0x26U, 0x86U, 0xFDU, 0x53U, 0x37U, 0x4AU,
         0x36U, 0xB0U, 0x84U, 0x66U, 0x94U, 0xCCU, 0x34U, 0x21U,
         0x77U, 0xE4U, 0x28U, 0xD1U, 0x64U, 0x75U, 0x15U, 0xF0U,
         0x78U, 0x78U, 0x4DU, 0x69U, 0xCDU, 0xB9U, 0xE4U, 0x88U},
        {0xFDU, 0xEAU, 0xB9U, 0xACU, 0xF3U, 0x71U, 0x03U, 0x62U,
         0xBDU, 0x26U, 0x58U, 0xCDU, 0xC9U, 0xA2U, 0x9EU, 0x8FU,
         0x9CU, 0x75U, 0x7FU, 0xCFU, 0x98U, 0x11U, 0x60U, 0x3AU,
         0x8CU, 0x44U, 0x7CU, 0xD1U, 0xD9U, 0x15U, 0x11U, 0x08U},
        {0x4BU, 0xFDU, 0x2CU, 0x8BU, 0x6FU, 0x1EU, 0xECU, 0x7AU,
         0x2AU, 0xFEU, 0xB4U, 0x8BU, 0x93U, 0x4EU, 0xE4U, 0xB2U,
         0x69U, 0x41U, 0x82U, 0x02U, 0x7EU, 0x6DU, 0x0FU, 0xC0U,
         0x75U, 0x07U, 0x4FU, 0x2FU, 0xABU, 0xB3U, 0x17U, 0x81U},
    };
    static const uint32_t lengths[] = {55U, 56U, 63U, 64U, 65U};
    uint8_t input[65];
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    uint32_t index;

    for (index = 0U; index < sizeof(input); index++)
    {
        input[index] = (uint8_t)index;
    }
    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); index++)
    {
        ota_sha256_t sha256;

        ota_sha256_init(&sha256);
        ota_sha256_update(&sha256, input, lengths[index]);
        ota_sha256_final(&sha256, digest);
        assert(memcmp(digest, expected_digests[index], sizeof(digest)) == 0);
    }
}

static void test_sha256_streams_multiple_blocks_with_an_independent_vector(void)
{
    static const uint8_t expected_digest[OTA_SHA256_DIGEST_BYTES] = {
        0x8BU, 0x4AU, 0x54U, 0x48U, 0x37U, 0xA1U, 0xA0U, 0x28U,
        0x0FU, 0xA8U, 0xA7U, 0xC8U, 0x28U, 0x65U, 0xC2U, 0x7AU,
        0x10U, 0x64U, 0xB3U, 0xCCU, 0x62U, 0x81U, 0xFDU, 0xA0U,
        0x75U, 0x35U, 0x66U, 0xB9U, 0xBBU, 0x10U, 0x4AU, 0x87U,
    };
    uint8_t input[192];
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_sha256_t sha256;
    uint32_t index;

    for (index = 0U; index < sizeof(input); index++)
    {
        input[index] = (uint8_t)index;
    }
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &input[0], 1U);
    ota_sha256_update(&sha256, &input[1], 63U);
    ota_sha256_update(&sha256, &input[64], 64U);
    ota_sha256_update(&sha256, &input[128], 64U);
    ota_sha256_final(&sha256, digest);
    assert(memcmp(digest, expected_digest, sizeof(digest)) == 0);
}

static void test_exact_python_manifest_is_decoded_and_verified(void)
{
    static const uint8_t image[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    ota_package_manifest_t manifest = {0};
    uint32_t package_length;

    package_length = build_valid_package(package, image, sizeof(image));
    assert(package_length == 196U + sizeof(image));
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                &manifest) == OTA_PACKAGE_STATUS_OK);
    assert(manifest.target_address == TRANSPORT_OTA_APPLICATION_BASE);
    assert(manifest.image_length == sizeof(image));
    assert(memcmp(manifest.product_id, "transport-recorder", 18U) == 0);
    assert(memcmp(manifest.hardware_id, "stm32h743vit6", 14U) == 0);
}

static void test_shared_header_validation_rejects_empty_images(void)
{
    static const uint8_t image[] = {0x21U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    ota_package_manifest_t manifest = {0};
    uint32_t version = 0U;

    build_valid_package(package, image, sizeof(image));
    assert(ota_package_validate_header(package,
                                       TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                       &manifest) == OTA_PACKAGE_STATUS_OK);
    assert(manifest.image_length == sizeof(image));
    assert(ota_package_version_parse(manifest.minimum_bootloader_version, &version));
    assert(version == UINT32_C(0x00010000));

    put_u32_le(&package[OTA_PACKAGE_OFFSET_IMAGE_LENGTH], 0U);
    assert(ota_package_validate_header(package,
                                       TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                       NULL) == OTA_PACKAGE_STATUS_EMPTY_IMAGE);
}

static void test_version_parser_rejects_leading_zero_components(void)
{
    uint8_t version[OTA_PACKAGE_IDENTIFIER_BYTES] = {0};
    uint32_t packed_version = 0U;

    memcpy(version, "01.2.3", 6U);
    assert(!ota_package_version_parse(version, &packed_version));
}

static void test_manifest_rejects_invalid_fixed_fields_and_identity(void)
{
    static const uint8_t image[] = {0xAAU};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    uint32_t package_length = build_valid_package(package, image, sizeof(image));

    package[OTA_PACKAGE_OFFSET_MAGIC] = 0U;
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_MAGIC);
    package_length = build_valid_package(package, image, sizeof(image));
    put_u16_le(&package[OTA_PACKAGE_OFFSET_FORMAT_VERSION], 2U);
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_FORMAT_VERSION);
    package_length = build_valid_package(package, image, sizeof(image));
    put_u16_le(&package[OTA_PACKAGE_OFFSET_HEADER_BYTES], 195U);
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_HEADER_BYTES);
    package_length = build_valid_package(package, image, sizeof(image));
    memset(&package[OTA_PACKAGE_OFFSET_PRODUCT_ID], ' ', OTA_PACKAGE_IDENTIFIER_BYTES);
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_IDENTITY);
}

static void test_manifest_rejects_payload_integrity_reserved_and_layout_failures(void)
{
    static const uint8_t image[] = {0x01U, 0x02U, 0x03U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    uint32_t package_length;

    package_length = build_valid_package(package, image, sizeof(image));
    package[OTA_PACKAGE_MANIFEST_BYTES] ^= 0x01U;
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_CRC32);
    package_length = build_valid_package(package, image, sizeof(image));
    package[OTA_PACKAGE_OFFSET_IMAGE_SHA256] ^= 0x01U;
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_SHA256);
    package_length = build_valid_package(package, image, sizeof(image));
    package[OTA_PACKAGE_OFFSET_RESERVED] = 1U;
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_RESERVED);
    package_length = build_valid_package(package, image, sizeof(image));
    put_u32_le(&package[OTA_PACKAGE_OFFSET_TARGET_ADDRESS],
               TRANSPORT_OTA_APPLICATION_BASE + 4U);
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_TARGET_ADDRESS);
    package_length = build_valid_package(package, image, sizeof(image));
    assert(ota_package_validate(package, package_length,
                                OTA_PACKAGE_MANIFEST_BYTES + sizeof(image) - 1U,
                                NULL) == OTA_PACKAGE_STATUS_SLOT_OVERFLOW);
}

static void test_manifest_rejects_image_larger_than_the_locked_package_maximum(void)
{
    static uint8_t image[TRANSPORT_OTA_PACKAGE_MAX_IMAGE_BYTES + 1U];
    static uint8_t package[OTA_PACKAGE_MANIFEST_BYTES
                           + TRANSPORT_OTA_PACKAGE_MAX_IMAGE_BYTES + 1U];
    const uint32_t package_length = build_valid_package(package, image, sizeof(image));

    assert(package_length == TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES + 1U);
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_BAD_IMAGE_LENGTH);
}

static void test_manifest_rejects_valid_checksum_packages_for_foreign_identity(void)
{
    static const uint8_t image[] = {0xA1U, 0xB2U, 0xC3U};
    uint8_t package[OTA_PACKAGE_MANIFEST_BYTES + sizeof(image)];
    const uint32_t package_length = build_valid_package(package, image, sizeof(image));

    memset(&package[OTA_PACKAGE_OFFSET_PRODUCT_ID], 0, OTA_PACKAGE_IDENTIFIER_BYTES);
    put_identifier(&package[OTA_PACKAGE_OFFSET_PRODUCT_ID], "other-recorder");
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_UNEXPECTED_PRODUCT_ID);

    build_valid_package(package, image, sizeof(image));
    memset(&package[OTA_PACKAGE_OFFSET_HARDWARE_ID], 0, OTA_PACKAGE_IDENTIFIER_BYTES);
    put_identifier(&package[OTA_PACKAGE_OFFSET_HARDWARE_ID], "other-hardware");
    assert(ota_package_validate(package, package_length,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                NULL) == OTA_PACKAGE_STATUS_UNEXPECTED_HARDWARE_ID);
}

int main(void)
{
    test_crc32_and_streaming_sha256_known_vectors();
    test_sha256_handles_padding_boundaries_with_independent_vectors();
    test_sha256_streams_multiple_blocks_with_an_independent_vector();
    test_exact_python_manifest_is_decoded_and_verified();
    test_shared_header_validation_rejects_empty_images();
    test_version_parser_rejects_leading_zero_components();
    test_manifest_rejects_invalid_fixed_fields_and_identity();
    test_manifest_rejects_payload_integrity_reserved_and_layout_failures();
    test_manifest_rejects_image_larger_than_the_locked_package_maximum();
    test_manifest_rejects_valid_checksum_packages_for_foreign_identity();
    puts("ota package: PASS");
    return 0;
}
