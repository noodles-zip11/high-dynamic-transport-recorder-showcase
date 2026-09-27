#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "time_service.h"

typedef struct
{
    int64_t rtc_utc_seconds;
    uint32_t backup_marker;
    uint64_t monotonic_us;
    bool calendar_valid;
    int get_result;
    int set_result;
    int calendar_valid_result;
    int backup_read_result;
    int backup_write_result;
    unsigned int get_calls;
    unsigned int set_calls;
    unsigned int backup_write_calls;
} fake_time_context_t;

static int fake_rtc_get_utc(int64_t *utc_seconds, void *context)
{
    fake_time_context_t *fake = context;

    fake->get_calls++;
    if (fake->get_result != 0)
    {
        return fake->get_result;
    }

    *utc_seconds = fake->rtc_utc_seconds;
    return 0;
}

static int fake_rtc_set_utc(int64_t utc_seconds, void *context)
{
    fake_time_context_t *fake = context;

    fake->set_calls++;
    if (fake->set_result != 0)
    {
        return fake->set_result;
    }

    fake->rtc_utc_seconds = utc_seconds;
    return 0;
}

static int fake_rtc_calendar_valid(bool *calendar_valid, void *context)
{
    fake_time_context_t *fake = context;

    if (fake->calendar_valid_result != 0)
    {
        return fake->calendar_valid_result;
    }

    *calendar_valid = fake->calendar_valid;
    return 0;
}

static int fake_backup_read(uint32_t *marker, void *context)
{
    fake_time_context_t *fake = context;

    if (fake->backup_read_result != 0)
    {
        return fake->backup_read_result;
    }

    *marker = fake->backup_marker;
    return 0;
}

static int fake_backup_write(uint32_t marker, void *context)
{
    fake_time_context_t *fake = context;

    fake->backup_write_calls++;
    if (fake->backup_write_result != 0)
    {
        return fake->backup_write_result;
    }

    fake->backup_marker = marker;
    return 0;
}

static uint64_t fake_monotonic_us(void *context)
{
    const fake_time_context_t *fake = context;

    return fake->monotonic_us;
}

static time_service_ops_t fake_ops(fake_time_context_t *context)
{
    const time_service_ops_t ops = {
        .rtc_get_utc = fake_rtc_get_utc,
        .rtc_set_utc = fake_rtc_set_utc,
        .rtc_calendar_valid = fake_rtc_calendar_valid,
        .backup_read = fake_backup_read,
        .backup_write = fake_backup_write,
        .monotonic_us = fake_monotonic_us,
        .context = context,
    };

    return ops;
}

static int test_boot_validity_requires_calendar_and_marker(void)
{
    time_service_t service;
    time_service_status_t status;
    fake_time_context_t fake = {
        .rtc_utc_seconds = INT64_C(1710000000),
        .backup_marker = TIME_SERVICE_BACKUP_MARKER,
        .monotonic_us = UINT64_C(1234567),
        .calendar_valid = true,
    };
    const time_service_ops_t ops = fake_ops(&fake);

    if (time_service_init(&service, &ops) != 0
        || time_service_get_status(&service, &status) != 0
        || !status.utc_valid
        || status.utc_unix_seconds != INT64_C(1710000000)
        || status.monotonic_us != UINT64_C(1234567)
        || status.epoch_id != 0U)
    {
        return 1;
    }

    fake.rtc_utc_seconds = INT64_C(1710000001);
    fake.monotonic_us = UINT64_C(2234567);
    if (time_service_get_status(&service, &status) != 0
        || status.utc_unix_seconds != INT64_C(1710000001)
        || status.monotonic_us != UINT64_C(2234567))
    {
        return 1;
    }

    return 0;
}

static int test_invalid_boot_does_not_invent_utc(void)
{
    time_service_t service;
    time_service_status_t status;
    fake_time_context_t fake = {
        .rtc_utc_seconds = INT64_C(1710000000),
        .backup_marker = TIME_SERVICE_BACKUP_INVALID_MARKER,
        .monotonic_us = UINT64_C(99),
        .calendar_valid = true,
    };
    const time_service_ops_t ops = fake_ops(&fake);

    if (time_service_init(&service, &ops) != 0
        || time_service_get_status(&service, &status) != 0
        || status.utc_valid
        || status.utc_unix_seconds != 0
        || status.monotonic_us != UINT64_C(99)
        || fake.get_calls != 0U)
    {
        return 1;
    }

    return 0;
}

static int test_set_marks_utc_valid_and_advances_epoch(void)
{
    time_service_t service;
    time_service_status_t status;
    fake_time_context_t fake = {
        .backup_marker = TIME_SERVICE_BACKUP_INVALID_MARKER,
        .calendar_valid = false,
    };
    const time_service_ops_t ops = fake_ops(&fake);

    if (time_service_init(&service, &ops) != 0
        || time_service_set_utc(&service, INT64_C(1710000000)) != 0
        || fake.rtc_utc_seconds != INT64_C(1710000000)
        || fake.backup_marker != TIME_SERVICE_BACKUP_MARKER
        || fake.set_calls != 1U
        || fake.backup_write_calls != 1U
        || time_service_get_status(&service, &status) != 0
        || !status.utc_valid
        || status.epoch_id != 1U)
    {
        return 1;
    }

    if (time_service_set_utc(&service, INT64_C(1710000100)) != 0
        || time_service_get_status(&service, &status) != 0
        || status.utc_unix_seconds != INT64_C(1710000100)
        || status.epoch_id != 2U)
    {
        return 1;
    }

    return 0;
}

static int test_invalidate_clears_marker_without_changing_calendar(void)
{
    time_service_t service;
    time_service_status_t status;
    fake_time_context_t fake = {
        .rtc_utc_seconds = INT64_C(1710000000),
        .backup_marker = TIME_SERVICE_BACKUP_MARKER,
        .calendar_valid = true,
    };
    const time_service_ops_t ops = fake_ops(&fake);

    if (time_service_init(&service, &ops) != 0
        || time_service_invalidate(&service) != 0
        || fake.backup_marker != TIME_SERVICE_BACKUP_INVALID_MARKER
        || !fake.calendar_valid
        || fake.set_calls != 0U
        || time_service_get_status(&service, &status) != 0
        || status.utc_valid
        || status.utc_unix_seconds != 0)
    {
        return 1;
    }

    return 0;
}

static int test_set_failure_preserves_invalid_state(void)
{
    time_service_t service;
    time_service_status_t status;
    fake_time_context_t fake = {
        .backup_marker = TIME_SERVICE_BACKUP_INVALID_MARKER,
        .calendar_valid = false,
        .set_result = -7,
    };
    const time_service_ops_t ops = fake_ops(&fake);

    if (time_service_init(&service, &ops) != 0
        || time_service_set_utc(&service, INT64_C(1710000000)) == 0
        || fake.backup_write_calls != 0U
        || time_service_get_status(&service, &status) != 0
        || status.utc_valid
        || status.epoch_id != 0U)
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_boot_validity_requires_calendar_and_marker() != 0
        || test_invalid_boot_does_not_invent_utc() != 0
        || test_set_marks_utc_valid_and_advances_epoch() != 0
        || test_invalidate_clears_marker_without_changing_calendar() != 0
        || test_set_failure_preserves_invalid_state() != 0)
    {
        fputs("time service: FAIL\n", stderr);
        return 1;
    }

    puts("time service: PASS");
    return 0;
}
