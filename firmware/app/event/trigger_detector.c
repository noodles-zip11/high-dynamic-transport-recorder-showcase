#include "trigger_detector.h"

#include <string.h>

static uint64_t square_i16(int16_t value)
{
    const int64_t widened = (int64_t)value;

    return (uint64_t)(widened * widened);
}

static uint8_t trigger_axis_mask(const icm45686_fifo_sample_t *sample)
{
    uint8_t mask = 0U;

    if (sample->accel[0] != 0)
    {
        mask |= TRIGGER_AXIS_X;
    }
    if (sample->accel[1] != 0)
    {
        mask |= TRIGGER_AXIS_Y;
    }
    if (sample->accel[2] != 0)
    {
        mask |= TRIGGER_AXIS_Z;
    }

    return mask;
}

void trigger_detector_init(trigger_detector_t *detector,
                           const trigger_detector_config_t *config)
{
    if (detector == NULL)
    {
        return;
    }

    memset(detector, 0, sizeof(*detector));
    if (config != NULL)
    {
        detector->config = *config;
    }
}

bool trigger_detector_feed(trigger_detector_t *detector,
                           const icm45686_fifo_sample_t *sample,
                           uint32_t sample_sequence,
                           uint16_t sample_index,
                           trigger_fact_t *out)
{
    uint64_t magnitude_sq;

    if (detector == NULL || sample == NULL || out == NULL
        || detector->config.consecutive_samples == 0U)
    {
        return false;
    }

    magnitude_sq = square_i16(sample->accel[0])
                   + square_i16(sample->accel[1])
                   + square_i16(sample->accel[2]);

    if ((detector->config.comparison == TRIGGER_COMPARISON_BELOW
         && magnitude_sq > detector->config.threshold_magnitude_sq)
        || (detector->config.comparison != TRIGGER_COMPARISON_BELOW
            && magnitude_sq < detector->config.threshold_magnitude_sq))
    {
        detector->consecutive_count = 0U;
        return false;
    }

    if (detector->consecutive_count < detector->config.consecutive_samples)
    {
        detector->consecutive_count++;
    }

    if (detector->consecutive_count < detector->config.consecutive_samples)
    {
        return false;
    }

    out->sample_sequence = sample_sequence;
    out->magnitude_sq = (uint32_t)magnitude_sq;
    out->threshold_magnitude_sq = detector->config.threshold_magnitude_sq;
    out->sample_index = sample_index;
    out->axis_mask = trigger_axis_mask(sample);
    return true;
}
