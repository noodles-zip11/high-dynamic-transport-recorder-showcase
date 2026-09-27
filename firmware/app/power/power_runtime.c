#include <rtthread.h>

#include "board_power.h"
#include "power_runtime.h"
#include "power_status_format.h"

static rt_bool_t power_runtime_started;
static rt_bool_t power_runtime_starting;
static rt_bool_t power_cpu_sleep_enabled;
static rt_bool_t power_runtime_tick_initialized;
static rt_tick_t power_runtime_last_tick;
static uint32_t power_runtime_maintenance_leases;
static uint32_t power_runtime_automatic_maintenance_leases;
static rt_bool_t power_runtime_manual_maintenance_lease;
static rt_bool_t power_runtime_event_active_state;

static rt_bool_t power_runtime_is_started(void)
{
    rt_base_t level;
    rt_bool_t result;

    level = rt_hw_interrupt_disable();
    result = power_runtime_started;
    rt_hw_interrupt_enable(level);
    return result;
}

static rt_bool_t power_runtime_sleep_enabled(void)
{
    rt_base_t level;
    rt_bool_t result;

    level = rt_hw_interrupt_disable();
    result = power_cpu_sleep_enabled;
    rt_hw_interrupt_enable(level);
    return result;
}

static void power_runtime_sync_ticks(void)
{
    rt_base_t level;
    rt_tick_t now;
    rt_tick_t elapsed = 0U;
    rt_bool_t record = RT_FALSE;

    level = rt_hw_interrupt_disable();
    now = rt_tick_get();
    if (power_runtime_tick_initialized)
    {
        elapsed = (rt_tick_t)(now - power_runtime_last_tick);
        record = RT_TRUE;
    }
    power_runtime_last_tick = now;
    power_runtime_tick_initialized = RT_TRUE;
    if (record)
    {
        /* Keep mode sampling and elapsed attribution in one critical section. */
        power_policy_record_mode_ticks((uint32_t)elapsed);
    }
    rt_hw_interrupt_enable(level);
}

static rt_err_t power_runtime_recompute_mode_locked(void)
{
    power_policy_snapshot_t snapshot;
    power_mode_t desired;

    power_policy_get_snapshot(&snapshot);
    if (snapshot.mode == POWER_MODE_FAULT_FALLBACK)
    {
        return RT_EOK;
    }
    desired = power_runtime_event_active_state
                  ? POWER_MODE_EVENT_ACTIVE
                  : (power_runtime_maintenance_leases != 0U
                         ? POWER_MODE_MAINTENANCE
                         : POWER_MODE_MONITOR);
    if (snapshot.mode == desired)
    {
        return RT_EOK;
    }
    power_runtime_sync_ticks();
    return power_policy_set_mode(desired);
}

/* Caller holds a short interrupt critical section; no blocking or WFI here. */
static rt_err_t power_runtime_acquire_maintenance_lease_locked(
    rt_bool_t manual,
    rt_bool_t *entered)
{
    power_policy_snapshot_t snapshot;
    rt_err_t result;

    if (entered != RT_NULL)
    {
        *entered = RT_FALSE;
    }
    power_policy_get_snapshot(&snapshot);
    if (snapshot.mode == POWER_MODE_FAULT_FALLBACK
        || snapshot.mode == POWER_MODE_BOOT)
    {
        return -RT_EINVAL;
    }

    if (manual && power_runtime_manual_maintenance_lease)
    {
        return RT_EOK;
    }
    if (snapshot.mode == POWER_MODE_EVENT_ACTIVE
        && power_runtime_maintenance_leases == 0U)
    {
        return -RT_EINVAL;
    }
    if (snapshot.mode != POWER_MODE_MONITOR
        && snapshot.mode != POWER_MODE_MAINTENANCE
        && snapshot.mode != POWER_MODE_EVENT_ACTIVE)
    {
        return -RT_EINVAL;
    }
    if (!manual && snapshot.mode == POWER_MODE_MONITOR
        && power_runtime_maintenance_leases != 0U)
    {
        /* A lease must never be hidden by a stale MONITOR policy snapshot. */
        return -RT_EINVAL;
    }
    if (power_runtime_maintenance_leases == UINT32_MAX)
    {
        return -RT_EBUSY;
    }

    result = power_policy_acquire_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
    if (result != RT_EOK)
    {
        return result;
    }
    ++power_runtime_maintenance_leases;
    if (manual)
    {
        power_runtime_manual_maintenance_lease = RT_TRUE;
    }
    else
    {
        /* Every automatic caller owns one hold and must release that hold. */
        ++power_runtime_automatic_maintenance_leases;
    }
    if (snapshot.mode == POWER_MODE_MONITOR)
    {
        power_runtime_sync_ticks();
        result = power_policy_set_mode(POWER_MODE_MAINTENANCE);
        if (result != RT_EOK)
        {
            if (manual)
            {
                power_runtime_manual_maintenance_lease = RT_FALSE;
            }
            else
            {
                --power_runtime_automatic_maintenance_leases;
            }
            --power_runtime_maintenance_leases;
            power_policy_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
            return result;
        }
    }
    if (entered != RT_NULL && !manual)
    {
        *entered = RT_TRUE;
    }
    return RT_EOK;
}

/* Caller holds a short interrupt critical section; nested policy locks restore it. */
static rt_err_t power_runtime_release_maintenance_lease_locked(rt_bool_t manual)
{
    if (manual)
    {
        if (!power_runtime_manual_maintenance_lease)
        {
            return RT_EOK;
        }
        power_runtime_manual_maintenance_lease = RT_FALSE;
    }
    else
    {
        if (power_runtime_automatic_maintenance_leases == 0U)
        {
            return RT_EOK;
        }
        --power_runtime_automatic_maintenance_leases;
    }
    if (power_runtime_maintenance_leases != 0U)
    {
        --power_runtime_maintenance_leases;
    }
    power_policy_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
    return power_runtime_recompute_mode_locked();
}

/* Fault is terminal; clear maintenance-owned leases and their blocker holds. */
static void power_runtime_clear_maintenance_locked(void)
{
    while (power_runtime_maintenance_leases != 0U)
    {
        --power_runtime_maintenance_leases;
        power_policy_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
    }
    power_runtime_automatic_maintenance_leases = 0U;
    power_runtime_manual_maintenance_lease = RT_FALSE;
}

/* Caller holds a short interrupt critical section; no blocking, print, or WFI. */
static rt_err_t power_runtime_event_active_locked(rt_bool_t active)
{
    power_policy_snapshot_t snapshot;
    power_mode_t desired;
    rt_err_t result;

    power_policy_get_snapshot(&snapshot);
    if (active)
    {
        if (snapshot.mode == POWER_MODE_FAULT_FALLBACK)
        {
            return -RT_EBUSY;
        }
        if (snapshot.mode == POWER_MODE_BOOT)
        {
            return -RT_EINVAL;
        }
        if (power_runtime_event_active_state)
        {
            return RT_EOK;
        }
        result = power_policy_acquire_blocker(POWER_BLOCKER_EVENT);
        if (result != RT_EOK)
        {
            return result;
        }
        power_runtime_sync_ticks();
        result = power_policy_set_mode(POWER_MODE_EVENT_ACTIVE);
        if (result != RT_EOK)
        {
            power_policy_release_blocker(POWER_BLOCKER_EVENT);
            return result;
        }
        power_runtime_event_active_state = RT_TRUE;
        return RT_EOK;
    }

    if (snapshot.mode == POWER_MODE_FAULT_FALLBACK)
    {
        if (power_runtime_event_active_state)
        {
            power_policy_release_blocker(POWER_BLOCKER_EVENT);
        }
        power_runtime_event_active_state = RT_FALSE;
        return RT_EOK;
    }
    desired = power_runtime_maintenance_leases != 0U
                  ? POWER_MODE_MAINTENANCE
                  : POWER_MODE_MONITOR;
    if (snapshot.mode == POWER_MODE_EVENT_ACTIVE)
    {
        power_runtime_sync_ticks();
        result = power_policy_set_mode(desired);
        if (result != RT_EOK)
        {
            return result;
        }
    }
    if (power_runtime_event_active_state)
    {
        power_policy_release_blocker(POWER_BLOCKER_EVENT);
    }
    power_runtime_event_active_state = RT_FALSE;
    return RT_EOK;
}

/* All public mode changes enter here with interrupts already disabled. */
static rt_err_t power_runtime_set_mode_locked(power_mode_t mode)
{
    rt_err_t result;

    if (mode == POWER_MODE_MAINTENANCE)
    {
        return power_runtime_acquire_maintenance_lease_locked(RT_TRUE, RT_NULL);
    }
    if (mode == POWER_MODE_EVENT_ACTIVE)
    {
        return power_runtime_event_active_locked(RT_TRUE);
    }

    if (mode == POWER_MODE_FAULT_FALLBACK)
    {
        power_runtime_sync_ticks();
        result = power_policy_set_mode(mode);
        if (result == RT_EOK)
        {
            power_runtime_clear_maintenance_locked();
        }
        return result;
    }
    if (power_runtime_maintenance_leases != 0U
        || power_runtime_event_active_state)
    {
        return -RT_EINVAL;
    }
    power_runtime_sync_ticks();
    return power_policy_set_mode(mode);
}

static void power_idle_hook(void)
{
    rt_bool_t allowed;

    power_runtime_sync_ticks();
    allowed = power_runtime_sleep_enabled()
              && power_policy_can_cpu_sleep();

    power_policy_record_sleep_attempt(allowed);
    if (!allowed)
    {
        return;
    }

    /* Keep the window closed to thread switches while WFI still accepts IRQs. */
    rt_enter_critical();
    power_policy_begin_sleep_window();
    board_power_cpu_sleep();
    power_policy_finish_sleep_window();
    rt_exit_critical();
}

rt_err_t power_runtime_start(void)
{
    rt_err_t result;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }
    if (power_runtime_starting)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    power_runtime_starting = RT_TRUE;
    rt_hw_interrupt_enable(level);

    power_policy_init();
    level = rt_hw_interrupt_disable();
    power_cpu_sleep_enabled = RT_TRUE;
    power_runtime_maintenance_leases = 0U;
    power_runtime_automatic_maintenance_leases = 0U;
    power_runtime_manual_maintenance_lease = RT_FALSE;
    power_runtime_event_active_state = RT_FALSE;
    power_runtime_tick_initialized = RT_FALSE;
    power_runtime_last_tick = rt_tick_get();
    power_runtime_tick_initialized = RT_TRUE;
    rt_hw_interrupt_enable(level);

    /*
     * Registration is deliberately outside the critical section.  A failed
     * registration leaves started=false so a later caller can retry.
     */
    result = rt_thread_idle_sethook(power_idle_hook);
    level = rt_hw_interrupt_disable();
    power_runtime_starting = RT_FALSE;
    if (result == RT_EOK)
    {
        power_runtime_started = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (result != RT_EOK)
    {
        (void)power_policy_set_mode(POWER_MODE_FAULT_FALLBACK);
    }
    return result;
}

rt_err_t power_runtime_set_mode(power_mode_t mode)
{
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_set_mode_locked(mode);
    rt_hw_interrupt_enable(level);
    return result;
}

void power_runtime_set_blocker(power_blocker_t blocker, rt_bool_t active)
{
    power_runtime_sync_ticks();
    power_policy_set_blocker(blocker, active);
}

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    if (!power_runtime_is_started())
    {
        return -RT_ERROR;
    }
    power_runtime_sync_ticks();
    return power_policy_acquire_blocker(blocker);
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    power_runtime_sync_ticks();
    power_policy_release_blocker(blocker);
}

rt_err_t power_runtime_enter_maintenance(void)
{
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_acquire_maintenance_lease_locked(RT_TRUE, RT_NULL);
    rt_hw_interrupt_enable(level);
    return result;
}

rt_err_t power_runtime_enter_maintenance_if_monitor(rt_bool_t *entered)
{
    rt_base_t level;
    rt_err_t result;

    if (entered == RT_NULL)
    {
        return -RT_EINVAL;
    }
    *entered = RT_FALSE;
    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_acquire_maintenance_lease_locked(RT_FALSE, entered);
    rt_hw_interrupt_enable(level);
    return result;
}

rt_err_t power_runtime_exit_maintenance(void)
{
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_release_maintenance_lease_locked(RT_TRUE);
    rt_hw_interrupt_enable(level);
    return result;
}

rt_err_t power_runtime_exit_maintenance_if_owned(void)
{
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_release_maintenance_lease_locked(RT_FALSE);
    rt_hw_interrupt_enable(level);
    return result;
}

rt_err_t power_runtime_event_active(rt_bool_t active)
{
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    if (!power_runtime_started)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    result = power_runtime_event_active_locked(active);
    rt_hw_interrupt_enable(level);
    return result;
}

void power_runtime_record_wake(power_wake_reason_t reason)
{
    power_policy_record_wake(reason);
}

void power_runtime_get_snapshot(power_policy_snapshot_t *snapshot)
{
    if (power_runtime_is_started())
    {
        power_runtime_sync_ticks();
    }
    power_policy_get_snapshot(snapshot);
}

int power(int argc, char **argv)
{
    power_policy_snapshot_t snapshot;
    char status[512];

    if (argc == 2 && rt_strcmp(argv[1], "status") == 0)
    {
        power_runtime_get_snapshot(&snapshot);
        if (power_status_format(status, sizeof(status), &snapshot) < 0)
        {
            return -RT_ERROR;
        }

        rt_kprintf("%s\n", status);
        return RT_EOK;
    }

    if (argc == 3 && rt_strcmp(argv[1], "sleep") == 0)
    {
        rt_base_t level = rt_hw_interrupt_disable();

        if (rt_strcmp(argv[2], "on") == 0)
        {
            power_cpu_sleep_enabled = RT_TRUE;
            rt_hw_interrupt_enable(level);
            rt_kprintf("cpu sleep=on\n");
            return RT_EOK;
        }
        if (rt_strcmp(argv[2], "off") == 0)
        {
            power_cpu_sleep_enabled = RT_FALSE;
            rt_hw_interrupt_enable(level);
            rt_kprintf("cpu sleep=off\n");
            return RT_EOK;
        }
        rt_hw_interrupt_enable(level);
    }

    if (argc == 3 && rt_strcmp(argv[1], "maintenance") == 0)
    {
        if (rt_strcmp(argv[2], "on") == 0)
        {
            return power_runtime_enter_maintenance();
        }
        if (rt_strcmp(argv[2], "off") == 0)
        {
            return power_runtime_exit_maintenance();
        }
    }

    rt_kprintf("usage: power status | power sleep on|off | power maintenance on|off\n");
    return -RT_EINVAL;
}

MSH_CMD_EXPORT(power, power status; power sleep on|off; power maintenance on|off);
