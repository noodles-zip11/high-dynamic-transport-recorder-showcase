#include "power_policy.h"

typedef struct
{
    rt_bool_t initialized;
    power_mode_t mode;
    uint32_t blockers;
    uint32_t sleep_attempt_count;
    uint32_t sleep_enter_count;
    uint32_t sleep_skip_count;
    uint32_t wake_count;
    power_wake_reason_t last_wake_reason;
    power_blocker_stats_t blocker_stats[POWER_BLOCKER_COUNT];
    uint32_t mode_ticks[POWER_MODE_COUNT];
    rt_bool_t sleep_window_active;
    rt_bool_t wake_recorded_since_sleep;
} power_policy_state_t;

static power_policy_state_t power_policy_state;

static const uint32_t power_blocker_masks[POWER_BLOCKER_COUNT] = {
    POWER_BLOCKER_DMA,
    POWER_BLOCKER_EVENT,
    POWER_BLOCKER_STORAGE,
    POWER_BLOCKER_AI,
    POWER_BLOCKER_TRANSPORT,
    POWER_BLOCKER_OTA_DIAGNOSTIC,
};

static rt_bool_t power_mode_is_valid(power_mode_t mode)
{
    const int value = (int)mode;

    return value >= (int)POWER_MODE_BOOT
        && value <= (int)POWER_MODE_FAULT_FALLBACK;
}

static rt_bool_t power_wake_reason_is_valid(power_wake_reason_t reason)
{
    const int value = (int)reason;

    return value >= (int)POWER_WAKE_UNKNOWN
        && value <= (int)POWER_WAKE_UART;
}

static int power_mode_index(power_mode_t mode)
{
    return power_mode_is_valid(mode) ? (int)mode : -1;
}

static int power_blocker_index(power_blocker_t blocker)
{
    uint32_t index;
    const uint32_t mask = (uint32_t)blocker;

    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        if (mask == power_blocker_masks[index])
        {
            return (int)index;
        }
    }
    return -1;
}

static uint32_t power_policy_known_blockers(void)
{
    uint32_t index;
    uint32_t known = 0U;

    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        known |= power_blocker_masks[index];
    }
    return known;
}

static void power_policy_increment(uint32_t *counter)
{
    if (*counter < UINT32_MAX)
    {
        ++(*counter);
    }
}

static void power_policy_add(uint32_t *counter, uint32_t amount)
{
    if (UINT32_MAX - *counter < amount)
    {
        *counter = UINT32_MAX;
    }
    else
    {
        *counter += amount;
    }
}

static rt_bool_t power_mode_transition_allowed(power_mode_t from,
                                                power_mode_t to)
{
    if (from == to)
    {
        return RT_TRUE;
    }
    if (from == POWER_MODE_FAULT_FALLBACK)
    {
        return RT_FALSE;
    }
    if (to == POWER_MODE_FAULT_FALLBACK)
    {
        return RT_TRUE;
    }

    switch (from)
    {
    case POWER_MODE_BOOT:
        return to == POWER_MODE_MONITOR || to == POWER_MODE_MAINTENANCE;
    case POWER_MODE_MONITOR:
        return to == POWER_MODE_EVENT_ACTIVE || to == POWER_MODE_MAINTENANCE;
    case POWER_MODE_EVENT_ACTIVE:
        return to == POWER_MODE_MONITOR || to == POWER_MODE_MAINTENANCE;
    case POWER_MODE_MAINTENANCE:
        return to == POWER_MODE_MONITOR || to == POWER_MODE_EVENT_ACTIVE;
    default:
        return RT_FALSE;
    }
}

static rt_err_t power_policy_acquire_mask_locked(uint32_t mask)
{
    uint32_t index;

    if (!power_policy_state.initialized
        || mask == 0U
        || (mask & ~power_policy_known_blockers()) != 0U)
    {
        return -RT_EINVAL;
    }

    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        if ((mask & power_blocker_masks[index]) != 0U)
        {
            power_blocker_stats_t *stats = &power_policy_state.blocker_stats[index];

            power_policy_increment(&stats->acquire_count);
            power_policy_increment(&stats->hold_count);
            power_policy_state.blockers |= power_blocker_masks[index];
        }
    }
    return RT_EOK;
}

static void power_policy_release_mask_locked(uint32_t mask)
{
    uint32_t index;

    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        if ((mask & power_blocker_masks[index]) != 0U)
        {
            power_blocker_stats_t *stats = &power_policy_state.blocker_stats[index];

            if (stats->hold_count != 0U)
            {
                --stats->hold_count;
                if (stats->hold_count == 0U)
                {
                    power_policy_state.blockers &= ~power_blocker_masks[index];
                }
            }
        }
    }
}

void power_policy_init(void)
{
    rt_base_t level;
    uint32_t index;

    level = rt_hw_interrupt_disable();
    power_policy_state.initialized = RT_TRUE;
    power_policy_state.mode = POWER_MODE_BOOT;
    power_policy_state.blockers = 0U;
    power_policy_state.sleep_attempt_count = 0U;
    power_policy_state.sleep_enter_count = 0U;
    power_policy_state.sleep_skip_count = 0U;
    power_policy_state.wake_count = 0U;
    power_policy_state.last_wake_reason = POWER_WAKE_UNKNOWN;
    power_policy_state.sleep_window_active = RT_FALSE;
    power_policy_state.wake_recorded_since_sleep = RT_FALSE;
    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        power_policy_state.blocker_stats[index].hold_count = 0U;
        power_policy_state.blocker_stats[index].acquire_count = 0U;
    }
    for (index = 0U; index < POWER_MODE_COUNT; index++)
    {
        power_policy_state.mode_ticks[index] = 0U;
    }
    rt_hw_interrupt_enable(level);
}

rt_err_t power_policy_set_mode(power_mode_t mode)
{
    rt_base_t level;
    power_mode_t current;

    if (!power_mode_is_valid(mode))
    {
        return -RT_EINVAL;
    }

    level = rt_hw_interrupt_disable();
    if (!power_policy_state.initialized)
    {
        rt_hw_interrupt_enable(level);
        return -RT_EINVAL;
    }
    current = power_policy_state.mode;
    if (!power_mode_transition_allowed(current, mode))
    {
        rt_hw_interrupt_enable(level);
        return current == POWER_MODE_FAULT_FALLBACK ? -RT_EBUSY : -RT_EINVAL;
    }
    power_policy_state.mode = mode;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

void power_policy_set_blocker(power_blocker_t blocker, rt_bool_t active)
{
    rt_base_t level;
    uint32_t mask = (uint32_t)blocker & power_policy_known_blockers();

    if (mask == 0U)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        if (active)
        {
            uint32_t index;

            for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
            {
                if ((mask & power_blocker_masks[index]) != 0U
                    && power_policy_state.blocker_stats[index].hold_count == 0U)
                {
                    (void)power_policy_acquire_mask_locked(
                        power_blocker_masks[index]);
                }
            }
        }
        else
        {
            power_policy_release_mask_locked(mask);
        }
    }
    rt_hw_interrupt_enable(level);
}

rt_err_t power_policy_acquire_blocker(power_blocker_t blocker)
{
    const uint32_t mask = (uint32_t)blocker;
    rt_base_t level;
    rt_err_t result;

    level = rt_hw_interrupt_disable();
    result = power_policy_acquire_mask_locked(mask);
    rt_hw_interrupt_enable(level);
    return result;
}

void power_policy_release_blocker(power_blocker_t blocker)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        power_policy_release_mask_locked((uint32_t)blocker
                                         & power_policy_known_blockers());
    }
    rt_hw_interrupt_enable(level);
}

uint32_t power_policy_get_blocker_hold_count(power_blocker_t blocker)
{
    const int index = power_blocker_index(blocker);
    rt_base_t level;
    uint32_t result = 0U;

    if (index < 0)
    {
        return 0U;
    }
    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        result = power_policy_state.blocker_stats[index].hold_count;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

uint32_t power_policy_get_blocker_acquire_count(power_blocker_t blocker)
{
    const int index = power_blocker_index(blocker);
    rt_base_t level;
    uint32_t result = 0U;

    if (index < 0)
    {
        return 0U;
    }
    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        result = power_policy_state.blocker_stats[index].acquire_count;
    }
    rt_hw_interrupt_enable(level);
    return result;
}

void power_policy_record_mode_ticks(uint32_t ticks)
{
    rt_base_t level;
    int index;

    level = rt_hw_interrupt_disable();
    index = power_mode_index(power_policy_state.mode);
    if (power_policy_state.initialized && index >= 0)
    {
        power_policy_add(&power_policy_state.mode_ticks[index], ticks);
    }
    rt_hw_interrupt_enable(level);
}

uint32_t power_policy_get_mode_ticks(power_mode_t mode)
{
    const int index = power_mode_index(mode);
    rt_base_t level;
    uint32_t result = 0U;

    if (index < 0)
    {
        return 0U;
    }
    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        result = power_policy_state.mode_ticks[index];
    }
    rt_hw_interrupt_enable(level);
    return result;
}

rt_bool_t power_policy_can_cpu_sleep(void)
{
    rt_base_t level;
    rt_bool_t result;

    level = rt_hw_interrupt_disable();
    result = power_policy_state.initialized
             && power_mode_is_valid(power_policy_state.mode)
             && power_policy_state.mode != POWER_MODE_BOOT;
    rt_hw_interrupt_enable(level);
    return result;
}

rt_bool_t power_policy_can_stop(void)
{
    return RT_FALSE;
}

void power_policy_record_sleep_attempt(rt_bool_t entered)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        power_policy_increment(&power_policy_state.sleep_attempt_count);
        if (entered)
        {
            power_policy_increment(&power_policy_state.sleep_enter_count);
        }
        else
        {
            power_policy_increment(&power_policy_state.sleep_skip_count);
        }
    }
    rt_hw_interrupt_enable(level);
}

void power_policy_record_wake(power_wake_reason_t reason)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized
        && power_policy_state.sleep_window_active
        && !power_policy_state.wake_recorded_since_sleep
        && reason != POWER_WAKE_UNKNOWN
        && power_wake_reason_is_valid(reason))
    {
        /* The first concrete ISR owns this WFI's wake attribution. */
        power_policy_increment(&power_policy_state.wake_count);
        power_policy_state.last_wake_reason = reason;
        power_policy_state.wake_recorded_since_sleep = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);
}

void power_policy_begin_sleep_window(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized)
    {
        power_policy_state.sleep_window_active = RT_TRUE;
        power_policy_state.wake_recorded_since_sleep = RT_FALSE;
    }
    rt_hw_interrupt_enable(level);
}

void power_policy_finish_sleep_window(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (power_policy_state.initialized && power_policy_state.sleep_window_active)
    {
        if (!power_policy_state.wake_recorded_since_sleep)
        {
            power_policy_increment(&power_policy_state.wake_count);
            power_policy_state.last_wake_reason = POWER_WAKE_UNKNOWN;
        }
        power_policy_state.sleep_window_active = RT_FALSE;
        power_policy_state.wake_recorded_since_sleep = RT_FALSE;
    }
    rt_hw_interrupt_enable(level);
}

void power_policy_get_snapshot(power_policy_snapshot_t *snapshot)
{
    rt_base_t level;
    uint32_t index;

    if (snapshot == RT_NULL)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    snapshot->mode = power_policy_state.mode;
    snapshot->blockers = power_policy_state.blockers;
    snapshot->sleep_attempt_count = power_policy_state.sleep_attempt_count;
    snapshot->sleep_enter_count = power_policy_state.sleep_enter_count;
    snapshot->sleep_skip_count = power_policy_state.sleep_skip_count;
    snapshot->wake_count = power_policy_state.wake_count;
    snapshot->last_wake_reason = power_policy_state.last_wake_reason;
    snapshot->stop_compiled = RT_FALSE;
    snapshot->stop_runtime_allowed = RT_FALSE;
    for (index = 0U; index < POWER_BLOCKER_COUNT; index++)
    {
        snapshot->blocker_stats[index] = power_policy_state.blocker_stats[index];
    }
    for (index = 0U; index < POWER_MODE_COUNT; index++)
    {
        snapshot->mode_ticks[index] = power_policy_state.mode_ticks[index];
    }
    rt_hw_interrupt_enable(level);
}
