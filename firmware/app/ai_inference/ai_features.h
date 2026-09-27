#ifndef TRANSPORT_RECORDER_AI_FEATURES_H
#define TRANSPORT_RECORDER_AI_FEATURES_H

#include <stdint.h>

#include "event_assembler.h"

#define AI_FEATURE_COUNT 6U
#define AI_EVENT_SAMPLE_RATE_HZ 1600U
#define AI_EVENT_SAMPLE_COUNT 2400U
#define AI_EVENT_BLOCK_CAPACITY EVENT_MAX_BLOCK_COUNT

typedef struct
{
    float values[AI_FEATURE_COUNT];
} ai_feature_vector_t;

/* Values are in the same order and raw-count domain as ai/src/features.py. */
rt_err_t ai_features_extract_event(const event_record_t *event,
                                    ai_feature_vector_t *features);

#endif
