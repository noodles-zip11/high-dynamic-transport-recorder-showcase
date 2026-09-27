#include <stdint.h>
#include <stdio.h>

#include "icm45686.h"
#include "icm45686_fifo.h"
#include "imu_acquisition_service.h"

typedef struct
{
    uint16_t fifo_count_samples;
    rt_err_t transfer_result;
} fake_bus_t;

typedef struct
{
    const uint8_t *data;
    rt_size_t length;
    rt_size_t offset;
    rt_err_t result;
    rt_bool_t drain_stream;
    unsigned int call_count;
    rt_size_t call_lengths[8];
} fake_fifo_reader_t;

static fake_bus_t fake_bus;
static fake_fifo_reader_t fake_fifo_reader;

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "imu_acquisition: %s\n", message);
        return 0;
    }

    return 1;
}

static rt_err_t fake_bus_transfer(const uint8_t *tx,
                                  uint8_t *rx,
                                  rt_size_t length)
{
    if (tx == RT_NULL || rx == RT_NULL || length != 3U
        || tx[0] != UINT8_C(0x92))
    {
        return -RT_ERROR;
    }

    rx[0] = 0U;
    rx[1] = (uint8_t)(fake_bus.fifo_count_samples >> 8U);
    rx[2] = (uint8_t)fake_bus.fifo_count_samples;
    return fake_bus.transfer_result;
}

static rt_err_t fake_fifo_read(uint8_t *data, rt_size_t length)
{
    rt_size_t index;
    unsigned int call_index = fake_fifo_reader.call_count;

    fake_fifo_reader.call_count++;
    if (call_index < 8U)
    {
        fake_fifo_reader.call_lengths[call_index] = length;
    }

    if (fake_fifo_reader.result != RT_EOK)
    {
        return fake_fifo_reader.result;
    }

    if (data == RT_NULL
        || (!fake_fifo_reader.drain_stream
            && length != fake_fifo_reader.length)
        || (fake_fifo_reader.drain_stream
            && length > fake_fifo_reader.length - fake_fifo_reader.offset))
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < length; index++)
    {
        data[index] = fake_fifo_reader.data[fake_fifo_reader.offset + index];
    }

    if (fake_fifo_reader.drain_stream)
    {
        fake_fifo_reader.offset += length;
        fake_bus.fifo_count_samples -=
            (uint16_t)(length / ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE);
    }

    return RT_EOK;
}

static int initialize_acquisition(imu_acquisition_t *acquisition,
                                  icm45686_t *device)
{
    const icm45686_bus_t bus = {
        .transfer = fake_bus_transfer,
    };

    return icm45686_init(device, &bus) == RT_EOK
           && imu_acquisition_init(acquisition, device, fake_fifo_read)
                  == RT_EOK;
}

static void reset_fakes(uint16_t fifo_count, const uint8_t *data,
                        rt_size_t length, rt_err_t read_result)
{
    unsigned int index;

    fake_bus.fifo_count_samples = fifo_count;
    fake_bus.transfer_result = RT_EOK;
    fake_fifo_reader.data = data;
    fake_fifo_reader.length = length;
    fake_fifo_reader.offset = 0U;
    fake_fifo_reader.result = read_result;
    fake_fifo_reader.drain_stream = RT_FALSE;
    fake_fifo_reader.call_count = 0U;
    for (index = 0U; index < 8U; index++)
    {
        fake_fifo_reader.call_lengths[index] = 0U;
    }
}

static int test_empty_fifo_does_not_read(void)
{
    icm45686_t device = {0};
    imu_acquisition_t acquisition = {0};
    imu_acquisition_result_t result = {0};
    uint8_t raw_fifo_buffer[16] = {0};
    icm45686_fifo_sample_t samples[1] = {0};

    reset_fakes(0U, RT_NULL, 0U, RT_EOK);
    if (!expect(initialize_acquisition(&acquisition, &device),
                "empty FIFO setup must initialize acquisition")
        || !expect(imu_acquisition_service(&acquisition, raw_fifo_buffer,
                                            sizeof(raw_fifo_buffer), samples,
                                            1U, &result) == RT_EOK,
                   "empty FIFO must not be an error")
        || !expect(fake_fifo_reader.call_count == 0U,
                   "empty FIFO must not request a raw FIFO read")
        || !expect(result.fifo_bytes == 0U && result.samples_produced == 0U,
                   "empty FIFO result must have no data")
        || !expect(acquisition.service_count == 1U
                   && acquisition.empty_fifo_count == 1U,
                   "empty FIFO must update service and empty statistics"))
    {
        return 1;
    }

    return 0;
}

static int test_valid_fifo_packet_is_read_and_parsed(void)
{
    const uint8_t fifo_packet[16] = {
        UINT8_C(0x68), 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
        0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x07, 0x12, 0x34,
    };
    icm45686_t device = {0};
    imu_acquisition_t acquisition = {0};
    imu_acquisition_result_t result = {0};
    uint8_t raw_fifo_buffer[16] = {0};
    icm45686_fifo_sample_t samples[1] = {0};

    reset_fakes(1U, fifo_packet, sizeof(fifo_packet), RT_EOK);
    if (!expect(initialize_acquisition(&acquisition, &device),
                "valid FIFO setup must initialize acquisition")
        || !expect(imu_acquisition_service(&acquisition, raw_fifo_buffer,
                                            sizeof(raw_fifo_buffer), samples,
                                            1U, &result) == RT_EOK,
                   "valid FIFO packet must be serviced")
        || !expect(fake_fifo_reader.call_count == 1U,
                   "valid FIFO packet must request one raw read")
        || !expect(result.fifo_bytes == 16U && result.samples_produced == 1U,
                   "valid FIFO packet must report its byte and sample count")
        || !expect(samples[0].accel[0] == 1 && samples[0].gyro[2] == 6,
                   "valid FIFO packet must reach the existing parser")
        || !expect(acquisition.sample_count == 1U
                   && acquisition.max_fifo_depth == 16U,
                   "valid FIFO packet must update sample and depth statistics"))
    {
        return 1;
    }

    return 0;
}

static int test_4112_byte_fifo_backlog_is_drained_in_chunks(void)
{
    const uint8_t fifo_packet[16] = {
        UINT8_C(0x68), 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
        0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x07, 0x12, 0x34,
    };
    icm45686_t device = {0};
    imu_acquisition_t acquisition = {0};
    imu_acquisition_result_t result = {0};
    uint8_t fifo_data[4112] = {0};
    uint8_t raw_fifo_buffer[1024] = {0};
    icm45686_fifo_sample_t samples[64] = {0};
    rt_size_t packet_index;
    rt_size_t byte_index;
    uint32_t total_samples = 0U;
    unsigned int chunk_count = 0U;

    for (packet_index = 0U; packet_index < 257U; packet_index++)
    {
        for (byte_index = 0U; byte_index < sizeof(fifo_packet); byte_index++)
        {
            fifo_data[packet_index * sizeof(fifo_packet) + byte_index] =
                fifo_packet[byte_index];
        }
    }

    reset_fakes(257U, fifo_data, sizeof(fifo_data), RT_EOK);
    fake_fifo_reader.drain_stream = RT_TRUE;
    if (!expect(initialize_acquisition(&acquisition, &device),
                "4112-byte backlog setup must initialize acquisition"))
    {
        return 1;
    }

    while (fake_bus.fifo_count_samples > 0U)
    {
        if (!expect(imu_acquisition_service(&acquisition, raw_fifo_buffer,
                                            sizeof(raw_fifo_buffer), samples,
                                            64U, &result) == RT_EOK,
                    "4112-byte backlog chunk must be serviced"))
        {
            return 1;
        }
        total_samples += (uint32_t)result.samples_produced;
        chunk_count++;
    }

    if (!expect(chunk_count == 5U,
                "4112-byte backlog must produce five FIFO chunks")
        || !expect(fake_fifo_reader.call_lengths[0] == 1024U
                   && fake_fifo_reader.call_lengths[1] == 1024U
                   && fake_fifo_reader.call_lengths[2] == 1024U
                   && fake_fifo_reader.call_lengths[3] == 1024U
                   && fake_fifo_reader.call_lengths[4] == 16U,
                   "backlog chunks were not read as 1024/16-byte transfers")
        || !expect(total_samples == 257U && acquisition.sample_count == 257U,
                   "backlog samples did not continue through the parser")
        || !expect(acquisition.fifo_capacity_error_count == 0U
                   && acquisition.max_fifo_depth == 4112U,
                   "normal FIFO backlog was recorded as a capacity failure"))
    {
        return 1;
    }

    return 0;
}

static int test_unprocessable_fifo_capacity_is_rejected_before_read(void)
{
    icm45686_t device = {0};
    imu_acquisition_t acquisition = {0};
    imu_acquisition_result_t result = {0};
    uint8_t raw_fifo_buffer[15] = {0};
    icm45686_fifo_sample_t samples[1] = {0};

    reset_fakes(1U, RT_NULL, 0U, RT_EOK);
    if (!expect(initialize_acquisition(&acquisition, &device),
                "unprocessable FIFO setup must initialize acquisition")
        || !expect(imu_acquisition_service(&acquisition, raw_fifo_buffer,
                                            sizeof(raw_fifo_buffer), samples,
                                            1U, &result) == -RT_ERROR,
                   "one complete FIFO packet must be rejected")
        || !expect(fake_fifo_reader.call_count == 0U,
                   "unprocessable FIFO must not request a raw read")
        || !expect(acquisition.fifo_capacity_error_count == 1U,
                   "unprocessable FIFO must update capacity error statistics"))
    {
        return 1;
    }

    return 0;
}

static int test_fifo_read_error_is_returned(void)
{
    const uint8_t fifo_packet[16] = {0};
    icm45686_t device = {0};
    imu_acquisition_t acquisition = {0};
    imu_acquisition_result_t result = {0};
    uint8_t raw_fifo_buffer[16] = {0};
    icm45686_fifo_sample_t samples[1] = {0};

    reset_fakes(1U, fifo_packet, sizeof(fifo_packet), -55);
    if (!expect(initialize_acquisition(&acquisition, &device),
                "read error setup must initialize acquisition")
        || !expect(imu_acquisition_service(&acquisition, raw_fifo_buffer,
                                            sizeof(raw_fifo_buffer), samples,
                                            1U, &result) == -55,
                   "raw FIFO read error must be returned")
        || !expect(acquisition.fifo_read_error_count == 1U,
                   "raw FIFO read error must update statistics"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_empty_fifo_does_not_read() != 0
        || test_valid_fifo_packet_is_read_and_parsed() != 0
        || test_4112_byte_fifo_backlog_is_drained_in_chunks() != 0
        || test_unprocessable_fifo_capacity_is_rejected_before_read() != 0
        || test_fifo_read_error_is_returned() != 0)
    {
        return 1;
    }

    puts("imu acquisition: PASS");
    return 0;
}
