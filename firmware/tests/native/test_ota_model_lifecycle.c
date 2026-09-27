#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ai_model_data.h"
#include "ai_runtime.h"
#include "ota_crc32.h"
#include "ota_model_lifecycle.h"
#include "ota_sha256.h"

#define TEST_SLOT_BYTES 1024U
#define TEST_STATE_BYTES 4096U
#define TEST_PACKAGE_BYTES 512U
#define TEST_MODEL_STATE_COMMIT_OFFSET 128U

typedef struct
{
    uint8_t slots[2][TEST_SLOT_BYTES];
    uint8_t state[OTA_MODEL_STATE_MAX_BANKS][TEST_STATE_BYTES];
    uint8_t fail_state_write;
    uint8_t fail_state_write_after_copy;
    uint8_t fail_state_commit_write;
    uint8_t fail_state_bank_erase;
    uint8_t fail_state_read_once;
    uint32_t state_read_count;
    uint32_t fail_state_read_at;
    uint32_t state_bank_erase_count[OTA_MODEL_STATE_MAX_BANKS];
    uint32_t last_erase_length;
} fake_model_storage_t;

typedef struct
{
    const ai_model_t *active_model;
    uint32_t prepare_count;
    uint32_t publish_count;
    uint32_t abort_count;
    uint32_t quarantine_count;
    uint8_t fail_next;
    uint8_t fail_publish_next;
    uint8_t gate_held;
} fake_runtime_activation_t;

static int fake_prepare_runtime_model(const ai_model_t *model, void *context)
{
    fake_runtime_activation_t *activation = context;

    if (activation == NULL || model == NULL)
    {
        return -1;
    }
    activation->prepare_count++;
    if (activation->gate_held)
    {
        return -1;
    }
    if (activation->fail_next)
    {
        activation->fail_next = 0U;
        return -1;
    }
    activation->gate_held = 1U;
    return 0;
}

static int fake_publish_runtime_model(const ai_model_t *model, void *context)
{
    fake_runtime_activation_t *activation = context;

    assert(activation != NULL && model != NULL);
    if (activation->fail_publish_next)
    {
        activation->fail_publish_next = 0U;
        return -1;
    }
    activation->publish_count++;
    activation->active_model = model;
    activation->gate_held = 0U;
    return 0;
}

static void fake_abort_runtime_model(void *context)
{
    fake_runtime_activation_t *activation = context;

    assert(activation != NULL);
    activation->abort_count++;
    activation->gate_held = 0U;
}

static void fake_quarantine_runtime_model(void *context)
{
    fake_runtime_activation_t *activation = context;

    assert(activation != NULL);
    activation->quarantine_count++;
    activation->active_model = NULL;
    activation->gate_held = 0U;
}

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

static uint32_t build_package(uint8_t *package)
{
    const ai_model_t *model = &transport_ai_model_v1;
    const float features[AI_FEATURE_COUNT] = {0};
    ai_prediction_t prediction = {0};
    ota_sha256_t sha256;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];
    uint32_t model_length = 0U;
    uint32_t golden_offset;
    uint32_t package_length;
    uint32_t index;

    memset(package, 0, TEST_PACKAGE_BYTES);
    model_length = append_u32(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                              model_length, model->magic);
    model_length = append_u16(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                              model_length, model->version);
    model_length = append_u8(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                             model_length, model->feature_count);
    model_length = append_u8(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                             model_length, model->hidden_units);
    model_length = append_u8(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                             model_length, model->class_count);
    model_length = append_u8(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                             model_length, model->reserved);
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->input_scale);
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->input_zero_point);
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->hidden_scale);
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->hidden_zero_point);
    for (index = 0U; index < model->feature_count; index++)
    {
        model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                    model_length, model->feature_mean[index]);
        model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                    model_length, model->feature_scale[index]);
    }
    memcpy(&package[OTA_MODEL_PACKAGE_HEADER_BYTES + model_length],
           model->weights1, (uint32_t)model->feature_count * model->hidden_units);
    model_length += (uint32_t)model->feature_count * model->hidden_units;
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->weights1_scale);
    for (index = 0U; index < model->hidden_units; index++)
    {
        model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                    model_length, model->bias1[index]);
    }
    memcpy(&package[OTA_MODEL_PACKAGE_HEADER_BYTES + model_length],
           model->weights2, (uint32_t)model->hidden_units * model->class_count);
    model_length += (uint32_t)model->hidden_units * model->class_count;
    model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                model_length, model->weights2_scale);
    for (index = 0U; index < model->class_count; index++)
    {
        model_length = append_float(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                    model_length, model->bias2[index]);
    }
    model_length = append_u32(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                              model_length, model->model_crc32);
    golden_offset = OTA_MODEL_PACKAGE_HEADER_BYTES + model_length;
    assert(ai_runtime_infer(model, features, &prediction) == RT_EOK);
    for (index = 0U; index < AI_FEATURE_COUNT; index++)
    {
        put_float_le(&package[golden_offset + index * 4U], features[index]);
    }
    put_float_le(&package[golden_offset + 24U], prediction.logits[0]);
    put_float_le(&package[golden_offset + 28U], prediction.logits[1]);
    package[golden_offset + 32U] = prediction.class_index;
    put_float_le(&package[golden_offset + 36U], prediction.confidence);
    package_length = golden_offset + OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES;

    memcpy(package, "TRMD", 4U);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_FORMAT_VERSION],
               OTA_MODEL_PACKAGE_FORMAT_VERSION);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_HEADER_BYTES],
               OTA_MODEL_PACKAGE_HEADER_BYTES);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_PACKAGE_LENGTH], package_length);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_MODEL_VERSION], 2U);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_RUNTIME_VERSION], 2U);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_FEATURE_VERSION], 1U);
    package[OTA_MODEL_PACKAGE_OFFSET_FEATURE_COUNT] = AI_FEATURE_COUNT;
    package[OTA_MODEL_PACKAGE_OFFSET_HIDDEN_UNITS] = model->hidden_units;
    package[OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT] = model->class_count;
    package[OTA_MODEL_PACKAGE_OFFSET_QUANTIZATION] =
        OTA_MODEL_PACKAGE_QUANTIZATION_INT8;
    package[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE_RANK] = 1U;
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE], AI_FEATURE_COUNT);
    put_u16_le(&package[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_COUNT], 1U);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_MODEL_OFFSET],
               OTA_MODEL_PACKAGE_HEADER_BYTES);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_MODEL_LENGTH], model_length);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OFFSET], golden_offset);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_LENGTH],
               OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES);
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_MODEL_PAYLOAD_CRC32],
               ota_crc32_compute(&package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                                 model_length));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &package[OTA_MODEL_PACKAGE_HEADER_BYTES],
                      model_length);
    ota_sha256_final(&sha256, digest);
    memcpy(&package[OTA_MODEL_PACKAGE_OFFSET_MODEL_SHA256], digest, sizeof(digest));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &package[golden_offset], 24U);
    ota_sha256_final(&sha256, digest);
    memcpy(&package[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_INPUT_SHA256], digest,
           sizeof(digest));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, &package[golden_offset + 24U], 16U);
    ota_sha256_final(&sha256, digest);
    memcpy(&package[OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OUTPUT_SHA256], digest,
           sizeof(digest));
    put_u32_le(&package[OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32],
               ota_crc32_compute(package, OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32));
    return package_length;
}

static int fake_slot_index(ota_model_slot_t slot)
{
    return slot == OTA_MODEL_SLOT_A ? 0 : slot == OTA_MODEL_SLOT_B ? 1 : -1;
}

static int fake_erase_slot(void *context, ota_model_slot_t slot,
                           uint32_t offset, uint32_t length)
{
    fake_model_storage_t *storage = context;
    const int index = fake_slot_index(slot);

    if (storage == NULL || index < 0 || offset != 0U || length == 0U
        || length > TEST_SLOT_BYTES)
    {
        return -1;
    }
    storage->last_erase_length = length;
    memset(storage->slots[index], 0xFF, length);
    return 0;
}

static int fake_write_slot(void *context, ota_model_slot_t slot, uint32_t offset,
                           const uint8_t *data, uint32_t length)
{
    fake_model_storage_t *storage = context;
    const int index = fake_slot_index(slot);

    if (storage == NULL || index < 0 || data == NULL || offset > TEST_SLOT_BYTES
        || length > TEST_SLOT_BYTES - offset)
    {
        return -1;
    }
    memcpy(&storage->slots[index][offset], data, length);
    return 0;
}

static int fake_read_slot(void *context, ota_model_slot_t slot, uint32_t offset,
                          uint8_t *data, uint32_t length)
{
    fake_model_storage_t *storage = context;
    const int index = fake_slot_index(slot);

    if (storage == NULL || index < 0 || data == NULL || offset > TEST_SLOT_BYTES
        || length > TEST_SLOT_BYTES - offset)
    {
        return -1;
    }
    memcpy(data, &storage->slots[index][offset], length);
    return 0;
}

static int fake_state_erase(void *context, uint32_t offset, uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL || offset > TEST_STATE_BYTES
        || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    memset(&storage->state[0U][offset], 0xFF, length);
    return 0;
}

static int fake_state_write(void *context, uint32_t offset, const uint8_t *data,
                            uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL || storage->fail_state_write || data == NULL
        || offset > TEST_STATE_BYTES || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    if (storage->fail_state_commit_write
        && offset % OTA_MODEL_STATE_RECORD_BYTES
               == TEST_MODEL_STATE_COMMIT_OFFSET)
    {
        return -1;
    }
    memcpy(&storage->state[0U][offset], data, length);
    return storage->fail_state_write_after_copy ? -1 : 0;
}

static int fake_state_read(void *context, uint32_t offset, uint8_t *data,
                           uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL)
    {
        return -1;
    }
    storage->state_read_count++;
    if (storage->fail_state_read_once
        || (storage->fail_state_read_at != 0U
            && storage->state_read_count == storage->fail_state_read_at))
    {
        storage->fail_state_read_once = 0U;
        storage->fail_state_read_at = 0U;
        return -1;
    }
    if (data == NULL || offset > TEST_STATE_BYTES
        || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    memcpy(data, &storage->state[0U][offset], length);
    return 0;
}

static int fake_state_bank_erase(void *context, uint8_t bank,
                                 uint32_t offset, uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL || bank >= OTA_MODEL_STATE_MAX_BANKS
        || storage->fail_state_bank_erase || offset > TEST_STATE_BYTES
        || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    storage->state_bank_erase_count[bank]++;
    memset(&storage->state[bank][offset], 0xFF, length);
    return 0;
}

static int fake_state_bank_write(void *context, uint8_t bank, uint32_t offset,
                                 const uint8_t *data, uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL || bank >= OTA_MODEL_STATE_MAX_BANKS
        || storage->fail_state_write || data == NULL
        || offset > TEST_STATE_BYTES || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    if (storage->fail_state_commit_write
        && offset % OTA_MODEL_STATE_RECORD_BYTES
               == TEST_MODEL_STATE_COMMIT_OFFSET)
    {
        return -1;
    }
    memcpy(&storage->state[bank][offset], data, length);
    return storage->fail_state_write_after_copy ? -1 : 0;
}

static int fake_state_bank_read(void *context, uint8_t bank, uint32_t offset,
                                uint8_t *data, uint32_t length)
{
    fake_model_storage_t *storage = context;

    if (storage == NULL || bank >= OTA_MODEL_STATE_MAX_BANKS
        || data == NULL || offset > TEST_STATE_BYTES
        || length > TEST_STATE_BYTES - offset)
    {
        return -1;
    }
    memcpy(data, &storage->state[bank][offset], length);
    return 0;
}

static ota_model_storage_t make_storage(fake_model_storage_t *storage)
{
    return (ota_model_storage_t){
        .erase_slot = fake_erase_slot,
        .write_slot = fake_write_slot,
        .read_slot = fake_read_slot,
        .erase_state = fake_state_erase,
        .write_state = fake_state_write,
        .read_state = fake_state_read,
        .context = storage,
        .slot_capacity_bytes = TEST_SLOT_BYTES,
        .state_capacity_bytes = TEST_STATE_BYTES,
    };
}

static ota_model_storage_t make_dual_storage(fake_model_storage_t *storage)
{
    ota_model_storage_t result = make_storage(storage);

    result.erase_state_bank = fake_state_bank_erase;
    result.write_state_bank = fake_state_bank_write;
    result.read_state_bank = fake_state_bank_read;
    result.state_bank_count = OTA_MODEL_STATE_MAX_BANKS;
    return result;
}

static void initialize_storage(fake_model_storage_t *storage)
{
    memset(storage, 0xFF, sizeof(*storage));
    storage->fail_state_write = 0U;
    storage->fail_state_write_after_copy = 0U;
    storage->fail_state_commit_write = 0U;
    storage->fail_state_bank_erase = 0U;
    storage->fail_state_read_once = 0U;
    storage->state_read_count = 0U;
    storage->fail_state_read_at = 0U;
    memset(storage->state_bank_erase_count, 0,
           sizeof(storage->state_bank_erase_count));
    storage->last_erase_length = 0U;
}

static void upload(ota_model_lifecycle_t *lifecycle, const uint8_t *package,
                   uint32_t length, uint32_t limit)
{
    uint32_t offset = 0U;

    assert(ota_model_lifecycle_begin(lifecycle, length) == OTA_DOWNLOAD_STATUS_OK);
    while (offset < limit)
    {
        const uint32_t chunk = (limit - offset) < 97U ? limit - offset : 97U;

        assert(ota_model_lifecycle_write(lifecycle, offset, &package[offset],
                                         chunk,
                                         ota_crc32_compute(&package[offset], chunk))
               == OTA_DOWNLOAD_STATUS_OK);
        offset += chunk;
    }
}

static void test_valid_update_and_reboot_keep_active_model(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    assert(!ota_model_lifecycle_has_valid_model(&lifecycle));
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);
    assert(ota_model_lifecycle_has_valid_model(&lifecycle));
    assert(ota_model_lifecycle_get_active_model(&lifecycle) != NULL);
    assert(ota_model_lifecycle_cancel(&lifecycle)
           == OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT);

    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_A);
    assert(ota_model_lifecycle_has_valid_model(&remounted));
}

static void test_interrupted_or_invalid_update_does_not_replace_active(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    uint8_t invalid_package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);

    upload(&lifecycle, package, package_length, package_length / 2U);
    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_A);
    assert(ota_model_lifecycle_has_valid_model(&remounted));
    assert(ota_model_lifecycle_write(&lifecycle, package_length / 2U,
                                     &package[package_length / 2U],
                                     package_length - package_length / 2U,
                                     ota_crc32_compute(&package[package_length / 2U],
                                                       package_length - package_length / 2U))
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);

    memcpy(invalid_package, package, package_length);
    invalid_package[OTA_MODEL_PACKAGE_HEADER_BYTES + 1U] ^= 0x01U;
    upload(&lifecycle, invalid_package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_INVALID_PACKAGE);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);
    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_B);
}

static void test_state_commit_failure_and_both_invalid_use_fallback(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    storage.fail_state_commit_write = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    storage.fail_state_commit_write = 0U;
    assert(ota_model_lifecycle_is_activation_uncertain(&lifecycle));
    assert(ota_model_lifecycle_get_active_slot(&lifecycle)
           == OTA_MODEL_SLOT_INVALID);
    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_A);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));

    storage.fail_state_write_after_copy = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    storage.fail_state_write_after_copy = 0U;
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);
    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_B);

    memset(storage.slots, 0x00, sizeof(storage.slots));
    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(!ota_model_lifecycle_has_valid_model(&remounted));
    assert(ota_model_lifecycle_get_active_slot(&remounted)
           == OTA_MODEL_SLOT_INVALID);
    assert(ota_model_lifecycle_get_runtime_model(&remounted)
           == &transport_ai_model_v1);
}

static void test_model_begin_erases_only_package_range(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage.slots[0][900U] = 0x5AU;
    storage_ops = make_storage(&storage);
    storage_ops.erase_block_bytes = 256U;
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    assert(ota_model_lifecycle_begin(&lifecycle,
                                     OTA_MODEL_PACKAGE_MAX_BYTES + 1U)
           == OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT);
    assert(storage.last_erase_length == 0U);
    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_OK);
    assert(storage.last_erase_length == 512U);
    assert(storage.slots[0][900U] == 0x5AU);
}

static void test_full_state_log_rejects_begin_before_erasing_model_slot(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    memset(storage.state, 0x00, sizeof(storage.state));
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(storage.last_erase_length == 0U);
}

static void test_runtime_activation_is_atomic_and_tracks_active_slot(void)
{
    fake_model_storage_t storage = {0};
    fake_runtime_activation_t activation = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);
    const ai_model_t *first_model;
    const ai_model_t *second_model;

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    ota_model_lifecycle_set_runtime_activation(
        &lifecycle, fake_prepare_runtime_model, fake_publish_runtime_model,
        fake_abort_runtime_model, fake_quarantine_runtime_model, &activation);

    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    first_model = ota_model_lifecycle_get_active_model(&lifecycle);
    assert(first_model != NULL && activation.active_model == first_model);

    activation.gate_held = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(activation.gate_held == 1U);
    activation.gate_held = 0U;

    activation.fail_next = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);
    assert(activation.active_model == first_model);

    storage.fail_state_commit_write = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    storage.fail_state_commit_write = 0U;
    assert(ota_model_lifecycle_is_activation_uncertain(&lifecycle));
    assert(ota_model_lifecycle_get_active_slot(&lifecycle)
           == OTA_MODEL_SLOT_INVALID);
    assert(activation.active_model == NULL);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    ota_model_lifecycle_set_runtime_activation(
        &lifecycle, fake_prepare_runtime_model, fake_publish_runtime_model,
        fake_abort_runtime_model, fake_quarantine_runtime_model, &activation);
    first_model = ota_model_lifecycle_get_active_model(&lifecycle);
    assert(first_model != NULL);
    activation.active_model = first_model;

    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);
    second_model = ota_model_lifecycle_get_active_model(&lifecycle);
    assert(activation.active_model == second_model);

    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);
    assert(activation.active_model == ota_model_lifecycle_get_active_model(
                                      &lifecycle));
    assert(activation.active_model == first_model);
    assert(activation.active_model != second_model);
    assert(activation.prepare_count == 6U);
    assert(activation.publish_count == 3U);
    assert(activation.abort_count == 0U);
    assert(activation.quarantine_count == 1U);
}

static void test_ambiguous_commit_quarantines_until_reboot(void)
{
    fake_model_storage_t storage = {0};
    fake_runtime_activation_t activation = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    ota_model_lifecycle_set_runtime_activation(
        &lifecycle, fake_prepare_runtime_model, fake_publish_runtime_model,
        fake_abort_runtime_model, fake_quarantine_runtime_model, &activation);

    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    storage.fail_state_read_at = storage.state_read_count + 2U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(ota_model_lifecycle_is_activation_uncertain(&lifecycle));
    assert(ota_model_lifecycle_get_active_slot(&lifecycle)
           == OTA_MODEL_SLOT_INVALID);
    assert(ota_model_lifecycle_get_active_model(&lifecycle) == NULL);
    assert(ota_model_lifecycle_get_runtime_model(&lifecycle) == NULL);
    assert(activation.quarantine_count == 1U);

    storage.last_erase_length = 0U;
    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(ota_model_lifecycle_cancel(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(storage.last_erase_length == 0U);

    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(!ota_model_lifecycle_is_activation_uncertain(&remounted));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_B);
    assert(ota_model_lifecycle_has_valid_model(&remounted));
}

static void test_publish_failure_quarantines_until_reboot(void)
{
    fake_model_storage_t storage = {0};
    fake_runtime_activation_t activation = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);

    initialize_storage(&storage);
    storage_ops = make_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    ota_model_lifecycle_set_runtime_activation(
        &lifecycle, fake_prepare_runtime_model, fake_publish_runtime_model,
        fake_abort_runtime_model, fake_quarantine_runtime_model, &activation);

    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);

    activation.fail_publish_next = 1U;
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(ota_model_lifecycle_is_activation_uncertain(&lifecycle));
    assert(ota_model_lifecycle_get_active_slot(&lifecycle)
           == OTA_MODEL_SLOT_INVALID);
    assert(ota_model_lifecycle_get_active_model(&lifecycle) == NULL);
    assert(ota_model_lifecycle_get_runtime_model(&lifecycle) == NULL);
    assert(activation.quarantine_count == 1U);
    assert(activation.gate_held == 0U);

    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(!ota_model_lifecycle_is_activation_uncertain(&remounted));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_B);
    assert(ota_model_lifecycle_has_valid_model(&remounted));
}

static void test_dual_state_banks_compact_without_losing_active_model(void)
{
    fake_model_storage_t storage = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);
    uint32_t update_index;

    initialize_storage(&storage);
    storage_ops = make_dual_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    for (update_index = 0U; update_index < 50U; update_index++)
    {
        upload(&lifecycle, package, package_length, package_length);
        assert(ota_model_lifecycle_finalize(&lifecycle)
               == OTA_DOWNLOAD_STATUS_OK);
    }
    assert(lifecycle.state_full != 0U);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);

    storage.fail_state_bank_erase = 1U;
    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);
    storage.fail_state_bank_erase = 0U;

    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_OK);
    assert(storage.state_bank_erase_count[0] == 1U);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_B);
    upload(&lifecycle, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&lifecycle) == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);

    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_has_valid_model(&remounted));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_A);
}

static void test_gc_never_erases_authoritative_bank(void)
{
    fake_model_storage_t storage = {0};
    fake_runtime_activation_t activation = {0};
    ota_model_lifecycle_t lifecycle;
    ota_model_lifecycle_t remounted;
    ota_model_storage_t storage_ops;
    uint8_t package[TEST_PACKAGE_BYTES];
    const uint32_t package_length = build_package(package);
    uint32_t update_index;

    initialize_storage(&storage);
    storage_ops = make_dual_storage(&storage);
    assert(ota_model_lifecycle_init(&lifecycle, &storage_ops));
    ota_model_lifecycle_set_runtime_activation(
        &lifecycle, fake_prepare_runtime_model, fake_publish_runtime_model,
        fake_abort_runtime_model, fake_quarantine_runtime_model, &activation);

    for (update_index = 0U; update_index < 25U; update_index++)
    {
        upload(&lifecycle, package, package_length, package_length);
        assert(ota_model_lifecycle_finalize(&lifecycle)
               == OTA_DOWNLOAD_STATUS_OK);
    }
    assert(lifecycle.authoritative_state_valid != 0U);
    assert(lifecycle.authoritative_state_bank == 0U);
    assert(lifecycle.state_bank == 1U);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);

    for (update_index = 0U; update_index < 25U; update_index++)
    {
        activation.fail_next = 1U;
        upload(&lifecycle, package, package_length, package_length);
        assert(ota_model_lifecycle_finalize(&lifecycle)
               == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    }
    assert(lifecycle.state_full != 0U);
    assert(lifecycle.authoritative_state_bank == 0U);
    assert(lifecycle.state_bank == 1U);

    storage.fail_state_commit_write = 1U;
    assert(ota_model_lifecycle_begin(&lifecycle, package_length)
           == OTA_DOWNLOAD_STATUS_STORAGE_ERROR);
    assert(storage.state_bank_erase_count[0] == 0U);
    assert(storage.state_bank_erase_count[1] == 1U);
    assert(ota_model_lifecycle_get_active_slot(&lifecycle) == OTA_MODEL_SLOT_A);
    storage.fail_state_commit_write = 0U;

    assert(ota_model_lifecycle_init(&remounted, &storage_ops));
    assert(ota_model_lifecycle_has_valid_model(&remounted));
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_A);
    upload(&remounted, package, package_length, package_length);
    assert(ota_model_lifecycle_finalize(&remounted)
           == OTA_DOWNLOAD_STATUS_OK);
    assert(ota_model_lifecycle_get_active_slot(&remounted) == OTA_MODEL_SLOT_B);
}

int main(void)
{
    test_valid_update_and_reboot_keep_active_model();
    test_interrupted_or_invalid_update_does_not_replace_active();
    test_state_commit_failure_and_both_invalid_use_fallback();
    test_model_begin_erases_only_package_range();
    test_full_state_log_rejects_begin_before_erasing_model_slot();
    test_runtime_activation_is_atomic_and_tracks_active_slot();
    test_ambiguous_commit_quarantines_until_reboot();
    test_publish_failure_quarantines_until_reboot();
    test_dual_state_banks_compact_without_losing_active_model();
    test_gc_never_erases_authoritative_bank();
    puts("ota model lifecycle: PASS");
    return 0;
}
