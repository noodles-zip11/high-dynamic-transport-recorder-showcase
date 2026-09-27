#include "monotonic_clock.h"

#include <limits.h>
#include <stddef.h>

#include <rtthread.h>

#define MONOTONIC_CLOCK_WRAP_TICKS (UINT64_C(1) << 32)
#define MICROSECONDS_PER_SECOND UINT64_C(1000000)

static monotonic_clock_t system_monotonic_clock;

bool monotonic_clock_observe(monotonic_clock_t *clock,
                             uint32_t tick,
                             uint32_t ticks_per_second,
                             uint64_t *monotonic_us)
{
    uint64_t epoch_ticks;
    uint64_t extended_ticks;
    uint64_t seconds;
    uint64_t remainder_us;

    if (clock == NULL || ticks_per_second == 0U || monotonic_us == NULL)
    {
        return false;
    }

    epoch_ticks = clock->epoch_ticks;
    if (clock->initialized && tick < clock->last_tick)
    {
        if (epoch_ticks > UINT64_MAX - MONOTONIC_CLOCK_WRAP_TICKS)
        {
            return false;
        }
        epoch_ticks += MONOTONIC_CLOCK_WRAP_TICKS;
    }

    if (epoch_ticks > UINT64_MAX - tick)
    {
        return false;
    }
    extended_ticks = epoch_ticks + tick;
    seconds = extended_ticks / ticks_per_second;
    remainder_us = ((extended_ticks % ticks_per_second)
                    * MICROSECONDS_PER_SECOND) / ticks_per_second;
    if (seconds > (UINT64_MAX - remainder_us) / MICROSECONDS_PER_SECOND)
    {
        return false;
    }

    clock->last_tick = tick;
    clock->epoch_ticks = epoch_ticks;
    clock->initialized = true;
    *monotonic_us = seconds * MICROSECONDS_PER_SECOND + remainder_us;
    return true;
}

uint64_t monotonic_clock_now_us(void)
{
    rt_base_t level;
    uint64_t monotonic_us = 0U;

    level = rt_hw_interrupt_disable();
    (void)monotonic_clock_observe(&system_monotonic_clock, rt_tick_get(),
                                  RT_TICK_PER_SECOND, &monotonic_us);
    rt_hw_interrupt_enable(level);
    return monotonic_us;
}
