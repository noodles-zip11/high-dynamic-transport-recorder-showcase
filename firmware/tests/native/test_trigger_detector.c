#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "trigger_detector.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "trigger_detector: %s\n", message);
        return 0;
    }

    return 1;
}

static int test_requires_two_consecutive_threshold_hits(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = 100U,
        .consecutive_samples = 2U,
    };
    icm45686_fifo_sample_t sample = {0};
    trigger_fact_t fact = {0};

    trigger_detector_init(&detector, &config);
    sample.accel[0] = 9;
    if (!expect(!trigger_detector_feed(&detector, &sample, 10U, 3U, &fact),
                "a sub-threshold sample must not trigger"))
    {
        return 1;
    }

    sample.accel[0] = 10;
    if (!expect(!trigger_detector_feed(&detector, &sample, 11U, 4U, &fact),
                "the first threshold hit must not trigger")
        || !expect(trigger_detector_feed(&detector, &sample, 12U, 5U, &fact),
                   "the second consecutive hit must trigger")
        || !expect(fact.sample_sequence == 12U && fact.magnitude_sq == 100U,
                   "the trigger fact must describe the triggering sample")
        || !expect(fact.sample_index == 5U,
                   "the trigger fact must retain the index inside its block")
        || !expect(fact.axis_mask == TRIGGER_AXIS_X,
                   "the trigger fact must retain the active axis"))
    {
        return 1;
    }

    return 0;
}

static int test_interrupted_run_and_minimum_negative_value_are_safe(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = UINT32_C(1073741824),
        .consecutive_samples = 2U,
    };
    icm45686_fifo_sample_t sample = {0};
    trigger_fact_t fact = {0};

    trigger_detector_init(&detector, &config);
    sample.accel[0] = -32768;
    if (!expect(!trigger_detector_feed(&detector, &sample, 1U, 0U, &fact),
                "a first INT16_MIN hit must not trigger"))
    {
        return 1;
    }

    memset(&sample, 0, sizeof(sample));
    if (!expect(!trigger_detector_feed(&detector, &sample, 2U, 1U, &fact),
                "a sub-threshold sample must reset the consecutive count"))
    {
        return 1;
    }

    sample.accel[0] = -32768;
    return !expect(!trigger_detector_feed(&detector, &sample, 3U, 2U, &fact),
                   "a restarted run needs another hit")
           || !expect(trigger_detector_feed(&detector, &sample, 4U, 3U, &fact),
                      "INT16_MIN must be squared without overflow");
}

static int test_invalid_arguments_do_not_dereference_samples(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = 1U,
        .consecutive_samples = 2U,
    };
    trigger_fact_t fact = {0};

    trigger_detector_init(&detector, &config);
    return !expect(!trigger_detector_feed(&detector, NULL, 1U, 0U, &fact),
                   "a null sample must be rejected safely");
}

static int test_below_comparison_requires_eight_consecutive_low_samples(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = UINT32_C(2359296),
        .consecutive_samples = 8U,
        .comparison = TRIGGER_COMPARISON_BELOW,
    };
    icm45686_fifo_sample_t sample = {0};
    trigger_fact_t fact = {0};
    unsigned int index;

    trigger_detector_init(&detector, &config);
    sample.accel[0] = 2048;
    if (!expect(!trigger_detector_feed(&detector, &sample, 1U, 0U, &fact),
                "1g must not trigger a below-threshold detector")
        || !expect(detector.consecutive_count == 0U,
                   "a sample above the below threshold must not accumulate"))
    {
        return 1;
    }

    sample.accel[0] = 1536;
    for (index = 0U; index < 3U; index++)
    {
        if (!expect(!trigger_detector_feed(&detector, &sample, 2U + index,
                                           (uint16_t)index, &fact),
                    "the first low samples must not trigger"))
        {
            return 1;
        }
    }

    sample.accel[0] = 2048;
    if (!expect(!trigger_detector_feed(&detector, &sample, 5U, 3U, &fact),
                "an above-threshold sample must reset a low run")
        || !expect(detector.consecutive_count == 0U,
                   "an above-threshold sample must clear the low count"))
    {
        return 1;
    }

    sample.accel[0] = 1536;
    for (index = 0U; index < 7U; index++)
    {
        if (!expect(!trigger_detector_feed(&detector, &sample, 6U + index,
                                           (uint16_t)(4U + index), &fact),
                    "seven low samples must not trigger"))
        {
            return 1;
        }
    }

    return !expect(trigger_detector_feed(&detector, &sample, 13U, 11U, &fact),
                   "the eighth consecutive low sample must trigger")
           || !expect(fact.magnitude_sq == UINT32_C(2359296),
                      "the below trigger fact must retain the low magnitude");
}

static int test_below_comparison_handles_int16_min_without_overflow(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = UINT32_C(2359296),
        .consecutive_samples = 8U,
        .comparison = TRIGGER_COMPARISON_BELOW,
    };
    icm45686_fifo_sample_t sample = {0};
    trigger_fact_t fact = {0};

    trigger_detector_init(&detector, &config);
    sample.accel[0] = INT16_MIN;
    sample.accel[1] = INT16_MIN;
    sample.accel[2] = INT16_MIN;
    return !expect(!trigger_detector_feed(&detector, &sample, 1U, 0U, &fact),
                   "INT16_MIN must be above a low-g threshold without overflow")
           || !expect(detector.consecutive_count == 0U,
                      "an overflowing low-g comparison must not accumulate");
}

static int test_zero_and_omitted_comparison_preserve_above_compatibility(void)
{
    trigger_detector_t detector;
    trigger_detector_config_t config = {
        .threshold_magnitude_sq = 0U,
        .consecutive_samples = 1U,
    };
    icm45686_fifo_sample_t sample = {0};
    trigger_fact_t fact = {0};

    trigger_detector_init(&detector, &config);
    return !expect(trigger_detector_feed(&detector, &sample, 1U, 0U, &fact),
                   "zero comparison must default to the existing above behavior")
           || !expect(fact.magnitude_sq == 0U,
                      "the zero-magnitude above trigger must retain its fact");
}

int main(void)
{
    if (test_requires_two_consecutive_threshold_hits() != 0
        || test_interrupted_run_and_minimum_negative_value_are_safe() != 0
        || test_invalid_arguments_do_not_dereference_samples() != 0
        || test_below_comparison_requires_eight_consecutive_low_samples() != 0
        || test_below_comparison_handles_int16_min_without_overflow() != 0
        || test_zero_and_omitted_comparison_preserve_above_compatibility() != 0)
    {
        return 1;
    }

    puts("trigger detector: PASS");
    return 0;
}
