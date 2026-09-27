#include <stdint.h>
#include <stdio.h>

#include "reset_power_service.h"

typedef struct
{
    uint32_t raw_flags;
    int read_result;
    int clear_result;
    unsigned int read_calls;
    unsigned int clear_calls;
} fake_reset_context_t;

static int fake_read_reset_flags(uint32_t *raw_flags, void *context)
{
    fake_reset_context_t *fake = context;

    fake->read_calls++;
    if (fake->read_result != 0)
    {
        return fake->read_result;
    }

    *raw_flags = fake->raw_flags;
    return 0;
}

static int fake_clear_reset_flags(void *context)
{
    fake_reset_context_t *fake = context;

    fake->clear_calls++;
    return fake->clear_result;
}

static int test_normalization_priority(void)
{
    if (reset_power_normalize(RESET_POWER_RAW_BROWN_OUT
                              | RESET_POWER_RAW_IWDG)
            != RESET_POWER_CAUSE_BOR_OR_POR
        || reset_power_normalize(RESET_POWER_RAW_POWER_ON
                                 | RESET_POWER_RAW_IWDG)
               != RESET_POWER_CAUSE_BOR_OR_POR
        || reset_power_normalize(RESET_POWER_RAW_IWDG
                                 | RESET_POWER_RAW_WWDG
                                 | RESET_POWER_RAW_SOFTWARE)
               != RESET_POWER_CAUSE_IWDG
        || reset_power_normalize(RESET_POWER_RAW_WWDG
                                 | RESET_POWER_RAW_SOFTWARE)
               != RESET_POWER_CAUSE_WWDG
        || reset_power_normalize(RESET_POWER_RAW_SOFTWARE
                                 | RESET_POWER_RAW_PIN)
               != RESET_POWER_CAUSE_SOFTWARE
        || reset_power_normalize(RESET_POWER_RAW_PIN)
               != RESET_POWER_CAUSE_PIN
        || reset_power_normalize(0U) != RESET_POWER_CAUSE_UNKNOWN)
    {
        return 1;
    }

    return 0;
}

static int test_capture_records_then_clears_flags(void)
{
    reset_power_service_t service;
    reset_power_status_t status;
    fake_reset_context_t fake = {
        .raw_flags = RESET_POWER_RAW_IWDG | RESET_POWER_RAW_SOFTWARE,
    };
    const reset_power_ops_t ops = {
        .read_raw_flags = fake_read_reset_flags,
        .clear_raw_flags = fake_clear_reset_flags,
        .context = &fake,
    };

    if (reset_power_service_init(&service, &ops) != 0
        || reset_power_service_capture(&service) != 0
        || reset_power_service_get_status(&service, &status) != 0
        || !status.captured
        || status.raw_flags != fake.raw_flags
        || status.cause != RESET_POWER_CAUSE_IWDG
        || fake.read_calls != 1U
        || fake.clear_calls != 1U)
    {
        return 1;
    }

    return 0;
}

static int test_read_failure_does_not_clear_or_publish_status(void)
{
    reset_power_service_t service;
    reset_power_status_t status;
    fake_reset_context_t fake = {
        .raw_flags = RESET_POWER_RAW_PIN,
        .read_result = -4,
    };
    const reset_power_ops_t ops = {
        .read_raw_flags = fake_read_reset_flags,
        .clear_raw_flags = fake_clear_reset_flags,
        .context = &fake,
    };

    if (reset_power_service_init(&service, &ops) != 0
        || reset_power_service_capture(&service) == 0
        || reset_power_service_get_status(&service, &status) != 0
        || status.captured
        || fake.read_calls != 1U
        || fake.clear_calls != 0U)
    {
        return 1;
    }

    return 0;
}

static int test_clear_failure_does_not_publish_status(void)
{
    reset_power_service_t service;
    reset_power_status_t status;
    fake_reset_context_t fake = {
        .raw_flags = RESET_POWER_RAW_PIN,
        .clear_result = -5,
    };
    const reset_power_ops_t ops = {
        .read_raw_flags = fake_read_reset_flags,
        .clear_raw_flags = fake_clear_reset_flags,
        .context = &fake,
    };

    if (reset_power_service_init(&service, &ops) != 0
        || reset_power_service_capture(&service) == 0
        || reset_power_service_get_status(&service, &status) != 0
        || status.captured
        || fake.read_calls != 1U
        || fake.clear_calls != 1U)
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_normalization_priority() != 0
        || test_capture_records_then_clears_flags() != 0
        || test_read_failure_does_not_clear_or_publish_status() != 0
        || test_clear_failure_does_not_publish_status() != 0)
    {
        fputs("reset power service: FAIL\n", stderr);
        return 1;
    }

    puts("reset power service: PASS");
    return 0;
}
