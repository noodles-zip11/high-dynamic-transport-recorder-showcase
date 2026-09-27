#include "ai_features.h"

#include <math.h>
#include <stddef.h>

static rt_err_t ai_features_visit_samples(const event_record_t *event,
                                          uint32_t *count,
                                          double *accel_peak_sq,
                                          double *accel_min_sq,
                                          double *accel_max_sq,
                                          double *accel_sum_sq,
                                          double *gyro_peak_sq)
{
    uint8_t block_index;
    uint16_t sample_index;

    if (event == RT_NULL || count == RT_NULL || accel_peak_sq == RT_NULL
        || accel_min_sq == RT_NULL || accel_max_sq == RT_NULL
        || accel_sum_sq == RT_NULL || gyro_peak_sq == RT_NULL)
    {
        return -RT_ERROR;
    }

    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        const sample_block_t *block = event->blocks[block_index];
        if (block == RT_NULL)
        {
            return -RT_ERROR;
        }
        for (sample_index = 0U; sample_index < block->sample_count; sample_index++)
        {
            const icm45686_fifo_sample_t *sample = &block->samples[sample_index];
            double accel_sq = (double)sample->accel[0] * sample->accel[0]
                            + (double)sample->accel[1] * sample->accel[1]
                            + (double)sample->accel[2] * sample->accel[2];
            double gyro_sq = (double)sample->gyro[0] * sample->gyro[0]
                           + (double)sample->gyro[1] * sample->gyro[1]
                           + (double)sample->gyro[2] * sample->gyro[2];

            if (*count == 0U || accel_sq < *accel_min_sq)
            {
                *accel_min_sq = accel_sq;
            }
            if (*count == 0U || accel_sq > *accel_max_sq)
            {
                *accel_max_sq = accel_sq;
            }
            if (accel_sq > *accel_peak_sq)
            {
                *accel_peak_sq = accel_sq;
            }
            if (gyro_sq > *gyro_peak_sq)
            {
                *gyro_peak_sq = gyro_sq;
            }
            *accel_sum_sq += accel_sq;
            (*count)++;
        }
    }
    return RT_EOK;
}

rt_err_t ai_features_extract_event(const event_record_t *event,
                                   ai_feature_vector_t *features)
{
    uint32_t count = 0U;
    double accel_peak_sq = 0.0;
    double accel_min_sq = 0.0;
    double accel_max_sq = 0.0;
    double accel_sum_sq = 0.0;
    double gyro_peak_sq = 0.0;
    double accel_peak;
    double accel_rms;

    if (features == RT_NULL
        || ai_features_visit_samples(event, &count, &accel_peak_sq,
                                     &accel_min_sq, &accel_max_sq,
                                     &accel_sum_sq, &gyro_peak_sq) != RT_EOK
        || count != AI_EVENT_SAMPLE_COUNT)
    {
        return -RT_ERROR;
    }

    accel_peak = sqrt(accel_peak_sq);
    accel_rms = sqrt(accel_sum_sq / (double)count);
    features->values[0] = (float)accel_peak;
    features->values[1] = (float)(sqrt(accel_max_sq) - sqrt(accel_min_sq));
    features->values[2] = (float)accel_rms;
    features->values[3] = (float)count / (float)AI_EVENT_SAMPLE_RATE_HZ;
    features->values[4] = (float)(accel_rms > 0.0
                                   ? accel_peak / accel_rms : 0.0);
    features->values[5] = (float)sqrt(gyro_peak_sq);
    return RT_EOK;
}
