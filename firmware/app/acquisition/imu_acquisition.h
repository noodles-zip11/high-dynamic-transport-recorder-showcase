#ifndef FIRMWARE_IMU_ACQUISITION_H
#define FIRMWARE_IMU_ACQUISITION_H

#include <stdint.h>

#include <rtthread.h>

#include "sample_block_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t service_count;
    uint32_t sample_count;
    uint16_t max_fifo_depth;
    uint32_t empty_fifo_count;
    uint32_t fifo_count_error_count;
    uint32_t fifo_capacity_error_count;
    uint32_t fifo_read_error_count;
    uint32_t fifo_parse_error_count;
    uint32_t dma_start_error_count;
    uint32_t dma_timeout_count;
    uint32_t dma_completion_error_count;
    uint16_t pool_min_free_count;
    uint32_t pool_backpressure_count;
    uint32_t batch_publish_error_count;
    uint32_t published_block_count;
} imu_acquisition_stats_t;

rt_err_t imu_acquisition_start(void);

void imu_acquisition_get_stats(imu_acquisition_stats_t *stats);

sample_block_t *imu_acquisition_take_ready_block(rt_int32_t timeout);

rt_err_t imu_acquisition_release_block(sample_block_t *block);

sample_block_pool_t *imu_acquisition_sample_pool(void);

#ifdef __cplusplus
}
#endif

#endif
