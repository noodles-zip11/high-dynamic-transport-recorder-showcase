#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ai_model_data.h"
#include "ai_runtime.h"
#include "ota_crc32.h"
#include "ota_model_package.h"
#include "ota_sha256.h"

#define TEST_PACKAGE_BYTES (OTA_MODEL_PACKAGE_HEADER_BYTES \
                            + OTA_MODEL_PACKAGE_MAX_MODEL_BYTES \
                            + OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES)

typedef struct
{
    uint8_t bytes[TEST_PACKAGE_BYTES];
    uint32_t length;
    uint32_t fail_offset;
} fake_package_t;

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

static void put_float_le(uint8_t *data, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_u32_le(data, bits);
}

static uint32_t append_u8(uint8_t *data, uint32_t offset, uint8_t value)
{
    data[offset] = value;
    return offset + 1U;
}

static uint32_t append_u16(uint8_t *data, uint32_t offset, uint16_t value)
{
    put_u16_le(&data[offset], value);
    return offset + 2U;
}

static uint32_t append_u32(uint8_t *data, uint32_t offset, uint32_t value)
{
    put_u32_le(&data[offset], value);
    return offset + 4U;
}

static uint32_t append_float(uint8_t *data, uint32_t offset, float value)
{
    put_float_le(&data[offset], value);
    return offset + 4U;
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t build_model_payload(uint8_t *payload, const ai_model_t *model)
{
    uint32_t offset = 0U;
    uint32_t index;

    offset = append_u32(payload, offset, model->magic);
    offset = append_u16(payload, offset, model->version);
    offset = append_u8(payload, offset, model->feature_count);
    offset = append_u8(payload, offset, model->hidden_units);
    offset = append_u8(payload, offset, model->class_count);
    offset = append_u8(payload, offset, model->reserved);
    offset = append_float(payload, offset, model->input_scale);
    offset = append_float(payload, offset, model->input_zero_point);
    offset = append_float(payload, offset, model->hidden_scale);
    offset = append_float(payload, offset, model->hidden_zero_point);
    for (index = 0U; index < model->feature_count; index++)
    {
        offset = append_float(payload, offset, model->feature_mean[index]);
        offset = append_float(payload, offset, model->feature_scale[index]);
    }
    memcpy(&payload[offset], model->weights1,
           (uint32_t)model->feature_count * model->hidden_units);
    offset += (uint32_t)model->feature_count * model->hidden_units;
    offset = append_float(payload, offset, model->weights1_scale);
    for (index = 0U; index < model->hidden_units; index++)
    {
        offset = append_float(payload, offset, model->bias1[index]);
    }
    memcpy(&payload[offset], model->weights2,
           (uint32_t)model->hidden_units * model->class_count);
    offset += (uint32_t)model->hidden_units * model->class_count;
    offset = append_float(payload, offset, model->weights2_scale);
    for (index = 0U; index < model->class_count; index++)
    {
        offset = append_float(payload, offset, model->bias2[index]);
    }
    return append_u32(payload, offset, model->model_crc32);
}

static ai_model_t make_two_class_compatibility_model(
    ai_model_storage_t *storage)
{
    ai_model_t model = transport_ai_model_v1;
    uint32_t hidden_index;
    uint32_t class_index;

    model.class_count = 2U;
    model.weights2 = storage->weights2;
    model.bias2 = storage->bias2;
    for (hidden_index = 0U; hidden_index < model.hidden_units; hidden_index++)
    {
        for (class_index = 0U; class_index < model.class_count; class_index++)
        {
            storage->weights2[hidden_index * model.class_count + class_index] =
                transport_ai_model_v1.weights2[
                    hidden_index * transport_ai_model_v1.class_count
                    + class_index];
        }
    }
    for (class_index = 0U; class_index < model.class_count; class_index++)
    {
        storage->bias2[class_index] = transport_ai_model_v1.bias2[class_index];
    }
    model.model_crc32 = ai_runtime_model_crc32(&model);
    return model;
}

static void update_golden_hashes(fake_package_t *package)
{
    ota_sha256_t input_sha;
    ota_sha256_t output_sha;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    const uint8_t *golden = &package->bytes[OTA_MODEL_PACKAGE_HEADER_BYTES
                                             + get_u32_le(&package->bytes[32])];

    ota_sha256_init(&input_sha);
    ota_sha256_update(&input_sha, golden, 24U);
    ota_sha256_final(&input_sha, digest);
    memcpy(&package->bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_INPUT_SHA256],
           digest, sizeof(digest));

    ota_sha256_init(&output_sha);
    ota_sha256_update(&output_sha, &golden[24], 16U);
    ota_sha256_final(&output_sha, digest);
    memcpy(&package->bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OUTPUT_SHA256],
           digest, sizeof(digest));
}

static void update_header_crc(fake_package_t *package)
{
    put_u32_le(&package->bytes[OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32],
               ota_crc32_compute(package->bytes,
                                 OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32));
}

static fake_package_t build_valid_package_for_model(const ai_model_t *model)
{
    fake_package_t package = {0};
    const float features[AI_FEATURE_COUNT] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    ai_prediction_t prediction = {0};
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    ota_sha256_t sha256;
    uint32_t model_length;
    uint32_t golden_offset;

    package.fail_offset = UINT32_MAX;

    model_length = build_model_payload(
        &package.bytes[OTA_MODEL_PACKAGE_HEADER_BYTES], model);
    golden_offset = OTA_MODEL_PACKAGE_HEADER_BYTES + model_length;
    assert(ai_runtime_infer(model, features, &prediction) == RT_EOK);
    put_float_le(&package.bytes[golden_offset], features[0]);
    put_float_le(&package.bytes[golden_offset + 4U], features[1]);
    put_float_le(&package.bytes[golden_offset + 8U], features[2]);
    put_float_le(&package.bytes[golden_offset + 12U], features[3]);
    put_float_le(&package.bytes[golden_offset + 16U], features[4]);
    put_float_le(&package.bytes[golden_offset + 20U], features[5]);
    put_float_le(&package.bytes[golden_offset + 24U], prediction.logits[0]);
    put_float_le(&package.bytes[golden_offset + 28U], prediction.logits[1]);
    package.bytes[golden_offset + 32U] = prediction.class_index;
    put_float_le(&package.bytes[golden_offset + 36U], prediction.confidence);
    package.length = golden_offset + OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES;

    memset(package.bytes, 0, OTA_MODEL_PACKAGE_HEADER_BYTES);
    memcpy(package.bytes, "TRMD", 4U);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_FORMAT_VERSION],
               OTA_MODEL_PACKAGE_FORMAT_VERSION);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_HEADER_BYTES],
               OTA_MODEL_PACKAGE_HEADER_BYTES);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_PACKAGE_LENGTH],
               package.length);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_MODEL_VERSION],
               model->version);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_RUNTIME_VERSION],
               model->version);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_FEATURE_VERSION], 1U);
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_FEATURE_COUNT] = model->feature_count;
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_HIDDEN_UNITS] =
        model->hidden_units;
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT] =
        model->class_count;
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_QUANTIZATION] =
        OTA_MODEL_PACKAGE_QUANTIZATION_INT8;
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE_RANK] = 1U;
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE], AI_FEATURE_COUNT);
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_COUNT], 1U);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_MODEL_OFFSET],
               OTA_MODEL_PACKAGE_HEADER_BYTES);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_MODEL_LENGTH], model_length);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OFFSET], golden_offset);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_LENGTH],
               OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES);
    put_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_MODEL_PAYLOAD_CRC32],
               ota_crc32_compute(&package.bytes[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                 model_length));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &package.bytes[OTA_MODEL_PACKAGE_HEADER_BYTES],
                      model_length);
    ota_sha256_final(&sha256, digest);
    memcpy(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_MODEL_SHA256], digest,
           sizeof(digest));
    update_golden_hashes(&package);
    update_header_crc(&package);
    return package;
}

static fake_package_t build_valid_package(void)
{
    return build_valid_package_for_model(&transport_ai_model_v1);
}

static fake_package_t build_two_class_compatibility_package(void)
{
    ai_model_storage_t storage = {0};
    ai_model_t model = make_two_class_compatibility_model(&storage);

    return build_valid_package_for_model(&model);
}

static int fake_read(void *context, uint32_t offset, uint8_t *data, uint32_t length)
{
    fake_package_t *package = context;

    if (package == NULL || data == NULL || offset > package->length
        || length > package->length - offset
        || (package->fail_offset != UINT32_MAX
            && offset <= package->fail_offset
            && package->fail_offset < offset + length))
    {
        return -1;
    }
    memcpy(data, &package->bytes[offset], length);
    return 0;
}

static ota_model_package_status_t validate(fake_package_t *package,
                                           ota_model_package_scratch_t *scratch,
                                           ota_model_package_info_t *info)
{
    return ota_model_package_validate(fake_read, package, package->length,
                                      scratch, info);
}

static void test_valid_package_decodes_and_checks_golden_vector(void)
{
    fake_package_t package = build_valid_package();
    ota_model_package_scratch_t scratch;
    ota_model_package_info_t info;

    package.fail_offset = UINT32_MAX;
    assert(validate(&package, &scratch, &info) == OTA_MODEL_PACKAGE_STATUS_OK);
    assert(info.model_version == 2U);
    assert(info.feature_count == AI_FEATURE_COUNT);
    assert(info.hidden_units == 8U);
    assert(info.class_count == transport_ai_model_v1.class_count);
    assert(info.golden_vector_count == 1U);
}

static void test_two_class_package_remains_compatible(void)
{
    fake_package_t package = build_two_class_compatibility_package();
    ota_model_package_scratch_t scratch;
    ota_model_package_info_t info;

    assert(validate(&package, &scratch, &info) == OTA_MODEL_PACKAGE_STATUS_OK);
    assert(info.class_count == 2U);
}

static void test_package_rejects_class_count_outside_runtime_contract(void)
{
    fake_package_t package = build_valid_package();
    ota_model_package_scratch_t scratch;
    ota_model_package_info_t info;

    package.bytes[OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT] = 0U;
    update_header_crc(&package);
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_INVALID_CONTRACT);

    package = build_valid_package();
    package.bytes[OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT] =
        (uint8_t)(AI_MODEL_MAX_CLASSES + 1U);
    update_header_crc(&package);
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_INVALID_CONTRACT);
}

static void test_package_rejects_contract_and_integrity_mutations(void)
{
    fake_package_t package = build_valid_package();
    ota_model_package_scratch_t scratch;
    ota_model_package_info_t info;

    package.fail_offset = UINT32_MAX;
    put_u16_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE], 0U);
    update_header_crc(&package);
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_INVALID_CONTRACT);

    package = build_valid_package();
    package.bytes[OTA_MODEL_PACKAGE_HEADER_BYTES + 1U] ^= 0x01U;
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_INVALID_MODEL);

    package = build_valid_package();
    put_float_le(&package.bytes[
                     get_u32_le(&package.bytes[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OFFSET])
                     + 36U],
                 0.0F);
    update_golden_hashes(&package);
    update_header_crc(&package);
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN);
}

static void test_package_reports_storage_failure(void)
{
    fake_package_t package = build_valid_package();
    ota_model_package_scratch_t scratch;
    ota_model_package_info_t info;

    package.fail_offset = 0U;
    assert(validate(&package, &scratch, &info)
           == OTA_MODEL_PACKAGE_STATUS_STORAGE_ERROR);
}

int main(void)
{
    test_valid_package_decodes_and_checks_golden_vector();
    test_two_class_package_remains_compatible();
    test_package_rejects_class_count_outside_runtime_contract();
    test_package_rejects_contract_and_integrity_mutations();
    test_package_reports_storage_failure();
    puts("ota model package: PASS");
    return 0;
}
