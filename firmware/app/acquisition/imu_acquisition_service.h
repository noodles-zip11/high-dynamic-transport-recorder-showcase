#ifndef TRANSPORT_RECORDER_IMU_ACQUISITION_SERVICE_H
#define TRANSPORT_RECORDER_IMU_ACQUISITION_SERVICE_H

#include <stdint.h>

#include <rtthread.h>

#include "icm45686.h"
#include "icm45686_fifo.h"

typedef rt_err_t (*imu_acquisition_read_fifo_fn)(uint8_t *data,
                                                  rt_size_t length);

typedef struct
{
    icm45686_t *device;
    imu_acquisition_read_fifo_fn read_fifo;
    uint32_t service_count;
    uint32_t empty_fifo_count;
    uint32_t fifo_count_error_count;
    uint32_t fifo_capacity_error_count;
    uint32_t fifo_read_error_count;
    uint32_t fifo_parse_error_count;
    uint32_t sample_count;
    uint16_t max_fifo_depth;
} imu_acquisition_t;

typedef struct
{
    uint16_t fifo_bytes;
    rt_size_t samples_produced;
    icm45686_fifo_parse_status_t parse_status;
} imu_acquisition_result_t;

rt_err_t imu_acquisition_init(imu_acquisition_t *acquisition,
                              icm45686_t *device,
                              imu_acquisition_read_fifo_fn read_fifo);

rt_err_t imu_acquisition_service(
    imu_acquisition_t *acquisition,
    uint8_t *raw_fifo_buffer,
    rt_size_t raw_fifo_capacity,
    icm45686_fifo_sample_t *samples,
    rt_size_t sample_capacity,
    imu_acquisition_result_t *result);

#endif
