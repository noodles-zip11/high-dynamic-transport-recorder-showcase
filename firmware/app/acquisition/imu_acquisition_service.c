#include "imu_acquisition_service.h"

#define IMU_ACQUISITION_MAX_FIFO_CHUNK_SAMPLES 64U
#define IMU_ACQUISITION_MAX_FIFO_CHUNK_BYTES \
    (IMU_ACQUISITION_MAX_FIFO_CHUNK_SAMPLES \
     * ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE)

rt_err_t imu_acquisition_init(imu_acquisition_t *acquisition,
                              icm45686_t *device,
                              imu_acquisition_read_fifo_fn read_fifo)
{
    if (acquisition == RT_NULL || device == RT_NULL || read_fifo == RT_NULL)
    {
        return -RT_ERROR;
    }

    acquisition->device = device;
    acquisition->read_fifo = read_fifo;
    acquisition->service_count = 0U;
    acquisition->empty_fifo_count = 0U;
    acquisition->fifo_count_error_count = 0U;
    acquisition->fifo_capacity_error_count = 0U;
    acquisition->fifo_read_error_count = 0U;
    acquisition->fifo_parse_error_count = 0U;
    acquisition->sample_count = 0U;
    acquisition->max_fifo_depth = 0U;
    return RT_EOK;
}

rt_err_t imu_acquisition_service(
    imu_acquisition_t *acquisition,
    uint8_t *raw_fifo_buffer,
    rt_size_t raw_fifo_capacity,
    icm45686_fifo_sample_t *samples,
    rt_size_t sample_capacity,
    imu_acquisition_result_t *result)
{
    icm45686_fifo_parse_result_t parse_result;
    uint16_t fifo_bytes;
    rt_size_t chunk_capacity;
    rt_size_t sample_buffer_capacity;
    rt_size_t read_bytes;
    rt_err_t status;

    if (acquisition == RT_NULL || acquisition->device == RT_NULL
        || acquisition->read_fifo == RT_NULL || raw_fifo_buffer == RT_NULL
        || samples == RT_NULL || sample_capacity == 0U || result == RT_NULL)
    {
        return -RT_ERROR;
    }

    result->fifo_bytes = 0U;
    result->samples_produced = 0U;
    result->parse_status = ICM45686_FIFO_PARSE_INVALID_ARGUMENT;
    acquisition->service_count++;

    status = icm45686_read_fifo_count(acquisition->device, &fifo_bytes);
    if (status != RT_EOK)
    {
        acquisition->fifo_count_error_count++;
        return status;
    }

    if (fifo_bytes == 0U)
    {
        acquisition->empty_fifo_count++;
        result->parse_status = ICM45686_FIFO_PARSE_OK;
        return RT_EOK;
    }

    if (fifo_bytes > acquisition->max_fifo_depth)
    {
        acquisition->max_fifo_depth = fifo_bytes;
    }

    chunk_capacity = raw_fifo_capacity;
    sample_buffer_capacity = sample_capacity
                             * ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE;
    if (chunk_capacity > sample_buffer_capacity)
    {
        chunk_capacity = sample_buffer_capacity;
    }
    if (chunk_capacity > IMU_ACQUISITION_MAX_FIFO_CHUNK_BYTES)
    {
        chunk_capacity = IMU_ACQUISITION_MAX_FIFO_CHUNK_BYTES;
    }
    chunk_capacity -= chunk_capacity
                      % ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE;

    if (chunk_capacity < ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE
        || fifo_bytes % ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE != 0U)
    {
        acquisition->fifo_capacity_error_count++;
        return -RT_ERROR;
    }

    read_bytes = (rt_size_t)fifo_bytes > chunk_capacity
                     ? chunk_capacity
                     : (rt_size_t)fifo_bytes;
    result->fifo_bytes = (uint16_t)read_bytes;

    status = acquisition->read_fifo(raw_fifo_buffer, read_bytes);
    if (status != RT_EOK)
    {
        acquisition->fifo_read_error_count++;
        return status;
    }

    parse_result = icm45686_fifo_parse_accel_gyro(
        raw_fifo_buffer,
        read_bytes,
        ICM45686_FIFO_BIG_ENDIAN,
        samples,
        sample_capacity);
    result->samples_produced = parse_result.samples_produced;
    result->parse_status = parse_result.status;

    if (parse_result.status != ICM45686_FIFO_PARSE_OK)
    {
        acquisition->fifo_parse_error_count++;
        return -RT_ERROR;
    }

    acquisition->sample_count += (uint32_t)parse_result.samples_produced;
    return RT_EOK;
}
