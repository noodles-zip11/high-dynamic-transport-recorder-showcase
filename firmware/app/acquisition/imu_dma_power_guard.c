#include "imu_dma_power_guard.h"

#include "power_runtime.h"

rt_err_t imu_dma_power_guard_acquire(imu_dma_power_guard_t *guard)
{
    rt_err_t result;

    if (guard == RT_NULL)
    {
        return -RT_ERROR;
    }

    guard->held = RT_FALSE;
    result = power_runtime_acquire_blocker(POWER_BLOCKER_DMA);
    if (result == RT_EOK)
    {
        guard->held = RT_TRUE;
    }
    return result;
}

void imu_dma_power_guard_release(imu_dma_power_guard_t *guard)
{
    if (guard != RT_NULL && guard->held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_DMA);
        guard->held = RT_FALSE;
    }
}
