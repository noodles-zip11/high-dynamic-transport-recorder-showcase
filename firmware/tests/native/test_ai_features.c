#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ai_features.h"

static sample_block_t blocks[AI_EVENT_BLOCK_CAPACITY];

static int near_value(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static int test_extracts_counts_v1_in_frozen_order(void)
{
    event_record_t event = {0};
    ai_feature_vector_t features = {0};
    uint32_t sample_index;
    uint32_t block_index;
    uint32_t remaining = AI_EVENT_SAMPLE_COUNT;

    memset(blocks, 0, sizeof(blocks));
    for (block_index = 0U; remaining > 0U; block_index++)
    {
        uint16_t count = (remaining > SAMPLE_BLOCK_SAMPLE_CAPACITY)
                         ? SAMPLE_BLOCK_SAMPLE_CAPACITY
                         : (uint16_t)remaining;
        blocks[block_index].sample_count = count;
        event.blocks[block_index] = &blocks[block_index];
        event.block_count++;
        remaining -= count;
    }
    blocks[0].samples[0].accel[0] = 3;
    blocks[0].samples[0].accel[1] = 4;
    blocks[0].samples[1].gyro[2] = 6;

    if (ai_features_extract_event(&event, &features) != RT_EOK)
    {
        fputs("ai features: valid event was rejected\n", stderr);
        return 1;
    }
    if (!near_value(features.values[0], 5.0F, 0.001F)
        || !near_value(features.values[1], 5.0F, 0.001F)
        || !near_value(features.values[2], sqrtf(25.0F / AI_EVENT_SAMPLE_COUNT), 0.001F)
        || !near_value(features.values[3], 1.5F, 0.0001F)
        || !near_value(features.values[4], sqrtf((float)AI_EVENT_SAMPLE_COUNT), 0.01F)
        || !near_value(features.values[5], 6.0F, 0.001F))
    {
        fputs("ai features: counts_v1 values do not match host formulas\n", stderr);
        return 1;
    }

    (void)sample_index;
    return 0;
}

static int test_rejects_wrong_event_length(void)
{
    event_record_t event = {0};
    ai_feature_vector_t features = {0};

    event.block_count = 1U;
    event.blocks[0] = &blocks[0];
    blocks[0].sample_count = 1U;
    if (ai_features_extract_event(&event, &features) != -RT_ERROR)
    {
        fputs("ai features: wrong event length was accepted\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_extracts_counts_v1_in_frozen_order() != 0
        || test_rejects_wrong_event_length() != 0)
    {
        return 1;
    }
    puts("ai features: PASS");
    return 0;
}
