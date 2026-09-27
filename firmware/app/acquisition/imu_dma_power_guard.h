#ifndef TRANSPORT_RECORDER_IMU_DMA_POWER_GUARD_H
#define TRANSPORT_RECORDER_IMU_DMA_POWER_GUARD_H

#include <rtthread.h>

typedef struct
{
    rt_bool_t held;
} imu_dma_power_guard_t;

rt_err_t imu_dma_power_guard_acquire(imu_dma_power_guard_t *guard);
void imu_dma_power_guard_release(imu_dma_power_guard_t *guard);

#endif
