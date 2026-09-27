#include "power_status_format.h"

static const char *power_mode_name(power_mode_t mode)
{
    switch (mode)
    {
    case POWER_MODE_BOOT:
        return "boot";
    case POWER_MODE_MONITOR:
        return "monitor";
    case POWER_MODE_EVENT_ACTIVE:
        return "event_active";
    case POWER_MODE_MAINTENANCE:
        return "maintenance";
    case POWER_MODE_FAULT_FALLBACK:
        return "fault_fallback";
    default:
        return "unknown";
    }
}

static const char *power_wake_reason_name(power_wake_reason_t reason)
{
    switch (reason)
    {
    case POWER_WAKE_UNKNOWN:
        return "unknown";
    case POWER_WAKE_SYSTICK:
        return "systick";
    case POWER_WAKE_IMU_INT1:
        return "imu_int1";
    case POWER_WAKE_DMA:
        return "dma";
    case POWER_WAKE_UART:
        return "uart";
    default:
        return "unknown";
    }
}

int power_status_format(char *buffer,
                        rt_size_t capacity,
                        const power_policy_snapshot_t *snapshot)
{
    int result;

    if (buffer == RT_NULL || snapshot == RT_NULL || capacity == 0U)
    {
        return -RT_EINVAL;
    }

    result = rt_snprintf(buffer,
                         capacity,
                         "mode=%s blockers=0x%08x attempts=%u entries=%u skips=%u "
                         "wakes=%u last_wake=%s stop_compiled=%d stop_allowed=%d "
                         "blocker_holds=[%u,%u,%u,%u,%u,%u] "
                         "blocker_acquires=[%u,%u,%u,%u,%u,%u] "
                         "mode_ticks=[%u,%u,%u,%u,%u]",
                         power_mode_name(snapshot->mode),
                         (unsigned int)snapshot->blockers,
                         (unsigned int)snapshot->sleep_attempt_count,
                         (unsigned int)snapshot->sleep_enter_count,
                         (unsigned int)snapshot->sleep_skip_count,
                         (unsigned int)snapshot->wake_count,
                         power_wake_reason_name(snapshot->last_wake_reason),
                         snapshot->stop_compiled,
                         snapshot->stop_runtime_allowed,
                         (unsigned int)snapshot->blocker_stats[0].hold_count,
                         (unsigned int)snapshot->blocker_stats[1].hold_count,
                         (unsigned int)snapshot->blocker_stats[2].hold_count,
                         (unsigned int)snapshot->blocker_stats[3].hold_count,
                         (unsigned int)snapshot->blocker_stats[4].hold_count,
                         (unsigned int)snapshot->blocker_stats[5].hold_count,
                         (unsigned int)snapshot->blocker_stats[0].acquire_count,
                         (unsigned int)snapshot->blocker_stats[1].acquire_count,
                         (unsigned int)snapshot->blocker_stats[2].acquire_count,
                         (unsigned int)snapshot->blocker_stats[3].acquire_count,
                         (unsigned int)snapshot->blocker_stats[4].acquire_count,
                         (unsigned int)snapshot->blocker_stats[5].acquire_count,
                         (unsigned int)snapshot->mode_ticks[0],
                         (unsigned int)snapshot->mode_ticks[1],
                         (unsigned int)snapshot->mode_ticks[2],
                         (unsigned int)snapshot->mode_ticks[3],
                         (unsigned int)snapshot->mode_ticks[4]);

    if (result < 0)
    {
        buffer[capacity - 1U] = '\0';
        return -RT_ERROR;
    }

    if ((rt_size_t)result >= capacity)
    {
        buffer[capacity - 1U] = '\0';
        return -RT_ENOMEM;
    }

    return result;
}
