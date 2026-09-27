#ifndef TRANSPORT_RECORDER_IMU_SAMPLE_BATCHER_H
#define TRANSPORT_RECORDER_IMU_SAMPLE_BATCHER_H

#include <stdint.h>

#include <rtthread.h>

#include "icm45686_fifo.h"

#define IMU_SAMPLE_BATCH_SIZE 32U
#define IMU_SAMPLE_BATCHER_MAX_INPUT_SAMPLES 64U
#define IMU_SAMPLE_BATCHER_CAPACITY \
    (IMU_SAMPLE_BATCH_SIZE + IMU_SAMPLE_BATCHER_MAX_INPUT_SAMPLES)

typedef rt_err_t (*imu_sample_batcher_emit_fn)(
    const icm45686_fifo_sample_t *samples,
    uint16_t sample_count,
    uint32_t first_sequence,
    uint64_t first_monotonic_us,
    uint32_t sample_period_ns,
    void *context);

typedef struct
{
    icm45686_fifo_sample_t samples[IMU_SAMPLE_BATCHER_CAPACITY];
    uint16_t sample_count;
    uint32_t first_sequence;
    uint64_t first_monotonic_us;
    uint32_t sample_period_ns;
} imu_sample_batcher_t;

void imu_sample_batcher_init(imu_sample_batcher_t *batcher);

rt_err_t imu_sample_batcher_push(imu_sample_batcher_t *batcher,
                                 const icm45686_fifo_sample_t *samples,
                                 uint16_t sample_count,
                                 uint32_t first_sequence,
                                 uint64_t first_monotonic_us,
                                 uint32_t sample_period_ns,
                                 imu_sample_batcher_emit_fn emit,
                                 void *context);

#endif
