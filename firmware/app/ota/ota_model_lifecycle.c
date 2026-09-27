#include "ota_model_lifecycle.h"

#include <string.h>

#include "ai_model_data.h"
#include "ota_crc32.h"

#define MODEL_STATE_OFFSET_MAGIC UINT32_C(0)
#define MODEL_STATE_OFFSET_FORMAT_VERSION UINT32_C(4)
#define MODEL_STATE_OFFSET_HEADER_BYTES UINT32_C(6)
#define MODEL_STATE_OFFSET_GENERATION UINT32_C(8)
#define MODEL_STATE_OFFSET_ACTIVE_SLOT UINT32_C(12)
#define MODEL_STATE_OFFSET_MODEL_VERSION UINT32_C(14)
#define MODEL_STATE_OFFSET_PACKAGE_LENGTH UINT32_C(16)
#define MODEL_STATE_OFFSET_MODEL_LENGTH UINT32_C(20)
#define MODEL_STATE_OFFSET_MODEL_PAYLOAD_CRC32 UINT32_C(24)
#define MODEL_STATE_OFFSET_MODEL_SHA256 UINT32_C(28)
#define MODEL_STATE_OFFSET_GOLDEN_INPUT_SHA256 UINT32_C(60)
#define MODEL_STATE_OFFSET_GOLDEN_OUTPUT_SHA256 UINT32_C(92)
#define MODEL_STATE_OFFSET_RECORD_CRC32 UINT32_C(124)
#define MODEL_STATE_OFFSET_COMMIT UINT32_C(128)
#define MODEL_STATE_COMMIT_BYTES UINT32_C(4)

typedef struct
{
    uint32_t generation;
    ota_model_slot_t slot;
    ota_model_package_info_t info;
} model_state_view_t;

typedef struct
{
    ota_model_lifecycle_t *lifecycle;
    ota_model_slot_t slot;
} package_reader_context_t;

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
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

static int hash_is_zero(const uint8_t *digest)
{
    uint32_t index;

    for (index = 0U; index < 32U; index++)
    {
        if (digest[index] != 0U)
        {
            return 0;
        }
    }
    return 1;
}

static int model_slot_index(ota_model_slot_t slot)
{
    return slot == OTA_MODEL_SLOT_A ? 0 : slot == OTA_MODEL_SLOT_B ? 1 : -1;
}

static uint8_t model_state_bank_count(const ota_model_storage_t *storage)
{
    return storage != NULL && storage->state_bank_count != 0U
               ? storage->state_bank_count
               : 1U;
}

static int model_storage_is_valid(const ota_model_storage_t *storage)
{
    const uint8_t state_bank_count = model_state_bank_count(storage);

    if (storage == NULL || storage->erase_slot == NULL
        || storage->write_slot == NULL || storage->read_slot == NULL
        || storage->slot_capacity_bytes == 0U
        || storage->state_capacity_bytes < OTA_MODEL_STATE_RECORD_BYTES
        || storage->state_capacity_bytes / OTA_MODEL_STATE_RECORD_BYTES == 0U
        || state_bank_count > OTA_MODEL_STATE_MAX_BANKS)
    {
        return 0;
    }
    if (state_bank_count == 1U)
    {
        return storage->write_state != NULL && storage->read_state != NULL;
    }
    return storage->erase_state_bank != NULL
           && storage->write_state_bank != NULL
           && storage->read_state_bank != NULL;
}

static int model_state_read(const ota_model_lifecycle_t *lifecycle,
                            uint8_t bank, uint32_t offset, uint8_t *data,
                            uint32_t length)
{
    const ota_model_storage_t *storage =
        lifecycle == NULL ? NULL : &lifecycle->storage;

    if (storage == NULL || bank >= model_state_bank_count(storage))
    {
        return -1;
    }
    if (model_state_bank_count(storage) > 1U)
    {
        return storage->read_state_bank(storage->context, bank, offset, data,
                                        length);
    }
    return storage->read_state(storage->context, offset, data, length);
}

static int model_state_write(const ota_model_lifecycle_t *lifecycle,
                             uint8_t bank, uint32_t offset,
                             const uint8_t *data, uint32_t length)
{
    const ota_model_storage_t *storage =
        lifecycle == NULL ? NULL : &lifecycle->storage;

    if (storage == NULL || bank >= model_state_bank_count(storage))
    {
        return -1;
    }
    if (model_state_bank_count(storage) > 1U)
    {
        return storage->write_state_bank(storage->context, bank, offset, data,
                                         length);
    }
    return storage->write_state(storage->context, offset, data, length);
}

static int model_state_erase(const ota_model_lifecycle_t *lifecycle,
                             uint8_t bank, uint32_t offset, uint32_t length)
{
    const ota_model_storage_t *storage =
        lifecycle == NULL ? NULL : &lifecycle->storage;

    if (storage == NULL || bank >= model_state_bank_count(storage))
    {
        return -1;
    }
    if (model_state_bank_count(storage) > 1U)
    {
        return storage->erase_state_bank(storage->context, bank, offset,
                                         length);
    }
    return storage->erase_state == NULL
               ? -1
               : storage->erase_state(storage->context, offset, length);
}

static int record_is_erased(const uint8_t *record)
{
    uint32_t index;

    for (index = 0U; index < OTA_MODEL_STATE_RECORD_BYTES; index++)
    {
        if (record[index] != 0xFFU)
        {
            return 0;
        }
    }
    return 1;
}

static int decode_state_record(const uint8_t *record, model_state_view_t *view)
{
    if (record == NULL || view == NULL
        || get_u32_le(&record[MODEL_STATE_OFFSET_MAGIC]) != OTA_MODEL_STATE_MAGIC
        || get_u16_le(&record[MODEL_STATE_OFFSET_FORMAT_VERSION])
               != OTA_MODEL_STATE_FORMAT_VERSION
        || get_u16_le(&record[MODEL_STATE_OFFSET_HEADER_BYTES])
               != OTA_MODEL_STATE_RECORD_BYTES
        || get_u32_le(&record[MODEL_STATE_OFFSET_RECORD_CRC32])
               != ota_crc32_compute(record, MODEL_STATE_OFFSET_RECORD_CRC32)
        || get_u32_le(&record[MODEL_STATE_OFFSET_COMMIT])
               != OTA_MODEL_STATE_COMMIT_MARKER
        || (record[MODEL_STATE_OFFSET_ACTIVE_SLOT] != OTA_MODEL_SLOT_A
            && record[MODEL_STATE_OFFSET_ACTIVE_SLOT] != OTA_MODEL_SLOT_B))
    {
        return 0;
    }
    memset(view, 0, sizeof(*view));
    view->generation = get_u32_le(&record[MODEL_STATE_OFFSET_GENERATION]);
    view->slot = (ota_model_slot_t)record[MODEL_STATE_OFFSET_ACTIVE_SLOT];
    view->info.model_version =
        get_u16_le(&record[MODEL_STATE_OFFSET_MODEL_VERSION]);
    view->info.package_length =
        get_u32_le(&record[MODEL_STATE_OFFSET_PACKAGE_LENGTH]);
    view->info.model_length =
        get_u32_le(&record[MODEL_STATE_OFFSET_MODEL_LENGTH]);
    view->info.model_payload_crc32 =
        get_u32_le(&record[MODEL_STATE_OFFSET_MODEL_PAYLOAD_CRC32]);
    memcpy(view->info.model_sha256,
           &record[MODEL_STATE_OFFSET_MODEL_SHA256],
           sizeof(view->info.model_sha256));
    memcpy(view->info.golden_input_sha256,
           &record[MODEL_STATE_OFFSET_GOLDEN_INPUT_SHA256],
           sizeof(view->info.golden_input_sha256));
    memcpy(view->info.golden_output_sha256,
           &record[MODEL_STATE_OFFSET_GOLDEN_OUTPUT_SHA256],
           sizeof(view->info.golden_output_sha256));
    return view->generation != 0U && view->info.package_length != 0U
           && view->info.model_length != 0U
           && !hash_is_zero(view->info.model_sha256)
           && !hash_is_zero(view->info.golden_input_sha256)
           && !hash_is_zero(view->info.golden_output_sha256);
}

static int package_reader(void *context, uint32_t offset, uint8_t *data,
                          uint32_t length)
{
    package_reader_context_t *reader = context;

    if (reader == NULL || reader->lifecycle == NULL
        || reader->slot == OTA_MODEL_SLOT_INVALID)
    {
        return -1;
    }
    return reader->lifecycle->storage.read_slot(
        reader->lifecycle->storage.context, reader->slot, offset, data, length);
}

static int package_matches_state(const ota_model_package_info_t *package,
                                 const model_state_view_t *state)
{
    return package != NULL && state != NULL
           && package->model_version == state->info.model_version
           && package->package_length == state->info.package_length
           && package->model_length == state->info.model_length
           && package->model_payload_crc32 == state->info.model_payload_crc32
           && memcmp(package->model_sha256, state->info.model_sha256,
                     sizeof(package->model_sha256)) == 0
           && memcmp(package->golden_input_sha256,
                     state->info.golden_input_sha256,
                     sizeof(package->golden_input_sha256)) == 0
           && memcmp(package->golden_output_sha256,
                     state->info.golden_output_sha256,
                     sizeof(package->golden_output_sha256)) == 0;
}

static int validate_slot(ota_model_lifecycle_t *lifecycle,
                         ota_model_slot_t slot,
                         uint32_t package_length,
                         ota_model_package_info_t *info)
{
    package_reader_context_t reader = {
        .lifecycle = lifecycle,
        .slot = slot,
    };

    return ota_model_package_validate(package_reader, &reader, package_length,
                                      &lifecycle->package_scratch, info)
           == OTA_MODEL_PACKAGE_STATUS_OK;
}

static int select_active_model(ota_model_lifecycle_t *lifecycle,
                               const model_state_view_t *state)
{
    ota_model_package_info_t info;
    const int slot_index = model_slot_index(state == NULL
                                                ? OTA_MODEL_SLOT_INVALID
                                                : state->slot);

    if (state == NULL || slot_index < 0
        || state->info.package_length > lifecycle->storage.slot_capacity_bytes
        || !validate_slot(lifecycle, state->slot, state->info.package_length,
                          &info)
        || !package_matches_state(&info, state)
        || ai_runtime_decode_model(lifecycle->package_scratch.model_bytes,
                                   info.model_length,
                                   &lifecycle->slot_model_storage[slot_index],
                                   &lifecycle->slot_models[slot_index]) != RT_EOK)
    {
        return 0;
    }
    lifecycle->active_slot = state->slot;
    lifecycle->active_generation = state->generation;
    lifecycle->active_info = info;
    lifecycle->active_valid = 1U;
    return 1;
}

static int model_download_erase(void *context, uint32_t offset,
                                uint32_t length)
{
    ota_model_lifecycle_t *lifecycle = context;

    if (lifecycle == NULL || lifecycle->target_slot == OTA_MODEL_SLOT_INVALID)
    {
        return -1;
    }
    return lifecycle->storage.erase_slot(lifecycle->storage.context,
                                         lifecycle->target_slot, offset, length);
}

static int model_download_write(void *context, uint32_t offset,
                                const uint8_t *data, uint32_t length)
{
    ota_model_lifecycle_t *lifecycle = context;

    if (lifecycle == NULL || lifecycle->target_slot == OTA_MODEL_SLOT_INVALID)
    {
        return -1;
    }
    return lifecycle->storage.write_slot(lifecycle->storage.context,
                                         lifecycle->target_slot, offset, data,
                                         length);
}

static int model_download_read(void *context, uint32_t offset, uint8_t *data,
                               uint32_t length)
{
    ota_model_lifecycle_t *lifecycle = context;

    if (lifecycle == NULL || lifecycle->target_slot == OTA_MODEL_SLOT_INVALID)
    {
        return -1;
    }
    return lifecycle->storage.read_slot(lifecycle->storage.context,
                                        lifecycle->target_slot, offset, data,
                                        length);
}

static int model_download_validate(void *context)
{
    ota_model_lifecycle_t *lifecycle = context;
    package_reader_context_t reader;

    if (lifecycle == NULL || lifecycle->target_slot == OTA_MODEL_SLOT_INVALID)
    {
        return -1;
    }
    reader.lifecycle = lifecycle;
    reader.slot = lifecycle->target_slot;
    lifecycle->last_package_status = (uint8_t)ota_model_package_validate(
        package_reader, &reader, lifecycle->download.total_bytes,
        &lifecycle->package_scratch, &lifecycle->pending_info);
    return lifecycle->last_package_status == OTA_MODEL_PACKAGE_STATUS_OK ? 0 : -1;
}

static int state_record_matches(const uint8_t *record,
                                const model_state_view_t *state)
{
    model_state_view_t decoded;

    return decode_state_record(record, &decoded)
           && decoded.generation == state->generation
           && decoded.slot == state->slot
           && package_matches_state(&decoded.info, state);
}

static void build_state_record(uint8_t *record,
                               const model_state_view_t *state)
{
    memset(record, 0, OTA_MODEL_STATE_RECORD_BYTES);
    put_u32_le(&record[MODEL_STATE_OFFSET_MAGIC], OTA_MODEL_STATE_MAGIC);
    put_u16_le(&record[MODEL_STATE_OFFSET_FORMAT_VERSION],
               OTA_MODEL_STATE_FORMAT_VERSION);
    put_u16_le(&record[MODEL_STATE_OFFSET_HEADER_BYTES],
               OTA_MODEL_STATE_RECORD_BYTES);
    put_u32_le(&record[MODEL_STATE_OFFSET_GENERATION], state->generation);
    record[MODEL_STATE_OFFSET_ACTIVE_SLOT] = (uint8_t)state->slot;
    put_u16_le(&record[MODEL_STATE_OFFSET_MODEL_VERSION],
               state->info.model_version);
    put_u32_le(&record[MODEL_STATE_OFFSET_PACKAGE_LENGTH],
               state->info.package_length);
    put_u32_le(&record[MODEL_STATE_OFFSET_MODEL_LENGTH],
               state->info.model_length);
    put_u32_le(&record[MODEL_STATE_OFFSET_MODEL_PAYLOAD_CRC32],
               state->info.model_payload_crc32);
    memcpy(&record[MODEL_STATE_OFFSET_MODEL_SHA256], state->info.model_sha256,
           sizeof(state->info.model_sha256));
    memcpy(&record[MODEL_STATE_OFFSET_GOLDEN_INPUT_SHA256],
           state->info.golden_input_sha256,
           sizeof(state->info.golden_input_sha256));
    memcpy(&record[MODEL_STATE_OFFSET_GOLDEN_OUTPUT_SHA256],
           state->info.golden_output_sha256,
           sizeof(state->info.golden_output_sha256));
    put_u32_le(&record[MODEL_STATE_OFFSET_RECORD_CRC32],
               ota_crc32_compute(record, MODEL_STATE_OFFSET_RECORD_CRC32));
    put_u32_le(&record[MODEL_STATE_OFFSET_COMMIT], OTA_MODEL_STATE_COMMIT_MARKER);
}

static int state_record_fits(uint32_t capacity, uint32_t offset)
{
    return offset <= capacity
           && OTA_MODEL_STATE_RECORD_BYTES <= capacity - offset;
}

static void refresh_state_cursor(ota_model_lifecycle_t *lifecycle)
{
    const uint8_t state_bank_count =
        model_state_bank_count(lifecycle == NULL ? NULL : &lifecycle->storage);
    uint8_t bank;

    if (lifecycle == NULL)
    {
        return;
    }
    if (lifecycle->state_bank >= state_bank_count)
    {
        lifecycle->state_bank = 0U;
    }
    if (state_record_fits(
            lifecycle->storage.state_capacity_bytes,
            lifecycle->state_bank_next_offset[lifecycle->state_bank]))
    {
        lifecycle->next_state_offset =
            lifecycle->state_bank_next_offset[lifecycle->state_bank];
        lifecycle->state_full = 0U;
        return;
    }
    for (bank = 0U; bank < state_bank_count; bank++)
    {
        if (state_record_fits(lifecycle->storage.state_capacity_bytes,
                              lifecycle->state_bank_next_offset[bank]))
        {
            lifecycle->state_bank = bank;
            lifecycle->next_state_offset =
                lifecycle->state_bank_next_offset[bank];
            lifecycle->state_full = 0U;
            return;
        }
    }
    lifecycle->next_state_offset = lifecycle->storage.state_capacity_bytes;
    lifecycle->state_full = 1U;
}

static void consume_state_record(ota_model_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL)
    {
        return;
    }
    if (state_record_fits(lifecycle->storage.state_capacity_bytes,
                          lifecycle->next_state_offset))
    {
        lifecycle->next_state_offset += OTA_MODEL_STATE_RECORD_BYTES;
        lifecycle->state_bank_next_offset[lifecycle->state_bank] =
            lifecycle->next_state_offset;
    }
    refresh_state_cursor(lifecycle);
}

static int compact_state_bank(ota_model_lifecycle_t *lifecycle,
                              uint8_t target_bank)
{
    uint8_t record[OTA_MODEL_STATE_RECORD_BYTES];
    uint8_t readback[OTA_MODEL_STATE_RECORD_BYTES];
    model_state_view_t state;

    if (lifecycle == NULL || !lifecycle->active_valid
        || lifecycle->active_slot == OTA_MODEL_SLOT_INVALID
        || lifecycle->active_generation == UINT32_MAX)
    {
        return lifecycle != NULL && !lifecycle->active_valid ? 0 : -1;
    }
    memset(&state, 0, sizeof(state));
    state.generation = lifecycle->active_generation + 1U;
    state.slot = lifecycle->active_slot;
    state.info = lifecycle->active_info;
    build_state_record(record, &state);
    (void)model_state_write(lifecycle, target_bank, 0U, record,
                            MODEL_STATE_OFFSET_COMMIT);
    if (model_state_read(lifecycle, target_bank, 0U, readback,
                         MODEL_STATE_OFFSET_COMMIT) != 0
        || memcmp(readback, record, MODEL_STATE_OFFSET_COMMIT) != 0)
    {
        return -1;
    }
    (void)model_state_write(lifecycle, target_bank,
                            MODEL_STATE_OFFSET_COMMIT,
                            &record[MODEL_STATE_OFFSET_COMMIT],
                            MODEL_STATE_COMMIT_BYTES);
    if (model_state_read(lifecycle, target_bank, 0U, readback,
                         sizeof(readback)) != 0
        || !state_record_matches(readback, &state))
    {
        return -1;
    }
    lifecycle->active_generation = state.generation;
    lifecycle->authoritative_state_bank = target_bank;
    lifecycle->authoritative_state_valid = 1U;
    lifecycle->state_bank_next_offset[target_bank] =
        OTA_MODEL_STATE_RECORD_BYTES;
    return 0;
}

static int ensure_state_capacity(ota_model_lifecycle_t *lifecycle)
{
    const uint8_t state_bank_count =
        model_state_bank_count(lifecycle == NULL ? NULL : &lifecycle->storage);
    const uint8_t target_bank = lifecycle == NULL
                                    ? 0U
                                    : (uint8_t)(((lifecycle->authoritative_state_valid
                                                     ? lifecycle->authoritative_state_bank
                                                     : lifecycle->state_bank)
                                                   + 1U)
                                                  % state_bank_count);

    if (lifecycle == NULL || !lifecycle->state_full)
    {
        return 0;
    }
    if (state_bank_count < 2U
        || model_state_erase(lifecycle, target_bank, 0U,
                             lifecycle->storage.state_capacity_bytes) != 0)
    {
        return -1;
    }
    lifecycle->state_bank_next_offset[target_bank] = 0U;
    if (compact_state_bank(lifecycle, target_bank) != 0)
    {
        /* The old bank is still authoritative; force a reboot/remount before
         * attempting another state mutation after an ambiguous compaction. */
        lifecycle->state_full = 1U;
        return -1;
    }
    lifecycle->state_bank = target_bank;
    refresh_state_cursor(lifecycle);
    return lifecycle->state_full ? -1 : 0;
}

static void mark_activation_uncertain(ota_model_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL)
    {
        return;
    }
    lifecycle->activation_uncertain = 1U;
    lifecycle->active_valid = 0U;
    lifecycle->active_slot = OTA_MODEL_SLOT_INVALID;
    if (lifecycle->quarantine_runtime_model != NULL)
    {
        lifecycle->quarantine_runtime_model(
            lifecycle->runtime_activation_context);
    }
    else if (lifecycle->abort_runtime_model != NULL)
    {
        /* Keep the transition gate from trapping the worker if an older
         * integration supplied only the original abort callback. */
        lifecycle->abort_runtime_model(lifecycle->runtime_activation_context);
    }
}

static int write_activation_state(ota_model_lifecycle_t *lifecycle)
{
    uint8_t record[OTA_MODEL_STATE_RECORD_BYTES];
    uint8_t readback[OTA_MODEL_STATE_RECORD_BYTES];
    model_state_view_t state;
    uint32_t record_offset;
    uint32_t state_record_count;
    int body_write_result;
    int body_readback_result;
    int commit_write_result;
    int readback_result;
    const int slot_index = model_slot_index(
        lifecycle == NULL ? OTA_MODEL_SLOT_INVALID : lifecycle->target_slot);
    rt_bool_t runtime_prepared = RT_FALSE;

    if (lifecycle == NULL)
    {
        return -1;
    }
    if (!state_record_fits(lifecycle->storage.state_capacity_bytes,
                           lifecycle->next_state_offset))
    {
        lifecycle->state_full = 1U;
        return -1;
    }
    if (lifecycle->active_generation == UINT32_MAX)
    {
        return -1;
    }
    record_offset = lifecycle->next_state_offset;
    state_record_count = lifecycle->storage.state_capacity_bytes
                         / OTA_MODEL_STATE_RECORD_BYTES;
    if (record_offset / OTA_MODEL_STATE_RECORD_BYTES >= state_record_count
        || !state_record_fits(lifecycle->storage.state_capacity_bytes,
                              record_offset))
    {
        lifecycle->state_full = 1U;
        return -1;
    }
    if (slot_index < 0
        || ai_runtime_decode_model(
               lifecycle->package_scratch.model_bytes,
               lifecycle->pending_info.model_length,
               &lifecycle->slot_model_storage[slot_index],
               &lifecycle->slot_models[slot_index]) != RT_EOK)
    {
        return -1;
    }
    memset(&state, 0, sizeof(state));
    state.generation = lifecycle->active_generation + 1U;
    state.slot = lifecycle->target_slot;
    state.info = lifecycle->pending_info;
    build_state_record(record, &state);
    body_write_result = model_state_write(
        lifecycle, lifecycle->state_bank, record_offset, record,
        MODEL_STATE_OFFSET_COMMIT);
    body_readback_result = model_state_read(
        lifecycle, lifecycle->state_bank, record_offset, readback,
        MODEL_STATE_OFFSET_COMMIT);
    /* The body is intentionally uncommitted while the live runtime switches.
     * A reset before the commit marker therefore leaves the previous record
     * authoritative during the next mount. */
    (void)body_write_result;
    if (body_readback_result != 0
        || memcmp(readback, record, MODEL_STATE_OFFSET_COMMIT) != 0)
    {
        consume_state_record(lifecycle);
        return -1;
    }
    if (lifecycle->prepare_runtime_model != NULL
        && lifecycle->prepare_runtime_model(
               &lifecycle->slot_models[slot_index],
               lifecycle->runtime_activation_context) != 0)
    {
        /* A failed prepare must be side-effect-free. Calling abort here
         * could release a transition gate owned by another caller. */
        consume_state_record(lifecycle);
        return -1;
    }
    runtime_prepared = lifecycle->prepare_runtime_model != NULL
                           ? RT_TRUE
                           : RT_FALSE;
    commit_write_result = model_state_write(
        lifecycle, lifecycle->state_bank,
        record_offset + MODEL_STATE_OFFSET_COMMIT,
        &record[MODEL_STATE_OFFSET_COMMIT], MODEL_STATE_COMMIT_BYTES);
    readback_result = model_state_read(lifecycle, lifecycle->state_bank,
                                       record_offset, readback, sizeof(readback));
    /* A QSPI callback may report an error after the device completed the write;
     * the final readback is the authoritative commit result. */
    (void)commit_write_result;
    if (readback_result != 0)
    {
        consume_state_record(lifecycle);
        mark_activation_uncertain(lifecycle);
        return -1;
    }
    if (!state_record_matches(readback, &state))
    {
        consume_state_record(lifecycle);
        mark_activation_uncertain(lifecycle);
        return -1;
    }
    if (runtime_prepared)
    {
        if (lifecycle->publish_runtime_model(
                &lifecycle->slot_models[slot_index],
                lifecycle->runtime_activation_context)
            != 0)
        {
            consume_state_record(lifecycle);
            mark_activation_uncertain(lifecycle);
            return -1;
        }
    }
    lifecycle->active_slot = lifecycle->target_slot;
    lifecycle->active_generation = state.generation;
    lifecycle->authoritative_state_bank = lifecycle->state_bank;
    lifecycle->authoritative_state_valid = 1U;
    lifecycle->active_info = lifecycle->pending_info;
    lifecycle->active_valid = 1U;
    consume_state_record(lifecycle);
    return 0;
}

static int model_download_commit(void *context)
{
    return write_activation_state(context);
}

bool ota_model_lifecycle_init(ota_model_lifecycle_t *lifecycle,
                              const ota_model_storage_t *storage)
{
    uint8_t record[OTA_MODEL_STATE_RECORD_BYTES];
    model_state_view_t state;
    uint32_t offset;
    uint32_t record_count;
    uint8_t bank;
    uint8_t best_bank = 0U;
    uint32_t best_generation = 0U;
    uint8_t found_active = 0U;
    const uint8_t state_bank_count =
        model_state_bank_count(storage);

    if (lifecycle == NULL || !model_storage_is_valid(storage))
    {
        return false;
    }
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->storage = *storage;
    lifecycle->active_slot = OTA_MODEL_SLOT_INVALID;
    lifecycle->target_slot = OTA_MODEL_SLOT_INVALID;
    lifecycle->last_package_status = OTA_MODEL_PACKAGE_STATUS_INVALID_ARGUMENT;
    lifecycle->download_storage.erase = model_download_erase;
    lifecycle->download_storage.write = model_download_write;
    lifecycle->download_storage.read = model_download_read;
    lifecycle->download_storage.validate = model_download_validate;
    lifecycle->download_storage.mark_pending_install = model_download_commit;
    lifecycle->download_storage.context = lifecycle;
    lifecycle->download_storage.capacity_bytes = storage->slot_capacity_bytes;
    lifecycle->download_storage.erase_block_bytes = storage->erase_block_bytes;
    ota_download_service_init(&lifecycle->download, &lifecycle->download_storage);

    record_count = storage->state_capacity_bytes / OTA_MODEL_STATE_RECORD_BYTES;
    for (bank = 0U; bank < state_bank_count; bank++)
    {
        lifecycle->state_bank_next_offset[bank] =
            storage->state_capacity_bytes;
        for (offset = 0U; offset < record_count * OTA_MODEL_STATE_RECORD_BYTES;
             offset += OTA_MODEL_STATE_RECORD_BYTES)
        {
            if (model_state_read(lifecycle, bank, offset, record,
                                 sizeof(record)) != 0)
            {
                return false;
            }
            if (record_is_erased(record))
            {
                if (lifecycle->state_bank_next_offset[bank]
                    == storage->state_capacity_bytes)
                {
                    lifecycle->state_bank_next_offset[bank] = offset;
                }
                continue;
            }
            if (!decode_state_record(record, &state)
                || state.info.package_length > storage->slot_capacity_bytes)
            {
                continue;
            }
            if (state.generation >= best_generation
                && select_active_model(lifecycle, &state))
            {
                best_generation = state.generation;
                best_bank = bank;
                found_active = 1U;
            }
        }
    }
    lifecycle->active_generation = best_generation;
    lifecycle->state_bank = found_active ? best_bank : 0U;
    lifecycle->authoritative_state_bank = found_active ? best_bank : 0U;
    lifecycle->authoritative_state_valid = found_active;
    lifecycle->mounted = 1U;
    if (!lifecycle->active_valid)
    {
        lifecycle->active_slot = OTA_MODEL_SLOT_INVALID;
        lifecycle->active_generation = 0U;
    }
    refresh_state_cursor(lifecycle);
    return true;
}

ota_download_status_t ota_model_lifecycle_begin(ota_model_lifecycle_t *lifecycle,
                                                uint32_t package_length)
{
    if (lifecycle == NULL || !lifecycle->mounted
        || lifecycle->activation_uncertain != 0U)
    {
        return lifecycle != NULL && lifecycle->activation_uncertain != 0U
                   ? OTA_DOWNLOAD_STATUS_STORAGE_ERROR
                   : OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (package_length < OTA_MODEL_PACKAGE_HEADER_BYTES
        || package_length > OTA_MODEL_PACKAGE_MAX_BYTES)
    {
        return OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    if (ensure_state_capacity(lifecycle) != 0)
    {
        return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
    }
    if (lifecycle->active_slot == OTA_MODEL_SLOT_A)
    {
        lifecycle->target_slot = OTA_MODEL_SLOT_B;
    }
    else if (lifecycle->active_slot == OTA_MODEL_SLOT_B)
    {
        lifecycle->target_slot = OTA_MODEL_SLOT_A;
    }
    else
    {
        lifecycle->target_slot = OTA_MODEL_SLOT_A;
    }
    return ota_download_begin(&lifecycle->download, package_length);
}

ota_download_status_t ota_model_lifecycle_write(ota_model_lifecycle_t *lifecycle,
                                                uint32_t offset,
                                                const uint8_t *data,
                                                uint32_t length,
                                                uint32_t crc32)
{
    if (lifecycle == NULL || lifecycle->activation_uncertain != 0U)
    {
        return lifecycle != NULL && lifecycle->activation_uncertain != 0U
                   ? OTA_DOWNLOAD_STATUS_STORAGE_ERROR
                   : OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    return ota_download_write(&lifecycle->download, offset, data, length, crc32);
}

void ota_model_lifecycle_query(const ota_model_lifecycle_t *lifecycle,
                               ota_download_progress_t *progress)
{
    ota_download_query(lifecycle == NULL ? NULL : &lifecycle->download, progress);
}

ota_download_status_t ota_model_lifecycle_finalize(
    ota_model_lifecycle_t *lifecycle)
{
    ota_download_status_t status;

    if (lifecycle == NULL || lifecycle->activation_uncertain != 0U)
    {
        return lifecycle != NULL && lifecycle->activation_uncertain != 0U
                   ? OTA_DOWNLOAD_STATUS_STORAGE_ERROR
                   : OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    status = ota_download_finalize(&lifecycle->download);
    if (status == OTA_DOWNLOAD_STATUS_INVALID_PACKAGE
        && lifecycle->last_package_status == OTA_MODEL_PACKAGE_STATUS_STORAGE_ERROR)
    {
        return OTA_DOWNLOAD_STATUS_STORAGE_ERROR;
    }
    return status;
}

ota_download_status_t ota_model_lifecycle_cancel(
    ota_model_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL || lifecycle->activation_uncertain != 0U
        || lifecycle->download.pending_install != 0U
        || lifecycle->target_slot == lifecycle->active_slot)
    {
        return lifecycle != NULL && lifecycle->activation_uncertain != 0U
                   ? OTA_DOWNLOAD_STATUS_STORAGE_ERROR
                   : OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT;
    }
    return ota_download_cancel(&lifecycle->download);
}

ota_model_slot_t ota_model_lifecycle_get_active_slot(
    const ota_model_lifecycle_t *lifecycle)
{
    return lifecycle == NULL ? OTA_MODEL_SLOT_INVALID : lifecycle->active_slot;
}

ota_model_slot_t ota_model_lifecycle_get_target_slot(
    const ota_model_lifecycle_t *lifecycle)
{
    return lifecycle == NULL ? OTA_MODEL_SLOT_INVALID : lifecycle->target_slot;
}

bool ota_model_lifecycle_has_valid_model(
    const ota_model_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->activation_uncertain == 0U
           && lifecycle->active_valid != 0U;
}

bool ota_model_lifecycle_is_activation_uncertain(
    const ota_model_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->activation_uncertain != 0U;
}

void ota_model_lifecycle_set_runtime_activation(
    ota_model_lifecycle_t *lifecycle,
    ota_model_runtime_prepare_fn prepare_runtime_model,
    ota_model_runtime_publish_fn publish_runtime_model,
    ota_model_runtime_abort_fn abort_runtime_model,
    ota_model_runtime_quarantine_fn quarantine_runtime_model,
    void *context)
{
    if (lifecycle == NULL
        || (prepare_runtime_model == NULL) != (publish_runtime_model == NULL)
        || (prepare_runtime_model == NULL) != (abort_runtime_model == NULL)
        || (prepare_runtime_model == NULL)
               != (quarantine_runtime_model == NULL))
    {
        return;
    }
    lifecycle->prepare_runtime_model = prepare_runtime_model;
    lifecycle->publish_runtime_model = publish_runtime_model;
    lifecycle->abort_runtime_model = abort_runtime_model;
    lifecycle->quarantine_runtime_model = quarantine_runtime_model;
    lifecycle->runtime_activation_context = context;
}

const ai_model_t *ota_model_lifecycle_get_active_model(
    const ota_model_lifecycle_t *lifecycle)
{
    const int slot_index = model_slot_index(
        lifecycle == NULL ? OTA_MODEL_SLOT_INVALID : lifecycle->active_slot);

    return ota_model_lifecycle_has_valid_model(lifecycle) && slot_index >= 0
               ? &lifecycle->slot_models[slot_index]
               : RT_NULL;
}

const ai_model_t *ota_model_lifecycle_get_runtime_model(
    const ota_model_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL || lifecycle->activation_uncertain != 0U)
    {
        return RT_NULL;
    }
    const ai_model_t *active = ota_model_lifecycle_get_active_model(lifecycle);

    return active != RT_NULL ? active : &transport_ai_model_v1;
}

const ota_model_package_info_t *ota_model_lifecycle_get_active_info(
    const ota_model_lifecycle_t *lifecycle)
{
    return ota_model_lifecycle_has_valid_model(lifecycle)
               ? &lifecycle->active_info
               : RT_NULL;
}
