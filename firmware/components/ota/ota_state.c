#include <stdbool.h>
#include <string.h>

#include "ota_crc32.h"
#include "ota_state.h"

#define OTA_STATE_FORMAT_VERSION 1U
#define OTA_STATE_COMMIT_MARKER UINT32_C(0x5A3CC3A5)

static uint16_t ota_state_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ota_state_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static void ota_state_write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void ota_state_write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static bool ota_state_kind_is_valid(ota_state_kind_t state)
{
    return state >= OTA_STATE_NORMAL && state <= OTA_STATE_ROLLBACK_REQUIRED;
}

static bool ota_state_reserved_is_zero(const uint8_t *encoded)
{
    uint32_t index;

    for (index = 0U; index < OTA_STATE_RESERVED_BYTES; index++)
    {
        if (encoded[OTA_STATE_OFFSET_RESERVED + index] != 0U)
        {
            return false;
        }
    }
    return true;
}

static bool ota_state_record_is_erased(const uint8_t encoded[OTA_STATE_RECORD_BYTES])
{
    uint32_t index;

    for (index = 0U; index < OTA_STATE_RECORD_BYTES; index++)
    {
        if (encoded[index] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

void ota_state_record_encode(const ota_state_record_t *record,
                             uint8_t encoded[OTA_STATE_RECORD_BYTES])
{
    if (record == NULL || encoded == NULL || !ota_state_kind_is_valid(record->state))
    {
        return;
    }

    memset(encoded, 0, OTA_STATE_RECORD_BYTES);
    memcpy(&encoded[OTA_STATE_OFFSET_MAGIC], "OTST", 4U);
    ota_state_write_u16_le(&encoded[OTA_STATE_OFFSET_FORMAT_VERSION], OTA_STATE_FORMAT_VERSION);
    ota_state_write_u16_le(&encoded[OTA_STATE_OFFSET_HEADER_BYTES], OTA_STATE_RECORD_BYTES);
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_GENERATION], record->generation);
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_KIND], (uint32_t)record->state);
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_TRIAL_COUNT], record->trial_count);
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_COPY_OFFSET], record->copy_offset);
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_CRC32],
                           ota_crc32_compute(encoded, OTA_STATE_OFFSET_CRC32));
    ota_state_write_u32_le(&encoded[OTA_STATE_OFFSET_COMMIT_MARKER], OTA_STATE_COMMIT_MARKER);
}

int ota_state_record_decode(const uint8_t encoded[OTA_STATE_RECORD_BYTES],
                            ota_state_record_t *record)
{
    ota_state_record_t decoded;

    if (encoded == NULL || record == NULL
        || memcmp(&encoded[OTA_STATE_OFFSET_MAGIC], "OTST", 4U) != 0
        || ota_state_read_u16_le(&encoded[OTA_STATE_OFFSET_FORMAT_VERSION])
           != OTA_STATE_FORMAT_VERSION
        || ota_state_read_u16_le(&encoded[OTA_STATE_OFFSET_HEADER_BYTES])
           != OTA_STATE_RECORD_BYTES
        || !ota_state_reserved_is_zero(encoded)
        || ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_CRC32])
           != ota_crc32_compute(encoded, OTA_STATE_OFFSET_CRC32)
        || ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_COMMIT_MARKER])
           != OTA_STATE_COMMIT_MARKER)
    {
        return 0;
    }

    decoded.generation = ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_GENERATION]);
    decoded.state = (ota_state_kind_t)ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_KIND]);
    decoded.trial_count = ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_TRIAL_COUNT]);
    decoded.copy_offset = ota_state_read_u32_le(&encoded[OTA_STATE_OFFSET_COPY_OFFSET]);
    if (!ota_state_kind_is_valid(decoded.state))
    {
        return 0;
    }
    *record = decoded;
    return 1;
}

ota_state_selection_t ota_state_select_newest(
    const uint8_t primary[OTA_STATE_RECORD_BYTES],
    const uint8_t secondary[OTA_STATE_RECORD_BYTES],
    ota_state_record_t *selected)
{
    ota_state_record_t primary_record;
    ota_state_record_t secondary_record;
    const int primary_valid = ota_state_record_decode(primary, &primary_record);
    const int secondary_valid = ota_state_record_decode(secondary, &secondary_record);

    if (selected == NULL)
    {
        return OTA_STATE_SELECT_RECOVERY;
    }
    if (ota_state_record_is_erased(primary) && ota_state_record_is_erased(secondary))
    {
        return OTA_STATE_SELECT_UNINITIALIZED;
    }
    if (!primary_valid && !secondary_valid)
    {
        return OTA_STATE_SELECT_RECOVERY;
    }
    if (primary_valid && (!secondary_valid
                          || primary_record.generation >= secondary_record.generation))
    {
        *selected = primary_record;
        return OTA_STATE_SELECT_PRIMARY;
    }

    *selected = secondary_record;
    return OTA_STATE_SELECT_SECONDARY;
}

static bool ota_state_transition_is_allowed(ota_state_kind_t current,
                                            ota_state_kind_t next)
{
    switch (current)
    {
    case OTA_STATE_NORMAL:
    case OTA_STATE_CONFIRMED:
        return next == OTA_STATE_PENDING_INSTALL;
    case OTA_STATE_PENDING_INSTALL:
        return next == OTA_STATE_INSTALLING;
    case OTA_STATE_INSTALLING:
        return next == OTA_STATE_TRIAL;
    case OTA_STATE_TRIAL:
        return next == OTA_STATE_CONFIRMED;
    case OTA_STATE_ROLLBACK_REQUIRED:
        return next == OTA_STATE_INSTALLING;
    default:
        return false;
    }
}

ota_state_transition_status_t ota_state_transition(const ota_state_record_t *current,
                                                   ota_state_kind_t next_state,
                                                   ota_state_record_t *next)
{
    if (current == NULL || next == NULL)
    {
        return OTA_STATE_TRANSITION_INVALID_ARGUMENT;
    }
    if (!ota_state_kind_is_valid(current->state) || !ota_state_kind_is_valid(next_state)
        || !ota_state_transition_is_allowed(current->state, next_state))
    {
        return OTA_STATE_TRANSITION_ILLEGAL;
    }
    if (current->generation == UINT32_MAX)
    {
        return OTA_STATE_TRANSITION_GENERATION_OVERFLOW;
    }

    *next = *current;
    next->generation++;
    next->state = next_state;
    if (next_state == OTA_STATE_PENDING_INSTALL || next_state == OTA_STATE_TRIAL)
    {
        next->trial_count = 0U;
        next->copy_offset = 0U;
    }
    if (next_state == OTA_STATE_INSTALLING && current->state == OTA_STATE_ROLLBACK_REQUIRED)
    {
        next->copy_offset = 0U;
    }
    return OTA_STATE_TRANSITION_OK;
}

ota_state_trial_result_t ota_state_prepare_trial_failure(
    const ota_state_record_t *current, ota_state_record_t *next)
{
    if (current == NULL || next == NULL || current->state != OTA_STATE_TRIAL)
    {
        return OTA_STATE_TRIAL_INVALID_STATE;
    }
    if (current->generation == UINT32_MAX)
    {
        return OTA_STATE_TRIAL_GENERATION_OVERFLOW;
    }

    *next = *current;
    next->generation++;
    if (current->trial_count >= OTA_STATE_MAX_TRIALS
        || current->trial_count == UINT32_MAX)
    {
        next->state = OTA_STATE_ROLLBACK_REQUIRED;
        return OTA_STATE_TRIAL_ROLLBACK_REQUIRED;
    }

    next->trial_count++;
    if (next->trial_count >= OTA_STATE_MAX_TRIALS)
    {
        next->state = OTA_STATE_ROLLBACK_REQUIRED;
        return OTA_STATE_TRIAL_ROLLBACK_REQUIRED;
    }
    return OTA_STATE_TRIAL_CONTINUE;
}
