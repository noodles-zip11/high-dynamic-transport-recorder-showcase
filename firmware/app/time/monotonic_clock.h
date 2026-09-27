#ifndef MONOTONIC_CLOCK_H
#define MONOTONIC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t last_tick;
    uint64_t epoch_ticks;
    bool initialized;
} monotonic_clock_t;

bool monotonic_clock_observe(monotonic_clock_t *clock,
                             uint32_t tick,
                             uint32_t ticks_per_second,
                             uint64_t *monotonic_us);
uint64_t monotonic_clock_now_us(void);

#endif
