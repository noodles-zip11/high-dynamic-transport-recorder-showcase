#include <stdio.h>

#include "imu_dma_power_guard.h"
#include "power_runtime.h"

static rt_err_t test_acquire_result;
static uint32_t test_acquire_count;
static uint32_t test_release_count;

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    if (blocker != POWER_BLOCKER_DMA)
    {
        return -RT_ERROR;
    }
    test_acquire_count++;
    return test_acquire_result;
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    if (blocker == POWER_BLOCKER_DMA)
    {
        test_release_count++;
    }
}

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "imu dma power guard: %s\n", message);
        return 0;
    }
    return 1;
}

static int test_success_releases_once(void)
{
    imu_dma_power_guard_t guard = {0};

    test_acquire_result = RT_EOK;
    test_acquire_count = 0U;
    test_release_count = 0U;
    if (!expect(imu_dma_power_guard_acquire(&guard) == RT_EOK,
                "successful DMA blocker acquire must succeed")
        || !expect(guard.held == RT_TRUE && test_acquire_count == 1U,
                   "successful acquire must mark the guard held"))
    {
        return 1;
    }

    imu_dma_power_guard_release(&guard);
    imu_dma_power_guard_release(&guard);
    return expect(guard.held == RT_FALSE && test_release_count == 1U,
                  "success, timeout, abort, and completion cleanup must be idempotent")
           ? 0 : 1;
}

static int test_failed_acquire_does_not_release(void)
{
    imu_dma_power_guard_t guard = {0};

    test_acquire_result = -RT_ERROR;
    test_acquire_count = 0U;
    test_release_count = 0U;
    if (!expect(imu_dma_power_guard_acquire(&guard) != RT_EOK,
                "failed DMA blocker acquire must be reported")
        || !expect(guard.held == RT_FALSE && test_acquire_count == 1U,
                   "failed acquire must not mark the guard held"))
    {
        return 1;
    }

    imu_dma_power_guard_release(&guard);
    return expect(test_release_count == 0U,
                  "failed acquire must not release an unowned blocker")
           ? 0 : 1;
}

int main(void)
{
    if (test_success_releases_once() != 0
        || test_failed_acquire_does_not_release() != 0)
    {
        return 1;
    }

    puts("imu dma power guard: PASS");
    return 0;
}
