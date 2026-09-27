#include "ai_result_sidecar.h"

#include <math.h>
#include <string.h>

#include "ota_crc32.h"

#define AI_RESULT_SIDECAR_MAGIC UINT32_C(0x53524941) /* "AIRS" */
#define AI_RESULT_SIDECAR_OFFSET_MAGIC 0U
#define AI_RESULT_SIDECAR_OFFSET_VERSION 4U
#define AI_RESULT_SIDECAR_OFFSET_HEADER_BYTES 6U
#define AI_RESULT_SIDECAR_OFFSET_SEQUENCE 8U
#define AI_RESULT_SIDECAR_OFFSET_EVENT_ID 12U
#define AI_RESULT_SIDECAR_OFFSET_MODEL_VERSION 16U
#define AI_RESULT_SIDECAR_OFFSET_STATUS 18U
#define AI_RESULT_SIDECAR_OFFSET_CLASS_INDEX 19U
#define AI_RESULT_SIDECAR_OFFSET_CLASS_COUNT 20U
#define AI_RESULT_SIDECAR_OFFSET_QUALITY_FLAGS 21U
#define AI_RESULT_SIDECAR_OFFSET_EVENT_FLAGS 22U
#define AI_RESULT_SIDECAR_OFFSET_SAMPLE_COUNT 24U
#define AI_RESULT_SIDECAR_OFFSET_MODEL_CRC32 28U
#define AI_RESULT_SIDECAR_OFFSET_CONFIDENCE 32U
#define AI_RESULT_SIDECAR_OFFSET_LOGITS 36U
#define AI_RESULT_SIDECAR_OFFSET_FAILURE_REASON 52U
#define AI_RESULT_SIDECAR_OFFSET_WRITE_GENERATION 54U
#define AI_RESULT_SIDECAR_OFFSET_RECORD_CRC32 60U
#define AI_RESULT_SIDECAR_OFFSET_COMMIT 64U

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void write_float_le(uint8_t *data, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    write_u32_le(data, bits);
}

static float read_float_le(const uint8_t *data)
{
    uint32_t bits = read_u32_le(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int result_is_valid(const ai_result_t *result)
{
    uint8_t index;

    if (result == NULL || result->event_id == 0U
        || result->status > AI_RESULT_STATUS_RESOURCE_LIMIT
        || result->class_count > AI_MODEL_MAX_CLASSES
        || (result->status == AI_RESULT_STATUS_PREDICTION
            && (result->class_count < 2U
                || result->class_index >= result->class_count))
        || !isfinite(result->confidence))
    {
        return 0;
    }
    for (index = 0U; index < AI_MODEL_MAX_CLASSES; index++)
    {
        if (!isfinite(result->logits[index]))
        {
            return 0;
        }
    }
    return 1;
}

static int storage_is_valid(const ai_result_sidecar_storage_t *storage)
{
    return storage != NULL && storage->read != NULL && storage->write != NULL
           && storage->erase != NULL
           && storage->capacity_bytes >= AI_RESULT_SIDECAR_RECORD_BYTES
           && storage->erase_sector_bytes != 0U
           && storage->capacity_bytes % AI_RESULT_SIDECAR_RECORD_BYTES == 0U
           && storage->capacity_bytes % storage->erase_sector_bytes == 0U
           && storage->erase_sector_bytes <= storage->capacity_bytes
           && storage->erase_sector_bytes % AI_RESULT_SIDECAR_RECORD_BYTES == 0U
           && storage->capacity_bytes / storage->erase_sector_bytes >= 3U;
}

static int record_is_erased(const uint8_t *encoded)
{
    uint32_t index;

    for (index = 0U; index < AI_RESULT_SIDECAR_RECORD_BYTES; index++)
    {
        if (encoded[index] != 0xFFU)
        {
            return 0;
        }
    }
    return 1;
}

static int decode_record(const uint8_t *encoded,
                         ai_result_t *result,
                         uint32_t *write_generation)
{
    ai_result_t decoded = {0};
    uint32_t decoded_generation;
    uint32_t index;

    if (encoded == NULL || result == NULL
        || read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_MAGIC])
           != AI_RESULT_SIDECAR_MAGIC
        || read_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_VERSION])
           != AI_RESULT_SIDECAR_FORMAT_VERSION
        || read_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_HEADER_BYTES])
           != AI_RESULT_SIDECAR_HEADER_BYTES
        || read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_COMMIT])
           != AI_RESULT_SIDECAR_COMMIT_MARKER
        || ota_crc32_compute(encoded, AI_RESULT_SIDECAR_HEADER_BYTES)
           != read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_RECORD_CRC32]))
    {
        return 0;
    }

    decoded.result_sequence = read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_SEQUENCE]);
    decoded.event_id = read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_EVENT_ID]);
    decoded.model_version = read_u16_le(
        &encoded[AI_RESULT_SIDECAR_OFFSET_MODEL_VERSION]);
    decoded.status = encoded[AI_RESULT_SIDECAR_OFFSET_STATUS];
    decoded.class_index = encoded[AI_RESULT_SIDECAR_OFFSET_CLASS_INDEX];
    decoded.class_count = encoded[AI_RESULT_SIDECAR_OFFSET_CLASS_COUNT];
    decoded.quality_flags = encoded[AI_RESULT_SIDECAR_OFFSET_QUALITY_FLAGS];
    decoded.event_flags = read_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_EVENT_FLAGS]);
    decoded.sample_count = read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_SAMPLE_COUNT]);
    decoded.model_crc32 = read_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_MODEL_CRC32]);
    decoded.confidence = read_float_le(&encoded[AI_RESULT_SIDECAR_OFFSET_CONFIDENCE]);
    for (index = 0U; index < AI_MODEL_MAX_CLASSES; index++)
    {
        decoded.logits[index] = read_float_le(
            &encoded[AI_RESULT_SIDECAR_OFFSET_LOGITS + index * sizeof(float)]);
    }
    decoded.failure_reason = read_u16_le(
        &encoded[AI_RESULT_SIDECAR_OFFSET_FAILURE_REASON]);
    decoded_generation = read_u32_le(
        &encoded[AI_RESULT_SIDECAR_OFFSET_WRITE_GENERATION]);
    if (!result_is_valid(&decoded) || decoded.result_sequence == 0U
        || decoded_generation == 0U)
    {
        return 0;
    }
    *result = decoded;
    if (write_generation != NULL)
    {
        *write_generation = decoded_generation;
    }
    return 1;
}

static void encode_record(const ai_result_t *result,
                          uint32_t sequence,
                          uint32_t write_generation,
                          uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES])
{
    uint32_t index;

    memset(encoded, 0xFF, AI_RESULT_SIDECAR_RECORD_BYTES);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_MAGIC], AI_RESULT_SIDECAR_MAGIC);
    write_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_VERSION],
                 AI_RESULT_SIDECAR_FORMAT_VERSION);
    write_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_HEADER_BYTES],
                 AI_RESULT_SIDECAR_HEADER_BYTES);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_SEQUENCE], sequence);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_EVENT_ID], result->event_id);
    write_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_MODEL_VERSION],
                 result->model_version);
    encoded[AI_RESULT_SIDECAR_OFFSET_STATUS] = result->status;
    encoded[AI_RESULT_SIDECAR_OFFSET_CLASS_INDEX] = result->class_index;
    encoded[AI_RESULT_SIDECAR_OFFSET_CLASS_COUNT] = result->class_count;
    encoded[AI_RESULT_SIDECAR_OFFSET_QUALITY_FLAGS] = result->quality_flags;
    write_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_EVENT_FLAGS], result->event_flags);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_SAMPLE_COUNT],
                 result->sample_count);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_MODEL_CRC32],
                 result->model_crc32);
    write_float_le(&encoded[AI_RESULT_SIDECAR_OFFSET_CONFIDENCE],
                   result->confidence);
    for (index = 0U; index < AI_MODEL_MAX_CLASSES; index++)
    {
        write_float_le(&encoded[AI_RESULT_SIDECAR_OFFSET_LOGITS
                                + index * sizeof(float)], result->logits[index]);
    }
    write_u16_le(&encoded[AI_RESULT_SIDECAR_OFFSET_FAILURE_REASON],
                 result->failure_reason);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_WRITE_GENERATION],
                 write_generation);
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_RECORD_CRC32],
                 ota_crc32_compute(encoded, AI_RESULT_SIDECAR_HEADER_BYTES));
    write_u32_le(&encoded[AI_RESULT_SIDECAR_OFFSET_COMMIT],
                 AI_RESULT_SIDECAR_COMMIT_MARKER);
}

static int read_slot(const ai_result_sidecar_t *sidecar,
                     uint32_t slot,
                     uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES])
{
    uint32_t offset;

    if (sidecar == NULL || slot >= sidecar->slot_count)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    offset = slot * AI_RESULT_SIDECAR_RECORD_BYTES;
    return sidecar->storage.read(sidecar->storage.context, offset, encoded,
                                 AI_RESULT_SIDECAR_RECORD_BYTES);
}

static int slot_is_erased(const ai_result_sidecar_t *sidecar, uint32_t slot)
{
    uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES];

    if (read_slot(sidecar, slot, encoded) != 0)
    {
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    return record_is_erased(encoded) ? 1 : 0;
}

static int sector_contains_slot(const ai_result_sidecar_t *sidecar,
                                uint32_t sector_start,
                                uint32_t slot)
{
    return slot != UINT32_MAX && slot >= sector_start
           && slot < sector_start + sidecar->slots_per_sector;
}

static int write_record(const ai_result_sidecar_t *sidecar,
                        uint32_t slot,
                        const ai_result_t *result,
                        uint32_t sequence,
                        uint32_t write_generation,
                        ai_result_t *committed)
{
    uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES];
    uint32_t committed_generation;
    int write_status = AI_RESULT_SIDECAR_OK;

    if (sidecar == NULL || result == NULL || committed == NULL
        || slot >= sidecar->slot_count)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    encode_record(result, sequence, write_generation, encoded);
    if (sidecar->storage.write(sidecar->storage.context,
                               slot * AI_RESULT_SIDECAR_RECORD_BYTES,
                               encoded, AI_RESULT_SIDECAR_OFFSET_COMMIT) != 0)
    {
        write_status = AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    else if (sidecar->storage.write(
                 sidecar->storage.context,
                 slot * AI_RESULT_SIDECAR_RECORD_BYTES
                     + AI_RESULT_SIDECAR_OFFSET_COMMIT,
                 &encoded[AI_RESULT_SIDECAR_OFFSET_COMMIT], sizeof(uint32_t))
             != 0)
    {
        write_status = AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    if (read_slot(sidecar, slot, encoded) != 0)
    {
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    if (!decode_record(encoded, committed, &committed_generation)
        || committed_generation != write_generation
        || committed->result_sequence != sequence
        || committed->event_id != result->event_id)
    {
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    /* A transport-level write error is recoverable when readback proves that
     * the commit marker and CRC reached the NOR device. */
    (void)write_status;
    return AI_RESULT_SIDECAR_OK;
}

static int find_erased_slot(const ai_result_sidecar_t *sidecar,
                            uint32_t start_slot,
                            uint32_t *slot)
{
    uint32_t offset;

    if (sidecar == NULL || slot == NULL || sidecar->slot_count == 0U)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    start_slot %= sidecar->slot_count;
    for (offset = 0U; offset < sidecar->slot_count; offset++)
    {
        uint32_t candidate = (start_slot + offset) % sidecar->slot_count;
        int erased = slot_is_erased(sidecar, candidate);

        if (erased < 0)
        {
            return AI_RESULT_SIDECAR_STORAGE_ERROR;
        }
        if (erased)
        {
            *slot = candidate;
            return AI_RESULT_SIDECAR_OK;
        }
    }
    return 1;
}

int ai_result_sidecar_mount(ai_result_sidecar_t *sidecar,
                            const ai_result_sidecar_storage_t *storage)
{
    uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES];
    ai_result_t candidate;
    uint32_t candidate_generation;
    uint32_t slot;
    uint32_t first_erased = UINT32_MAX;
    uint32_t highest_sequence = 0U;
    uint32_t highest_generation = 0U;
    uint32_t latest_physical_slot = UINT32_MAX;
    uint32_t latest_result_slot = UINT32_MAX;
    int have_valid_record = 0;

    if (sidecar == NULL || !storage_is_valid(storage))
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    memset(sidecar, 0, sizeof(*sidecar));
    sidecar->storage = *storage;
    sidecar->slot_count = storage->capacity_bytes / AI_RESULT_SIDECAR_RECORD_BYTES;
    sidecar->slots_per_sector = storage->erase_sector_bytes
                                / AI_RESULT_SIDECAR_RECORD_BYTES;
    sidecar->latest_write_slot = UINT32_MAX;
    sidecar->latest_result_slot = UINT32_MAX;
    for (slot = 0U; slot < sidecar->slot_count; slot++)
    {
        if (read_slot(sidecar, slot, encoded) != 0)
        {
            return AI_RESULT_SIDECAR_STORAGE_ERROR;
        }
        if (record_is_erased(encoded))
        {
            if (first_erased == UINT32_MAX)
            {
                first_erased = slot;
            }
            continue;
        }
        if (!decode_record(encoded, &candidate, &candidate_generation))
        {
            continue;
        }
        have_valid_record = 1;
        if (candidate_generation > highest_generation)
        {
            highest_generation = candidate_generation;
            latest_physical_slot = slot;
            sidecar->latest_write_generation = candidate_generation;
            sidecar->latest_write_slot = slot;
        }
        if (!sidecar->latest_valid
            || candidate.result_sequence >= highest_sequence)
        {
            highest_sequence = candidate.result_sequence;
            latest_result_slot = slot;
            sidecar->latest = candidate;
            sidecar->latest_valid = 1;
        }
    }
    sidecar->latest_result_slot = latest_result_slot;
    sidecar->next_slot = !have_valid_record
                         ? (first_erased == UINT32_MAX ? 0U : first_erased)
                         : (latest_physical_slot + 1U) % sidecar->slot_count;
    sidecar->next_sequence = highest_sequence == UINT32_MAX
                             ? 0U : highest_sequence + 1U;
    sidecar->next_generation = highest_generation == UINT32_MAX
                               ? 0U : highest_generation + 1U;
    if (sidecar->next_sequence == 0U || sidecar->next_generation == 0U)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    return AI_RESULT_SIDECAR_OK;
}

int ai_result_sidecar_append(ai_result_sidecar_t *sidecar,
                             const ai_result_t *result)
{
    uint32_t sequence;
    uint32_t write_generation;
    uint32_t start_slot;
    uint32_t sector_start;
    uint32_t slot;
    uint32_t sector_count;
    uint32_t start_sector;
    uint32_t offset;
    ai_result_t committed;
    int find_status;

    if (sidecar == NULL || !result_is_valid(result)
        || sidecar->next_sequence == 0U || sidecar->next_generation == 0U
        || sidecar->slot_count == 0U || sidecar->slots_per_sector == 0U)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    sequence = sidecar->next_sequence;
    write_generation = sidecar->next_generation;
    if (result->result_sequence != 0U)
    {
        sequence = result->result_sequence;
    }
    start_slot = sidecar->next_slot % sidecar->slot_count;
    find_status = find_erased_slot(sidecar, start_slot, &slot);
    if (find_status < 0)
    {
        return find_status;
    }
    if (find_status > 0)
    {
        sector_count = sidecar->slot_count / sidecar->slots_per_sector;
        start_sector = start_slot / sidecar->slots_per_sector;
        sector_start = UINT32_MAX;
        for (offset = 0U; offset < sector_count; offset++)
        {
            uint32_t candidate_sector = (start_sector + offset) % sector_count;
            uint32_t candidate_start = candidate_sector
                                        * sidecar->slots_per_sector;

            if (!sector_contains_slot(sidecar, candidate_start,
                                       sidecar->latest_write_slot)
                && !sector_contains_slot(sidecar, candidate_start,
                                         sidecar->latest_result_slot))
            {
                sector_start = candidate_start;
                break;
            }
        }
        if (sector_start == UINT32_MAX)
        {
            /* With at least three erase sectors, at most two sectors are
             * protected by the physical and business latest records. Never
             * erase a protected sector or attempt destructive copy-forward. */
            return AI_RESULT_SIDECAR_STORAGE_ERROR;
        }
        if (sidecar->storage.erase(
                sidecar->storage.context,
                sector_start * AI_RESULT_SIDECAR_RECORD_BYTES,
                sidecar->storage.erase_sector_bytes) != 0)
        {
            return AI_RESULT_SIDECAR_STORAGE_ERROR;
        }
        slot = sector_start;
    }
    if (write_record(sidecar, slot, result, sequence, write_generation,
                     &committed) != AI_RESULT_SIDECAR_OK)
    {
        sidecar->next_generation = write_generation == UINT32_MAX
                                   ? 0U : write_generation + 1U;
        sidecar->next_slot = (slot + 1U) % sidecar->slot_count;
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    if (!sidecar->latest_valid
        || committed.result_sequence >= sidecar->latest.result_sequence)
    {
        sidecar->latest = committed;
        sidecar->latest_valid = 1;
        sidecar->latest_result_slot = slot;
    }
    if (sequence >= sidecar->next_sequence && sequence != UINT32_MAX)
    {
        sidecar->next_sequence = sequence + 1U;
    }
    else if (sequence == UINT32_MAX)
    {
        sidecar->next_sequence = 0U;
    }
    sidecar->latest_write_generation = write_generation;
    sidecar->latest_write_slot = slot;
    sidecar->next_generation = write_generation == UINT32_MAX
                               ? 0U : write_generation + 1U;
    sidecar->next_slot = (slot + 1U) % sidecar->slot_count;
    return AI_RESULT_SIDECAR_OK;
}

int ai_result_sidecar_get_latest(const ai_result_sidecar_t *sidecar,
                                 ai_result_t *result)
{
    if (sidecar == NULL || result == NULL || !sidecar->latest_valid)
    {
        return AI_RESULT_SIDECAR_NOT_FOUND;
    }
    *result = sidecar->latest;
    return AI_RESULT_SIDECAR_OK;
}

int ai_result_sidecar_get_event(const ai_result_sidecar_t *sidecar,
                                uint32_t event_id,
                                ai_result_t *result)
{
    uint8_t encoded[AI_RESULT_SIDECAR_RECORD_BYTES];
    ai_result_t candidate;
    ai_result_t selected = {0};
    uint32_t slot;
    int found = 0;

    if (sidecar == NULL || result == NULL || event_id == 0U)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    if (sidecar->latest_valid && sidecar->latest.event_id == event_id)
    {
        *result = sidecar->latest;
        return AI_RESULT_SIDECAR_OK;
    }
    for (slot = 0U; slot < sidecar->slot_count; slot++)
    {
        if (read_slot(sidecar, slot, encoded) != 0)
        {
            return AI_RESULT_SIDECAR_STORAGE_ERROR;
        }
        if (decode_record(encoded, &candidate, NULL)
            && candidate.event_id == event_id
            && (!found || candidate.result_sequence > selected.result_sequence))
        {
            selected = candidate;
            found = 1;
        }
    }
    if (!found)
    {
        return AI_RESULT_SIDECAR_NOT_FOUND;
    }
    *result = selected;
    return AI_RESULT_SIDECAR_OK;
}
