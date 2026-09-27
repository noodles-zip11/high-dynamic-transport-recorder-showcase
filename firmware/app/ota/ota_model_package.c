#include "ota_model_package.h"

#include <math.h>
#include <string.h>

#include "ai_runtime.h"
#include "ota_crc32.h"
#include "ota_sha256.h"

#define OTA_MODEL_PACKAGE_MODEL_HEADER_BYTES UINT32_C(10)
#define OTA_MODEL_PACKAGE_GOLDEN_INPUT_BYTES UINT32_C(24)
#define OTA_MODEL_PACKAGE_GOLDEN_OUTPUT_BYTES UINT32_C(16)
#define OTA_MODEL_PACKAGE_GOLDEN_TOLERANCE 0.001F

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static float get_float_le(const uint8_t *data)
{
    uint32_t bits = get_u32_le(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int hash_is_zero(const uint8_t *digest)
{
    uint32_t index;

    for (index = 0U; index < OTA_SHA256_DIGEST_BYTES; index++)
    {
        if (digest[index] != 0U)
        {
            return 0;
        }
    }
    return 1;
}

static int near_value(float actual, float expected)
{
    return isfinite(actual) && isfinite(expected)
           && fabsf(actual - expected) <= OTA_MODEL_PACKAGE_GOLDEN_TOLERANCE;
}

static ota_model_package_status_t read_bytes(ota_model_package_read_fn read,
                                             void *context,
                                             uint32_t offset,
                                             uint8_t *data,
                                             uint32_t length)
{
    return read(context, offset, data, length) == 0
               ? OTA_MODEL_PACKAGE_STATUS_OK
               : OTA_MODEL_PACKAGE_STATUS_STORAGE_ERROR;
}

ota_model_package_status_t ota_model_package_validate(
    ota_model_package_read_fn read,
    void *context,
    uint32_t package_length,
    ota_model_package_scratch_t *scratch,
    ota_model_package_info_t *info)
{
    uint8_t header[OTA_MODEL_PACKAGE_HEADER_BYTES];
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_model_package_info_t parsed;
    ota_sha256_t sha256;
    uint32_t model_offset;
    uint32_t golden_offset;
    uint32_t expected_model_length;
    uint32_t index;
    ota_model_package_status_t status;

    if (read == RT_NULL || scratch == RT_NULL || info == RT_NULL
        || package_length < OTA_MODEL_PACKAGE_HEADER_BYTES)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_ARGUMENT;
    }
    status = read_bytes(read, context, 0U, header, sizeof(header));
    if (status != OTA_MODEL_PACKAGE_STATUS_OK)
    {
        return status;
    }
    if (get_u32_le(&header[OTA_MODEL_PACKAGE_OFFSET_MAGIC])
            != OTA_MODEL_PACKAGE_MAGIC
        || get_u16_le(&header[OTA_MODEL_PACKAGE_OFFSET_FORMAT_VERSION])
               != OTA_MODEL_PACKAGE_FORMAT_VERSION
        || get_u16_le(&header[OTA_MODEL_PACKAGE_OFFSET_HEADER_BYTES])
               != OTA_MODEL_PACKAGE_HEADER_BYTES
        || get_u32_le(&header[OTA_MODEL_PACKAGE_OFFSET_PACKAGE_LENGTH])
               != package_length
        || get_u32_le(&header[OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32])
               != ota_crc32_compute(header,
                                    OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32))
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_HEADER;
    }
    for (index = OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32 + sizeof(uint32_t);
         index < OTA_MODEL_PACKAGE_HEADER_BYTES; index++)
    {
        if (header[index] != 0U)
        {
            return OTA_MODEL_PACKAGE_STATUS_INVALID_HEADER;
        }
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.model_version = get_u16_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_MODEL_VERSION]);
    parsed.runtime_version = get_u16_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_RUNTIME_VERSION]);
    parsed.feature_version = get_u16_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_FEATURE_VERSION]);
    parsed.feature_count = header[OTA_MODEL_PACKAGE_OFFSET_FEATURE_COUNT];
    parsed.hidden_units = header[OTA_MODEL_PACKAGE_OFFSET_HIDDEN_UNITS];
    parsed.class_count = header[OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT];
    parsed.quantization = header[OTA_MODEL_PACKAGE_OFFSET_QUANTIZATION];
    parsed.input_shape = get_u16_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE]);
    parsed.golden_vector_count = get_u16_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_COUNT]);
    parsed.package_length = package_length;
    parsed.model_length = get_u32_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_MODEL_LENGTH]);
    parsed.golden_length = get_u32_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_LENGTH]);
    parsed.model_payload_crc32 = get_u32_le(
        &header[OTA_MODEL_PACKAGE_OFFSET_MODEL_PAYLOAD_CRC32]);
    memcpy(parsed.model_sha256,
           &header[OTA_MODEL_PACKAGE_OFFSET_MODEL_SHA256],
           sizeof(parsed.model_sha256));
    memcpy(parsed.golden_input_sha256,
           &header[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_INPUT_SHA256],
           sizeof(parsed.golden_input_sha256));
    memcpy(parsed.golden_output_sha256,
           &header[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OUTPUT_SHA256],
           sizeof(parsed.golden_output_sha256));

    model_offset = get_u32_le(&header[OTA_MODEL_PACKAGE_OFFSET_MODEL_OFFSET]);
    golden_offset = get_u32_le(&header[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OFFSET]);
    if (parsed.model_version == 0U || parsed.runtime_version != AI_MODEL_VERSION
        || parsed.feature_version != 1U
        || parsed.feature_count != AI_FEATURE_COUNT
        || parsed.hidden_units == 0U
        || parsed.hidden_units > AI_MODEL_MAX_HIDDEN_UNITS
        /* Keep the historical two-class package valid while accepting the
         * current four-class model contract. */
        || parsed.class_count < 2U
        || parsed.class_count > AI_MODEL_MAX_CLASSES
        || parsed.quantization != OTA_MODEL_PACKAGE_QUANTIZATION_INT8
        || header[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE_RANK] != 1U
        || parsed.input_shape != AI_FEATURE_COUNT
        || parsed.golden_vector_count == 0U
        || parsed.golden_vector_count > OTA_MODEL_PACKAGE_MAX_GOLDEN_VECTORS
        || parsed.model_length == 0U
        || parsed.model_length > OTA_MODEL_PACKAGE_MAX_MODEL_BYTES
        || parsed.golden_length
               != (uint32_t)parsed.golden_vector_count
                  * OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES
        || model_offset != OTA_MODEL_PACKAGE_HEADER_BYTES
        || golden_offset != model_offset + parsed.model_length
        || package_length != golden_offset + parsed.golden_length
        || hash_is_zero(parsed.model_sha256)
        || hash_is_zero(parsed.golden_input_sha256)
        || hash_is_zero(parsed.golden_output_sha256))
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_CONTRACT;
    }

    status = read_bytes(read, context, model_offset, scratch->model_bytes,
                        parsed.model_length);
    if (status != OTA_MODEL_PACKAGE_STATUS_OK)
    {
        return status;
    }
    status = read_bytes(read, context, golden_offset, scratch->golden_bytes,
                        parsed.golden_length);
    if (status != OTA_MODEL_PACKAGE_STATUS_OK)
    {
        return status;
    }
    if (ota_crc32_compute(scratch->model_bytes, parsed.model_length)
            != parsed.model_payload_crc32)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_MODEL;
    }
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, scratch->model_bytes, parsed.model_length);
    ota_sha256_final(&sha256, digest);
    if (memcmp(digest, parsed.model_sha256, sizeof(digest)) != 0
        || ai_runtime_decode_model(scratch->model_bytes, parsed.model_length,
                                   &scratch->model_storage, &scratch->model)
               != RT_EOK
        || scratch->model.hidden_units != parsed.hidden_units
        || scratch->model.class_count != parsed.class_count)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_MODEL;
    }

    ota_sha256_init(&sha256);
    for (index = 0U; index < parsed.golden_vector_count; index++)
    {
        const uint8_t *record = &scratch->golden_bytes[
            index * OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES];

        ota_sha256_update(&sha256, record, OTA_MODEL_PACKAGE_GOLDEN_INPUT_BYTES);
    }
    ota_sha256_final(&sha256, digest);
    if (memcmp(digest, parsed.golden_input_sha256, sizeof(digest)) != 0)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN;
    }
    ota_sha256_init(&sha256);
    for (index = 0U; index < parsed.golden_vector_count; index++)
    {
        const uint8_t *record = &scratch->golden_bytes[
            index * OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES];

        ota_sha256_update(&sha256, &record[OTA_MODEL_PACKAGE_GOLDEN_INPUT_BYTES],
                          OTA_MODEL_PACKAGE_GOLDEN_OUTPUT_BYTES);
    }
    ota_sha256_final(&sha256, digest);
    if (memcmp(digest, parsed.golden_output_sha256, sizeof(digest)) != 0)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN;
    }

    expected_model_length = 10U + 16U + (uint32_t)AI_FEATURE_COUNT * 8U
                            + (uint32_t)AI_FEATURE_COUNT * parsed.hidden_units
                            + 4U + (uint32_t)parsed.hidden_units * 4U
                            + (uint32_t)parsed.hidden_units * parsed.class_count
                            + 4U + (uint32_t)parsed.class_count * 4U + 4U;
    if (parsed.model_length != expected_model_length)
    {
        return OTA_MODEL_PACKAGE_STATUS_INVALID_MODEL;
    }
    for (index = 0U; index < parsed.golden_vector_count; index++)
    {
        const uint8_t *record = &scratch->golden_bytes[
            index * OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES];
        ai_prediction_t prediction = {0};
        float features[AI_FEATURE_COUNT];
        uint32_t feature_index;
        uint8_t expected_class;

        for (feature_index = 0U; feature_index < AI_FEATURE_COUNT; feature_index++)
        {
            features[feature_index] = get_float_le(
                &record[feature_index * sizeof(float)]);
        }
        if (record[33U] != 0U || record[34U] != 0U || record[35U] != 0U
            || ai_runtime_infer(&scratch->model, features, &prediction) != RT_EOK)
        {
            return OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN;
        }
        expected_class = record[32U];
        if (expected_class >= scratch->model.class_count
            || prediction.class_index != expected_class
            || !near_value(prediction.logits[0], get_float_le(&record[24U]))
            || !near_value(prediction.logits[1], get_float_le(&record[28U]))
            || !near_value(prediction.confidence, get_float_le(&record[36U])))
        {
            return OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN;
        }
    }
    *info = parsed;
    return OTA_MODEL_PACKAGE_STATUS_OK;
}
