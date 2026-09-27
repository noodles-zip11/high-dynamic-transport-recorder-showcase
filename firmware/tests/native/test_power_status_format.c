#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "power_status_format.h"

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "power status format: %s\n", message);
        return 1;
    }

    return 0;
}

static int expect_contains(const char *text, const char *fragment)
{
    return expect_true(strstr(text, fragment) != NULL, fragment);
}

int main(void)
{
    const power_policy_snapshot_t snapshot = {
        .mode = POWER_MODE_MONITOR,
        .blockers = POWER_BLOCKER_DMA
            | POWER_BLOCKER_EVENT
            | POWER_BLOCKER_OTA_DIAGNOSTIC,
        .sleep_attempt_count = 12U,
        .sleep_enter_count = 8U,
        .sleep_skip_count = 4U,
        .wake_count = 9U,
        .last_wake_reason = POWER_WAKE_IMU_INT1,
        .stop_compiled = RT_FALSE,
        .stop_runtime_allowed = RT_FALSE,
    };
    char buffer[512];
    char small_buffer[16];
    power_policy_snapshot_t unknown_snapshot = snapshot;
    int result;

    result = power_status_format(buffer, sizeof(buffer), &snapshot);
    if (expect_true(result > 0, "formatting a complete status must succeed")
        || expect_contains(buffer, "mode=monitor")
        || expect_contains(buffer, "blockers=0x00000023")
        || expect_contains(buffer, "attempts=12")
        || expect_contains(buffer, "entries=8")
        || expect_contains(buffer, "skips=4")
        || expect_contains(buffer, "wakes=9")
        || expect_contains(buffer, "last_wake=imu_int1")
        || expect_contains(buffer, "stop_compiled=0")
        || expect_contains(buffer, "stop_allowed=0")
        || expect_contains(buffer, "blocker_holds=")
        || expect_contains(buffer, "blocker_acquires=")
        || expect_contains(buffer, "mode_ticks=")
        || expect_true(strchr(buffer, '\n') == NULL, "status must be one line"))
    {
        return 1;
    }

    memset(small_buffer, 0xA5, sizeof(small_buffer));
    result = power_status_format(small_buffer, sizeof(small_buffer), &snapshot);
    if (expect_true(result == -RT_ENOMEM, "small buffers must report ENOMEM")
        || expect_true(small_buffer[sizeof(small_buffer) - 1U] == '\0',
                       "small buffers must remain NUL-terminated"))
    {
        return 1;
    }

    unknown_snapshot.mode = (power_mode_t)99;
    unknown_snapshot.last_wake_reason = (power_wake_reason_t)99;
    result = power_status_format(buffer, sizeof(buffer), &unknown_snapshot);
    if (expect_true(result > 0, "unknown enum values must still format")
        || expect_contains(buffer, "mode=unknown")
        || expect_contains(buffer, "last_wake=unknown"))
    {
        return 1;
    }

    puts("power status format: PASS");
    return 0;
}
