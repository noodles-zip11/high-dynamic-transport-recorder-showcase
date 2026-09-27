#include "event_export_debug.h"

#include <limits.h>
#include <string.h>

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "event_quality.h"
#endif
#include "imu_sample_batcher.h"

#define EVENT_EXPORT_WRITE_BUFFER_BYTES 256U
#ifndef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#define EVENT_EXPORT_MAX_SEQUENCE_GAP \
    (EVENT_MAX_BLOCK_COUNT * SAMPLE_BLOCK_SAMPLE_CAPACITY)
#endif

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
typedef event_quality_loss_summary_t event_export_loss_summary_t;
#else
typedef struct
{
    uint32_t lost_sample_count;
    uint32_t first_lost_sequence;
    uint32_t last_lost_sequence;
    uint32_t loss_episode_count;
    uint64_t first_loss_monotonic_us;
    uint64_t last_loss_monotonic_us;
} event_export_loss_summary_t;
#endif

static void event_export_put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void event_export_put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void event_export_put_u64_le(uint8_t *out, uint64_t value)
{
    uint8_t index;

    for (index = 0U; index < 8U; index++)
    {
        out[index] = (uint8_t)(value >> (index * 8U));
    }
}

static void event_export_put_i64_le(uint8_t *out, int64_t value)
{
    event_export_put_u64_le(out, (uint64_t)value);
}

static uint32_t event_export_crc32_update(uint32_t crc,
                                          const uint8_t *data,
                                          rt_size_t length)
{
    rt_size_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }

    return crc;
}

static void event_export_encode_sample(uint8_t *out,
                                       const icm45686_fifo_sample_t *sample)
{
    event_export_put_u16_le(&out[0], (uint16_t)sample->accel[0]);
    event_export_put_u16_le(&out[2], (uint16_t)sample->accel[1]);
    event_export_put_u16_le(&out[4], (uint16_t)sample->accel[2]);
    event_export_put_u16_le(&out[6], (uint16_t)sample->gyro[0]);
    event_export_put_u16_le(&out[8], (uint16_t)sample->gyro[1]);
    event_export_put_u16_le(&out[10], (uint16_t)sample->gyro[2]);
    event_export_put_u16_le(&out[12], sample->timestamp);
    out[14] = (uint8_t)sample->temperature;
    out[15] = sample->header;
}

static uint32_t event_export_payload_crc32(const event_record_t *event)
{
    uint8_t encoded[EVENT_EXPORT_SAMPLE_SIZE];
    uint32_t crc = 0xFFFFFFFFU;
    uint8_t block_index;
    uint16_t sample_index;

    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        for (sample_index = 0U;
             sample_index < event->blocks[block_index]->sample_count;
             sample_index++)
        {
            event_export_encode_sample(encoded,
                                       &event->blocks[block_index]->samples[sample_index]);
            crc = event_export_crc32_update(crc, encoded, sizeof(encoded));
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

#ifndef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static uint32_t event_export_sample_count(const event_record_t *event,
                                          uint8_t first_block,
                                          uint8_t block_count)
{
    uint32_t sample_count = 0U;
    uint8_t index;

    for (index = first_block; index < block_count; index++)
    {
        sample_count += event->blocks[index]->sample_count;
    }

    return sample_count;
}

static rt_bool_t event_export_loss_summary(
    const event_record_t *event,
    event_export_loss_summary_t *summary)
{
    uint8_t index;

    if (event == RT_NULL || summary == RT_NULL)
    {
        return RT_FALSE;
    }
    memset(summary, 0, sizeof(*summary));

    for (index = 1U; index < event->block_count; index++)
    {
        const sample_block_t *previous = event->blocks[index - 1U];
        const sample_block_t *current = event->blocks[index];
        uint32_t expected_sequence;
        uint32_t gap;
        uint64_t previous_span_us;
        uint64_t loss_start_us;
        uint64_t loss_duration_us;

        if (previous == RT_NULL || current == RT_NULL)
        {
            return RT_FALSE;
        }
        expected_sequence = previous->sequence + previous->sample_count;
        gap = current->sequence - expected_sequence;
        if (gap == 0U)
        {
            continue;
        }
        if (gap > EVENT_EXPORT_MAX_SEQUENCE_GAP
            || summary->lost_sample_count > UINT32_MAX - gap
            || summary->loss_episode_count == UINT32_MAX)
        {
            return RT_FALSE;
        }
        previous_span_us = ((uint64_t)previous->sample_count
                            * previous->sample_period_ns) / 1000U;
        if (previous->first_monotonic_us > UINT64_MAX - previous_span_us)
        {
            return RT_FALSE;
        }
        loss_start_us = previous->first_monotonic_us + previous_span_us;
        loss_duration_us = ((uint64_t)(gap - 1U)
                            * previous->sample_period_ns) / 1000U;
        if (loss_start_us > UINT64_MAX - loss_duration_us)
        {
            return RT_FALSE;
        }

        if (summary->loss_episode_count == 0U)
        {
            summary->first_lost_sequence = expected_sequence;
            summary->first_loss_monotonic_us = loss_start_us;
        }
        summary->lost_sample_count += gap;
        summary->last_lost_sequence = current->sequence - 1U;
        summary->loss_episode_count++;
        summary->last_loss_monotonic_us = loss_start_us + loss_duration_us;
    }

    return RT_TRUE;
}
#endif

static void event_export_encode_header(uint8_t *header,
                                       const event_record_t *event,
                                       uint32_t pretrigger_samples,
                                       uint32_t posttrigger_samples,
                                       uint32_t payload_crc32,
                                       const event_export_loss_summary_t *loss)
{
    uint32_t payload_length = (pretrigger_samples + posttrigger_samples)
                              * EVENT_EXPORT_SAMPLE_SIZE;
    const health_snapshot_t *snapshot = &event->health_snapshot;
    uint16_t context_valid_flags = snapshot->context_valid_flags
                                   & EVENT_EXPORT_CONTEXT_DEFINED_MASK;
    uint16_t environment_age_seconds;

    memset(header, 0, EVENT_EXPORT_HEADER_SIZE);
    if (snapshot->utc_valid)
    {
        context_valid_flags |= EVENT_EXPORT_CONTEXT_UTC_VALID;
    }
    if (snapshot->environment_valid)
    {
        context_valid_flags |= EVENT_EXPORT_CONTEXT_ENVIRONMENT_VALID;
    }
    if (snapshot->environment_fresh)
    {
        context_valid_flags |= EVENT_EXPORT_CONTEXT_ENVIRONMENT_FRESH;
    }
    if (snapshot->power_state != HEALTH_POWER_STATE_UNAVAILABLE)
    {
        context_valid_flags |= EVENT_EXPORT_CONTEXT_POWER_KNOWN;
    }
    environment_age_seconds = snapshot->environment_age_seconds > UINT16_MAX
                              ? UINT16_MAX
                              : (uint16_t)snapshot->environment_age_seconds;

    memcpy(header, "EV03", 4U);
    event_export_put_u16_le(&header[4], EVENT_EXPORT_VERSION);
    event_export_put_u16_le(&header[6], EVENT_EXPORT_HEADER_SIZE);
    event_export_put_u32_le(&header[8], event->event_id);
    event_export_put_u64_le(&header[12], event->trigger_monotonic_us);
    event_export_put_i64_le(&header[20], snapshot->utc_unix_seconds);
    event_export_put_u32_le(&header[28], snapshot->time_epoch_id);
    event_export_put_u32_le(&header[32], EVENT_EXPORT_SAMPLE_RATE_HZ);
    event_export_put_u32_le(&header[36], event->trigger_sequence);
    event_export_put_u32_le(&header[40], pretrigger_samples);
    event_export_put_u32_le(&header[44], posttrigger_samples);
    event_export_put_u16_le(&header[48], event->subtrigger_count);
    event_export_put_u16_le(&header[50],
                            event->flags
                            | (loss->lost_sample_count != 0U
                               ? EVENT_FLAG_DATA_LOSS : 0U));
    event_export_put_u32_le(&header[52], event->peak_magnitude_sq);
    event_export_put_u32_le(&header[56], event->threshold_magnitude_sq);
    event_export_put_u32_le(&header[60], payload_length);
    event_export_put_u32_le(&header[64], payload_crc32);
    header[68] = (uint8_t)snapshot->state;
    header[69] = (uint8_t)snapshot->power_state;
    event_export_put_u16_le(&header[70], context_valid_flags);
    event_export_put_u32_le(&header[72], snapshot->reset_raw_flags);
    event_export_put_u16_le(&header[76], (uint16_t)snapshot->temperature_centi_c);
    event_export_put_u16_le(&header[78], environment_age_seconds);
    event_export_put_u32_le(&header[80], snapshot->humidity_milli_rh);
    event_export_put_u32_le(&header[84], snapshot->free_log_bytes);
    event_export_put_u16_le(&header[88], snapshot->sample_pool_min_free);
    event_export_put_u32_le(&header[92], snapshot->last_fault_code);
    event_export_put_u32_le(&header[96], snapshot->imu_transport_error_count);
    event_export_put_u32_le(&header[100], snapshot->imu_dma_error_count);
    event_export_put_u32_le(&header[104], snapshot->sample_pool_backpressure_count);
    event_export_put_u32_le(&header[108], snapshot->storage_error_count);
    event_export_put_u32_le(&header[112], snapshot->event_export_error_count);
    event_export_put_u32_le(&header[124], snapshot->transition_sequence);
    event_export_put_u32_le(&header[128], loss->lost_sample_count);
    event_export_put_u32_le(&header[132], loss->first_lost_sequence);
    event_export_put_u32_le(&header[136], loss->last_lost_sequence);
    event_export_put_u32_le(&header[140], loss->loss_episode_count);
    event_export_put_u64_le(&header[144], loss->first_loss_monotonic_us);
    event_export_put_u64_le(&header[152], loss->last_loss_monotonic_us);
}

rt_err_t event_export_debug_write(event_assembler_t *assembler,
                                  sample_block_pool_t *pool,
                                  event_export_write_fn write,
                                  void *context)
{
    const event_record_t *event;
    uint8_t header[EVENT_EXPORT_HEADER_SIZE];
    uint8_t encoded[EVENT_EXPORT_WRITE_BUFFER_BYTES];
    uint8_t block_index;
    uint16_t sample_index;
    uint16_t encoded_length;
    uint32_t pretrigger_samples;
    uint32_t posttrigger_samples;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    event_quality_facts_t facts;
#else
    event_export_loss_summary_t loss;
#endif

    if (assembler == RT_NULL || pool == RT_NULL || write == RT_NULL)
    {
        return -RT_ERROR;
    }

    event = event_assembler_event(assembler);
    if (event == RT_NULL || assembler->state != EVENT_EXPORTING)
    {
        return -RT_ERROR;
    }

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    if (event_quality_collect_facts(pool, event, &facts) != RT_EOK)
    {
        goto failed;
    }
    pretrigger_samples = facts.pretrigger_samples;
    posttrigger_samples = facts.posttrigger_samples;
    event_export_encode_header(header, event, pretrigger_samples,
                               posttrigger_samples,
                               event_export_payload_crc32(event), &facts.loss);
#else
    pretrigger_samples = event_export_sample_count(
        event, 0U, event->pretrigger_block_count)
        + event->trigger_sample_index;
    posttrigger_samples = event_export_sample_count(
        event, 0U, event->block_count) - pretrigger_samples;
    if (!event_export_loss_summary(event, &loss))
    {
        goto failed;
    }
    event_export_encode_header(header, event, pretrigger_samples,
                               posttrigger_samples,
                               event_export_payload_crc32(event), &loss);
#endif
    if (write(header, sizeof(header), context) != RT_EOK)
    {
        goto failed;
    }

    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        encoded_length = 0U;
        for (sample_index = 0U;
             sample_index < event->blocks[block_index]->sample_count;
             sample_index++)
        {
            event_export_encode_sample(&encoded[encoded_length],
                                       &event->blocks[block_index]->samples[sample_index]);
            encoded_length += EVENT_EXPORT_SAMPLE_SIZE;
            if (encoded_length == sizeof(encoded)
                || sample_index + 1U == event->blocks[block_index]->sample_count)
            {
                if (write(encoded, encoded_length, context) != RT_EOK)
                {
                    goto failed;
                }
                encoded_length = 0U;
            }
        }

        if (sample_block_pool_release(pool, event->blocks[block_index]) != RT_EOK)
        {
            goto failed;
        }
        assembler->event.blocks[block_index] = RT_NULL;
    }

    return event_assembler_finish_export(assembler, pool, 1);

failed:
    (void)event_assembler_finish_export(assembler, pool, 0);
    return -RT_ERROR;
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static uint16_t event_export_get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t event_export_get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static uint64_t event_export_get_u64_le(const uint8_t *data)
{
    uint64_t value = 0U;
    uint8_t index;

    for (index = 0U; index < 8U; index++)
    {
        value |= (uint64_t)data[index] << (index * 8U);
    }
    return value;
}

rt_err_t event_export_debug_decode_header(const uint8_t *data,
                                          uint32_t length,
                                          uint32_t ev01_length,
                                          event_quality_facts_t *facts)
{
    uint32_t payload_length;
    uint32_t pretrigger_samples;
    uint32_t posttrigger_samples;
    uint32_t actual_sample_count;
    uint32_t adjusted_post_samples;
    uint32_t pretrigger_blocks;
    uint32_t posttrigger_blocks;
    uint16_t flags;
    uint8_t post_divisible;
    uint8_t has_short_pretrigger;
    uint8_t has_capped_duration;

    if (data == RT_NULL || facts == RT_NULL
        || length < EVENT_EXPORT_HEADER_SIZE
        || memcmp(data, "EV03", 4U) != 0
        || event_export_get_u16_le(&data[4]) != EVENT_EXPORT_VERSION
        || event_export_get_u16_le(&data[6]) != EVENT_EXPORT_HEADER_SIZE
        || ev01_length < EVENT_EXPORT_HEADER_SIZE)
    {
        return -RT_ERROR;
    }

    payload_length = event_export_get_u32_le(&data[60]);
    if (payload_length != ev01_length - EVENT_EXPORT_HEADER_SIZE
        || payload_length == 0U
        || payload_length % EVENT_EXPORT_SAMPLE_SIZE != 0U)
    {
        return -RT_ERROR;
    }
    actual_sample_count = payload_length / EVENT_EXPORT_SAMPLE_SIZE;
    if (actual_sample_count > EVENT_MAX_BLOCK_COUNT
                                   * SAMPLE_BLOCK_SAMPLE_CAPACITY)
    {
        return -RT_ERROR;
    }
    pretrigger_samples = event_export_get_u32_le(&data[40]);
    posttrigger_samples = event_export_get_u32_le(&data[44]);
    if (pretrigger_samples > UINT32_MAX - posttrigger_samples
        || pretrigger_samples + posttrigger_samples != actual_sample_count)
    {
        return -RT_ERROR;
    }
    flags = event_export_get_u16_le(&data[50]);
    if ((flags & (uint16_t)~(EVENT_FLAG_PRETRIGGER_SHORT
                             | EVENT_FLAG_DURATION_CAPPED
                             | EVENT_FLAG_RESOURCE_LIMIT
                             | EVENT_FLAG_DATA_LOSS)) != 0U)
    {
        return -RT_ERROR;
    }

    memset(facts, 0, sizeof(*facts));
    facts->event_id = event_export_get_u32_le(&data[8]);
    facts->trigger_sequence = event_export_get_u32_le(&data[36]);
    facts->actual_sample_count = actual_sample_count;
    facts->pretrigger_samples = pretrigger_samples;
    facts->posttrigger_samples = posttrigger_samples;
    facts->trigger_sample_index =
        (uint16_t)(pretrigger_samples % IMU_SAMPLE_BATCH_SIZE);
    facts->flags = flags;
    facts->loss.lost_sample_count = event_export_get_u32_le(&data[128]);
    facts->loss.first_lost_sequence = event_export_get_u32_le(&data[132]);
    facts->loss.last_lost_sequence = event_export_get_u32_le(&data[136]);
    facts->loss.loss_episode_count = event_export_get_u32_le(&data[140]);
    facts->loss.first_loss_monotonic_us =
        event_export_get_u64_le(&data[144]);
    facts->loss.last_loss_monotonic_us =
        event_export_get_u64_le(&data[152]);
    if (((flags & EVENT_FLAG_DATA_LOSS) != 0U
         && facts->loss.lost_sample_count == 0U)
        || ((flags & EVENT_FLAG_DATA_LOSS) == 0U
            && facts->loss.lost_sample_count != 0U)
        || (facts->loss.lost_sample_count == 0U
            && (facts->loss.first_lost_sequence != 0U
                || facts->loss.last_lost_sequence != 0U
                || facts->loss.loss_episode_count != 0U
                || facts->loss.first_loss_monotonic_us != 0U
                || facts->loss.last_loss_monotonic_us != 0U))
        || (facts->loss.lost_sample_count != 0U
            && (facts->loss.loss_episode_count == 0U
                || facts->loss.loss_episode_count
                       > facts->loss.lost_sample_count
                 || facts->loss.last_loss_monotonic_us
                        < facts->loss.first_loss_monotonic_us)))
    {
        return -RT_ERROR;
    }

    pretrigger_blocks = pretrigger_samples / IMU_SAMPLE_BATCH_SIZE;
    if (pretrigger_samples % IMU_SAMPLE_BATCH_SIZE > UINT32_MAX
        - posttrigger_samples)
    {
        return -RT_ERROR;
    }
    adjusted_post_samples = posttrigger_samples
                            + (pretrigger_samples % IMU_SAMPLE_BATCH_SIZE);
    post_divisible =
        (adjusted_post_samples % IMU_SAMPLE_BATCH_SIZE) == 0U;
    posttrigger_blocks = adjusted_post_samples / IMU_SAMPLE_BATCH_SIZE;
    if (pretrigger_blocks > EVENT_MAX_BLOCK_COUNT
        || posttrigger_blocks > EVENT_MAX_BLOCK_COUNT
        || pretrigger_blocks + posttrigger_blocks > EVENT_MAX_BLOCK_COUNT)
    {
        return -RT_ERROR;
    }
    facts->block_count = (uint8_t)(pretrigger_blocks + posttrigger_blocks);
    facts->pretrigger_block_count = (uint8_t)pretrigger_blocks;
    facts->posttrigger_block_count = (uint8_t)posttrigger_blocks;
    facts->sequence_continuous = facts->loss.lost_sample_count == 0U;
    has_short_pretrigger =
        (flags & EVENT_FLAG_PRETRIGGER_SHORT) != 0U;
    has_capped_duration =
        (flags & EVENT_FLAG_DURATION_CAPPED) != 0U;
    facts->fixed_shape = post_divisible
                         && (flags & EVENT_FLAG_RESOURCE_LIMIT) == 0U
                         && (has_short_pretrigger
                             ? pretrigger_blocks < EVENT_PRETRIGGER_BLOCK_COUNT
                             : pretrigger_blocks == EVENT_PRETRIGGER_BLOCK_COUNT)
                         && (has_capped_duration
                             ? posttrigger_blocks == EVENT_MAX_POST_BLOCK_COUNT
                             : posttrigger_blocks
                                   == EVENT_STANDARD_POST_BLOCK_COUNT);
    facts->pretrigger_short =
        has_short_pretrigger
        || pretrigger_blocks < EVENT_PRETRIGGER_BLOCK_COUNT;
    facts->duration_capped =
        has_capped_duration;
    facts->serialization = EVENT_QUALITY_SERIALIZABLE;
    return RT_EOK;
}
#endif
