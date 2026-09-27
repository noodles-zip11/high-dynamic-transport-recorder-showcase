#include "imu_sample_batcher.h"

#include <string.h>

static uint64_t imu_sample_batcher_time_offset_us(uint32_t sample_period_ns)
{
    return ((uint64_t)IMU_SAMPLE_BATCH_SIZE * sample_period_ns) / 1000U;
}

static uint64_t imu_sample_batcher_advance_time(uint64_t first_monotonic_us,
                                                uint32_t sample_period_ns)
{
    uint64_t offset_us =
        imu_sample_batcher_time_offset_us(sample_period_ns);

    if (first_monotonic_us > UINT64_MAX - offset_us)
    {
        return UINT64_MAX;
    }

    return first_monotonic_us + offset_us;
}

void imu_sample_batcher_init(imu_sample_batcher_t *batcher)
{
    if (batcher != RT_NULL)
    {
        memset(batcher, 0, sizeof(*batcher));
    }
}

rt_err_t imu_sample_batcher_push(imu_sample_batcher_t *batcher,
                                 const icm45686_fifo_sample_t *samples,
                                 uint16_t sample_count,
                                 uint32_t first_sequence,
                                 uint64_t first_monotonic_us,
                                 uint32_t sample_period_ns,
                                 imu_sample_batcher_emit_fn emit,
                                 void *context)
{
    uint16_t pending_count;
    rt_err_t first_error = RT_EOK;

    if (batcher == RT_NULL || emit == RT_NULL
        || sample_count > IMU_SAMPLE_BATCHER_MAX_INPUT_SAMPLES
        || (sample_count != 0U
            && (samples == RT_NULL || sample_period_ns == 0U)))
    {
        return -RT_ERROR;
    }
    if (sample_count != 0U
        && batcher->sample_count
           > IMU_SAMPLE_BATCHER_CAPACITY - sample_count)
    {
        return -RT_ERROR;
    }

    pending_count = batcher->sample_count;
    if (sample_count != 0U && pending_count == 0U)
    {
        batcher->first_sequence = first_sequence;
        batcher->first_monotonic_us = first_monotonic_us;
        batcher->sample_period_ns = sample_period_ns;
    }
    if (sample_count != 0U)
    {
        memmove(&batcher->samples[pending_count], samples,
                sample_count * sizeof(samples[0]));
        batcher->sample_count = (uint16_t)(pending_count + sample_count);
    }

    while (batcher->sample_count >= IMU_SAMPLE_BATCH_SIZE)
    {
        rt_err_t result = emit(batcher->samples,
                               IMU_SAMPLE_BATCH_SIZE,
                               batcher->first_sequence,
                               batcher->first_monotonic_us,
                               batcher->sample_period_ns,
                               context);

        if (result != RT_EOK && first_error == RT_EOK)
        {
            first_error = result;
        }

        batcher->sample_count =
            (uint16_t)(batcher->sample_count - IMU_SAMPLE_BATCH_SIZE);
        if (batcher->sample_count != 0U)
        {
            memmove(batcher->samples,
                    &batcher->samples[IMU_SAMPLE_BATCH_SIZE],
                    batcher->sample_count * sizeof(batcher->samples[0]));
        }
        batcher->first_sequence += IMU_SAMPLE_BATCH_SIZE;
        batcher->first_monotonic_us = imu_sample_batcher_advance_time(
            batcher->first_monotonic_us,
            batcher->sample_period_ns);
    }

    return first_error;
}
