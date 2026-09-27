#ifndef TRANSPORT_RECORDER_TRIGGER_DETECTOR_H
#define TRANSPORT_RECORDER_TRIGGER_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "icm45686_fifo.h"

#define TRIGGER_AXIS_X UINT8_C(0x01)
#define TRIGGER_AXIS_Y UINT8_C(0x02)
#define TRIGGER_AXIS_Z UINT8_C(0x04)

typedef enum
{
    TRIGGER_COMPARISON_ABOVE = 0,
    TRIGGER_COMPARISON_BELOW,
} trigger_comparison_t;

typedef struct
{
    uint32_t threshold_magnitude_sq;
    uint8_t consecutive_samples;
    trigger_comparison_t comparison;
} trigger_detector_config_t;

typedef struct
{
    trigger_detector_config_t config;
    uint8_t consecutive_count;
} trigger_detector_t;

typedef struct
{
    uint32_t sample_sequence;
    uint32_t magnitude_sq;
    uint32_t threshold_magnitude_sq;
    uint16_t sample_index;
    uint8_t axis_mask;
} trigger_fact_t;

void trigger_detector_init(trigger_detector_t *detector,
                           const trigger_detector_config_t *config);

bool trigger_detector_feed(trigger_detector_t *detector,
                           const icm45686_fifo_sample_t *sample,
                           uint32_t sample_sequence,
                           uint16_t sample_index,
                           trigger_fact_t *out);

#endif
