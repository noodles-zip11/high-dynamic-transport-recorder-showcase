#include <stdint.h>
#include <stdio.h>

#include "icm45686.h"

rt_err_t icm45686_configure_big_endian(icm45686_t *device);
rt_err_t icm45686_read_fifo_count(icm45686_t *device,
                                  uint16_t *fifo_count_bytes);
rt_err_t icm45686_verify_acquisition_config(icm45686_t *device);

#define FAKE_SPI_BUFFER_SIZE 17U
#define FAKE_SPI_MAX_TRANSACTIONS 32U

typedef struct
{
    rt_err_t result;
    uint8_t response[FAKE_SPI_BUFFER_SIZE];
    uint8_t observed_tx[FAKE_SPI_BUFFER_SIZE];
    rt_size_t observed_length;
    uint8_t transaction_tx[FAKE_SPI_MAX_TRANSACTIONS][FAKE_SPI_BUFFER_SIZE];
    uint8_t transaction_response[FAKE_SPI_MAX_TRANSACTIONS][FAKE_SPI_BUFFER_SIZE];
    rt_size_t transaction_length[FAKE_SPI_MAX_TRANSACTIONS];
    int use_transaction_responses;
    unsigned int call_count;
} fake_spi_t;

static fake_spi_t fake_spi;

static void fake_spi_reset(rt_err_t result)
{
    rt_size_t index;
    rt_size_t transaction_index;

    fake_spi.result = result;
    fake_spi.observed_length = 0U;
    fake_spi.call_count = 0U;
    fake_spi.use_transaction_responses = 0;

    for (index = 0U; index < FAKE_SPI_BUFFER_SIZE; index++)
    {
        fake_spi.response[index] = 0U;
        fake_spi.observed_tx[index] = 0U;
    }

    for (transaction_index = 0U;
         transaction_index < FAKE_SPI_MAX_TRANSACTIONS;
         transaction_index++)
    {
        fake_spi.transaction_length[transaction_index] = 0U;

        for (index = 0U; index < FAKE_SPI_BUFFER_SIZE; index++)
        {
            fake_spi.transaction_tx[transaction_index][index] = 0U;
            fake_spi.transaction_response[transaction_index][index] = 0U;
        }
    }
}

static rt_err_t fake_spi_transfer(const uint8_t *tx,
                                  uint8_t *rx,
                                  rt_size_t length)
{
    rt_size_t index;
    rt_size_t transaction_index;

    if (tx == RT_NULL || rx == RT_NULL || length > FAKE_SPI_BUFFER_SIZE)
    {
        return -RT_ERROR;
    }

    transaction_index = fake_spi.call_count;
    fake_spi.observed_length = length;

    for (index = 0U; index < length; index++)
    {
        fake_spi.observed_tx[index] = tx[index];

        if (transaction_index < FAKE_SPI_MAX_TRANSACTIONS)
        {
            fake_spi.transaction_tx[transaction_index][index] = tx[index];
        }

        rx[index] = fake_spi.use_transaction_responses
                    ? fake_spi.transaction_response[transaction_index][index]
                    : fake_spi.response[index];
    }

    if (transaction_index < FAKE_SPI_MAX_TRANSACTIONS)
    {
        fake_spi.transaction_length[transaction_index] = length;
    }

    fake_spi.call_count++;

    return fake_spi.result;
}

static void fake_spi_set_transaction_response(unsigned int transaction_index,
                                              rt_size_t byte_index,
                                              uint8_t value)
{
    if (transaction_index < FAKE_SPI_MAX_TRANSACTIONS
        && byte_index < FAKE_SPI_BUFFER_SIZE)
    {
        fake_spi.use_transaction_responses = 1;
        fake_spi.transaction_response[transaction_index][byte_index] = value;
    }
}

static rt_err_t initialize_device(icm45686_t *device)
{
    const icm45686_bus_t bus = {
        .transfer = fake_spi_transfer,
    };

    return icm45686_init(device, &bus);
}

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "icm45686: %s\n", message);
        return 0;
    }

    return 1;
}

static int expect_write_transaction(unsigned int index,
                                    uint8_t register_address,
                                    uint8_t value,
                                    const char *message)
{
    return expect(index < FAKE_SPI_MAX_TRANSACTIONS
                  && fake_spi.transaction_length[index] == 2U
                  && fake_spi.transaction_tx[index][0] == register_address
                  && fake_spi.transaction_tx[index][1] == value,
                  message);
}

static int expect_read_transaction(unsigned int index,
                                   uint8_t register_address,
                                   const char *message)
{
    return expect(index < FAKE_SPI_MAX_TRANSACTIONS
                  && fake_spi.transaction_length[index] == 2U
                  && fake_spi.transaction_tx[index][0]
                      == (uint8_t)(UINT8_C(0x80) | register_address),
                  message);
}

static int expect_indirect_write_transaction(unsigned int index,
                                             uint8_t address_high,
                                             uint8_t address_low,
                                             uint8_t value,
                                             const char *message)
{
    return expect(index < FAKE_SPI_MAX_TRANSACTIONS
                  && fake_spi.transaction_length[index] == 4U
                  && fake_spi.transaction_tx[index][0]
                      == ICM45686_REG_IREG_ADDR_15_8
                  && fake_spi.transaction_tx[index][1] == address_high
                  && fake_spi.transaction_tx[index][2] == address_low
                  && fake_spi.transaction_tx[index][3] == value,
                  message);
}

static int expect_indirect_address_transaction(unsigned int index,
                                               uint8_t address_high,
                                               uint8_t address_low,
                                               const char *message)
{
    return expect(index < FAKE_SPI_MAX_TRANSACTIONS
                  && fake_spi.transaction_length[index] == 3U
                  && fake_spi.transaction_tx[index][0]
                      == ICM45686_REG_IREG_ADDR_15_8
                  && fake_spi.transaction_tx[index][1] == address_high
                  && fake_spi.transaction_tx[index][2] == address_low,
                  message);
}

static int test_init(void)
{
    icm45686_t device = {0};
    const icm45686_bus_t valid_bus = {
        .transfer = fake_spi_transfer,
    };
    const icm45686_bus_t invalid_bus = {
        .transfer = RT_NULL,
    };

    if (!expect(icm45686_init(&device, &valid_bus) == RT_EOK,
                "init must accept a valid bus")
        || !expect(device.bus.transfer == fake_spi_transfer,
                   "init must store the transfer function")
        || !expect(icm45686_init(RT_NULL, &valid_bus) == -RT_ERROR,
                   "init must reject a null device")
        || !expect(icm45686_init(&device, RT_NULL) == -RT_ERROR,
                   "init must reject a null bus")
        || !expect(icm45686_init(&device, &invalid_bus) == -RT_ERROR,
                   "init must reject a bus without transfer"))
    {
        return 1;
    }

    return 0;
}

static int test_read_reg(void)
{
    icm45686_t device = {0};
    uint8_t data[2] = {0};
    uint8_t one_byte = 0U;

    fake_spi_reset(RT_EOK);
    fake_spi.response[1] = UINT8_C(0x12);
    fake_spi.response[2] = UINT8_C(0x34);

    if (!expect(initialize_device(&device) == RT_EOK,
                "read setup must initialize device")
        || !expect(icm45686_read_reg(&device, UINT8_C(0x12), data, 2U) == RT_EOK,
                   "read must return success after SPI success")
        || !expect(fake_spi.call_count == 1U,
                   "read must issue one SPI transfer")
        || !expect(fake_spi.observed_length == 3U,
                   "read must clock address plus two data bytes")
        || !expect(fake_spi.observed_tx[0] == UINT8_C(0x92),
                   "read must set bit 7 in the register address")
        || !expect(fake_spi.observed_tx[1] == 0U && fake_spi.observed_tx[2] == 0U,
                   "read data clocks must use dummy bytes")
        || !expect(data[0] == UINT8_C(0x12) && data[1] == UINT8_C(0x34),
                   "read must skip the receive byte paired with address"))
    {
        return 1;
    }

    fake_spi_reset(-23);
    if (!expect(icm45686_read_reg(&device, UINT8_C(0x12), &one_byte, 1U) == -23,
                "read must return the SPI transfer error"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    if (!expect(icm45686_read_reg(RT_NULL, UINT8_C(0x12), &one_byte, 1U) == -RT_ERROR,
                "read must reject a null device")
        || !expect(icm45686_read_reg(&device, UINT8_C(0x12), RT_NULL, 1U) == -RT_ERROR,
                   "read must reject a null output buffer")
        || !expect(icm45686_read_reg(&device, UINT8_C(0x12), &one_byte, 0U) == -RT_ERROR,
                   "read must reject zero length")
        || !expect(icm45686_read_reg(&device, UINT8_C(0x12), &one_byte, 17U) == -RT_ERROR,
                   "read must reject an oversized request")
        || !expect(icm45686_read_reg(&device, UINT8_C(0x92), &one_byte, 1U) == -RT_ERROR,
                   "read must reject a register address with bit 7 set")
        || !expect(fake_spi.call_count == 0U,
                   "invalid reads must not access SPI"))
    {
        return 1;
    }

    return 0;
}

static int test_write_reg(void)
{
    icm45686_t device = {0};
    const uint8_t data[2] = {UINT8_C(0xAB), UINT8_C(0xCD)};

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "write setup must initialize device")
        || !expect(icm45686_write_reg(&device, UINT8_C(0x12), data, 2U) == RT_EOK,
                   "write must return success after SPI success")
        || !expect(fake_spi.call_count == 1U,
                   "write must issue one SPI transfer")
        || !expect(fake_spi.observed_length == 3U,
                   "write must send address plus two data bytes")
        || !expect(fake_spi.observed_tx[0] == UINT8_C(0x12),
                   "write must clear bit 7 in the register address")
        || !expect(fake_spi.observed_tx[1] == UINT8_C(0xAB)
                   && fake_spi.observed_tx[2] == UINT8_C(0xCD),
                   "write must send the caller data unchanged"))
    {
        return 1;
    }

    fake_spi_reset(-31);
    if (!expect(icm45686_write_reg(&device, UINT8_C(0x12), data, 2U) == -31,
                "write must return the SPI transfer error"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    if (!expect(icm45686_write_reg(RT_NULL, UINT8_C(0x12), data, 1U) == -RT_ERROR,
                "write must reject a null device")
        || !expect(icm45686_write_reg(&device, UINT8_C(0x12), RT_NULL, 1U) == -RT_ERROR,
                   "write must reject a null input buffer")
        || !expect(icm45686_write_reg(&device, UINT8_C(0x12), data, 0U) == -RT_ERROR,
                   "write must reject zero length")
        || !expect(icm45686_write_reg(&device, UINT8_C(0x12), data, 17U) == -RT_ERROR,
                   "write must reject an oversized request")
        || !expect(icm45686_write_reg(&device, UINT8_C(0x92), data, 1U) == -RT_ERROR,
                   "write must reject a register address with bit 7 set")
        || !expect(fake_spi.call_count == 0U,
                   "invalid writes must not access SPI"))
    {
        return 1;
    }

    return 0;
}

static int test_probe(void)
{
    icm45686_t device = {0};

    if (!expect(initialize_device(&device) == RT_EOK,
                "probe setup must initialize device"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    fake_spi.response[1] = ICM45686_WHO_AM_I_VALUE;
    if (!expect(icm45686_probe(&device) == RT_EOK,
                "probe must accept the ICM45686 WHO_AM_I value")
        || !expect(fake_spi.observed_length == 2U
                   && fake_spi.observed_tx[0] == UINT8_C(0xF2),
                   "probe must read WHO_AM_I with command 0xF2"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    fake_spi.response[1] = UINT8_C(0x00);
    if (!expect(icm45686_probe(&device) == -RT_ERROR,
                "probe must reject an unexpected WHO_AM_I value"))
    {
        return 1;
    }

    fake_spi_reset(-47);
    if (!expect(icm45686_probe(&device) == -47,
                "probe must return the SPI transfer error"))
    {
        return 1;
    }

    return 0;
}

static int test_configure_1600hz_16g_2000dps(void)
{
    icm45686_t device = {0};

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "configuration setup must initialize device")
        || !expect(icm45686_configure_1600hz_16g_2000dps(&device) == RT_EOK,
                   "configuration must succeed after SPI success")
        || !expect(fake_spi.call_count == 3U,
                   "configuration must write three registers")
        || !expect(fake_spi.transaction_length[0] == 2U
                   && fake_spi.transaction_tx[0][0] == ICM45686_REG_ACCEL_CONFIG0
                   && fake_spi.transaction_tx[0][1]
                       == ICM45686_ACCEL_CONFIG0_16G_1600HZ,
                   "configuration must write accel settings first")
        || !expect(fake_spi.transaction_length[1] == 2U
                   && fake_spi.transaction_tx[1][0] == ICM45686_REG_GYRO_CONFIG0
                   && fake_spi.transaction_tx[1][1]
                       == ICM45686_GYRO_CONFIG0_2000DPS_1600HZ,
                   "configuration must write gyro settings second")
        || !expect(fake_spi.transaction_length[2] == 2U
                   && fake_spi.transaction_tx[2][0] == ICM45686_REG_PWR_MGMT0
                   && fake_spi.transaction_tx[2][1]
                       == ICM45686_PWR_MGMT0_ACCEL_GYRO_LN,
                   "configuration must enable low-noise sensors last"))
    {
        return 1;
    }

    fake_spi_reset(-61);
    if (!expect(icm45686_configure_1600hz_16g_2000dps(&device) == -61,
                "configuration must return the SPI error")
        || !expect(fake_spi.call_count == 1U,
                   "configuration must stop after the first SPI error"))
    {
        return 1;
    }

    return 0;
}

static int test_configure_fifo_accel_gyro_32_samples(void)
{
    icm45686_t device = {0};
    const uint8_t watermark_low =
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES & UINT16_C(0x00FF));
    const uint8_t watermark_high =
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES >> 8U);

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "FIFO setup must initialize device")
        || !expect(icm45686_configure_fifo_accel_gyro_32_samples(&device)
                   == RT_EOK,
                   "FIFO configuration must succeed after SPI success")
        || !expect(fake_spi.call_count == 6U,
                   "FIFO configuration must write six registers")
        || !expect_write_transaction(0U, ICM45686_REG_FIFO_CONFIG0,
                                     ICM45686_FIFO_CONFIG0_BYPASS_2KB,
                                     "FIFO configuration must set bypass depth first")
        || !expect_write_transaction(1U, ICM45686_REG_FIFO_CONFIG4,
                                     ICM45686_FIFO_CONFIG4_TIMESTAMP,
                                     "FIFO configuration must enable timestamp")
        || !expect_write_transaction(2U, ICM45686_REG_FIFO_CONFIG1_0,
                                     watermark_low,
                                     "FIFO configuration must write watermark low byte first")
        || !expect_write_transaction(3U, ICM45686_REG_FIFO_CONFIG1_1,
                                     watermark_high,
                                     "FIFO configuration must write watermark high byte second")
        || !expect_write_transaction(4U, ICM45686_REG_FIFO_CONFIG0,
                                     ICM45686_FIFO_CONFIG0_STREAM_2KB,
                                     "FIFO configuration must enable stream mode")
        || !expect_write_transaction(5U, ICM45686_REG_FIFO_CONFIG3,
                                     ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF,
                                     "FIFO configuration must enable accel gyro interface last"))
    {
        return 1;
    }

    fake_spi_reset(-73);
    if (!expect(icm45686_configure_fifo_accel_gyro_32_samples(&device) == -73,
                "FIFO configuration must return the SPI error")
        || !expect(fake_spi.call_count == 1U,
                   "FIFO configuration must stop after the first SPI error"))
    {
        return 1;
    }

    return 0;
}

static int test_configure_big_endian(void)
{
    icm45686_t device = {0};

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "endianness setup must initialize device")
        || !expect(icm45686_configure_big_endian(&device) == RT_EOK,
                   "big-endian configuration must succeed after SPI success")
        || !expect(fake_spi.call_count == 1U,
                   "big-endian configuration must issue one SPI transfer")
        || !expect_indirect_write_transaction(
            0U, UINT8_C(0xA2), UINT8_C(0x67), UINT8_C(0x02),
            "big-endian configuration must write SREG through IREG"))
    {
        return 1;
    }

    fake_spi_reset(-79);
    if (!expect(icm45686_configure_big_endian(&device) == -79,
                "big-endian configuration must return the SPI error")
        || !expect(fake_spi.call_count == 1U,
                   "big-endian configuration must stop after its SPI error"))
    {
        return 1;
    }

    return 0;
}

static int test_read_fifo_count(void)
{
    icm45686_t device = {0};
    uint16_t fifo_count_bytes = 0U;

    fake_spi_reset(RT_EOK);
    fake_spi.response[1] = UINT8_C(0x00);
    fake_spi.response[2] = UINT8_C(0x20);
    if (!expect(initialize_device(&device) == RT_EOK,
                "FIFO count setup must initialize device")
        || !expect(icm45686_read_fifo_count(&device, &fifo_count_bytes)
                   == RT_EOK,
                   "FIFO count must succeed after SPI success")
        || !expect(fake_spi.call_count == 1U
                   && fake_spi.observed_length == 3U
                   && fake_spi.observed_tx[0] == UINT8_C(0x92),
                   "FIFO count must read two bytes from register 0x12")
        || !expect(fifo_count_bytes == UINT16_C(512),
                   "FIFO count must convert 32 FIFO frames to 512 bytes"))
    {
        return 1;
    }

    fake_spi_reset(-83);
    if (!expect(icm45686_read_fifo_count(&device, &fifo_count_bytes) == -83,
                "FIFO count must return the SPI error"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    if (!expect(icm45686_read_fifo_count(&device, RT_NULL) == -RT_ERROR,
                "FIFO count must reject a null output pointer")
        || !expect(fake_spi.call_count == 0U,
                   "FIFO count with a null output must not access SPI"))
    {
        return 1;
    }

    return 0;
}

static int test_read_fifo_dma(void)
{
    icm45686_t device = {0};
    uint8_t tx[FAKE_SPI_BUFFER_SIZE] = {UINT8_C(0xA5)};
    uint8_t rx[FAKE_SPI_BUFFER_SIZE] = {0};

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "FIFO DMA setup must initialize device")
        || !expect(icm45686_read_fifo_dma(&device,
                                           fake_spi_transfer,
                                           tx,
                                           rx,
                                           16U) == RT_EOK,
                   "FIFO DMA read must succeed after DMA transfer success")
        || !expect(fake_spi.call_count == 1U,
                   "FIFO DMA read must issue one DMA transfer")
        || !expect(fake_spi.observed_length == 17U,
                   "FIFO DMA read must clock command plus FIFO bytes")
        || !expect(fake_spi.observed_tx[0] == UINT8_C(0x94),
                   "FIFO DMA read must send FIFO_DATA read command")
        || !expect(fake_spi.observed_tx[1] == 0U
                   && fake_spi.observed_tx[16] == 0U,
                   "FIFO DMA read must send dummy bytes after the command"))
    {
        return 1;
    }

    fake_spi_reset(-89);
    if (!expect(icm45686_read_fifo_dma(&device,
                                       fake_spi_transfer,
                                       tx,
                                       rx,
                                       16U) == -89,
                "FIFO DMA read must return the DMA transfer error")
        || !expect(fake_spi.call_count == 1U,
                   "FIFO DMA read must not retry a failed DMA transfer"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    if (!expect(icm45686_read_fifo_dma(&device,
                                       RT_NULL,
                                       tx,
                                       rx,
                                       1U) == -RT_ERROR,
                "FIFO DMA read must reject a null DMA transfer")
        || !expect(icm45686_read_fifo_dma(&device,
                                           fake_spi_transfer,
                                           RT_NULL,
                                           rx,
                                           1U) == -RT_ERROR,
                   "FIFO DMA read must reject a null transmit buffer")
        || !expect(icm45686_read_fifo_dma(&device,
                                           fake_spi_transfer,
                                           tx,
                                           RT_NULL,
                                           1U) == -RT_ERROR,
                   "FIFO DMA read must reject a null receive buffer")
        || !expect(icm45686_read_fifo_dma(&device,
                                           fake_spi_transfer,
                                           tx,
                                           rx,
                                           0U) == -RT_ERROR,
                   "FIFO DMA read must reject zero FIFO bytes")
        || !expect(fake_spi.call_count == 0U,
                   "invalid FIFO DMA reads must not access SPI"))
    {
        return 1;
    }

    return 0;
}

static int test_configure_int1_fifo_watermark(void)
{
    icm45686_t device = {0};

    fake_spi_reset(RT_EOK);
    if (!expect(initialize_device(&device) == RT_EOK,
                "INT1 setup must initialize device")
        || !expect(icm45686_configure_int1_fifo_watermark(&device)
                   == RT_EOK,
                   "INT1 FIFO watermark configuration must succeed after SPI success")
        || !expect(fake_spi.call_count == 5U,
                   "INT1 FIFO watermark configuration must write five registers")
        || !expect_write_transaction(0U, ICM45686_REG_INT1_CONFIG0,
                                     UINT8_C(0x00),
                                     "INT1 configuration must disable primary interrupt sources first")
        || !expect_write_transaction(1U, ICM45686_REG_INT1_CONFIG1,
                                     UINT8_C(0x00),
                                     "INT1 configuration must disable secondary interrupt sources second")
        || !expect_write_transaction(
            2U, ICM45686_REG_INT1_CONFIG2,
            ICM45686_INT1_CONFIG2_PUSH_PULL_PULSE_ACTIVE_HIGH,
            "INT1 configuration must select push-pull pulse active-high output")
        || !expect_indirect_write_transaction(
            3U, UINT8_C(0xA2), UINT8_C(0x6A),
            ICM45686_INT1_PULSE_DURATION_100US,
            "INT1 configuration must select its pulse duration through IREG")
        || !expect_write_transaction(4U, ICM45686_REG_INT1_CONFIG0,
                                     ICM45686_INT1_CONFIG0_FIFO_THS_ENABLE,
                                     "INT1 configuration must enable FIFO threshold last"))
    {
        return 1;
    }

    fake_spi_reset(-101);
    if (!expect(icm45686_configure_int1_fifo_watermark(&device) == -101,
                "INT1 configuration must return the SPI error")
        || !expect(fake_spi.call_count == 1U,
                   "INT1 configuration must stop after its first SPI error"))
    {
        return 1;
    }

    return 0;
}

static int test_verify_acquisition_config(void)
{
    icm45686_t device = {0};

    fake_spi_reset(RT_EOK);
    fake_spi_set_transaction_response(1U, 1U,
                                      ICM45686_SREG_CTRL_DATA_BIG_ENDIAN);
    fake_spi_set_transaction_response(2U, 1U,
                                      ICM45686_ACCEL_CONFIG0_16G_1600HZ);
    fake_spi_set_transaction_response(3U, 1U,
                                      ICM45686_GYRO_CONFIG0_2000DPS_1600HZ);
    fake_spi_set_transaction_response(4U, 1U,
                                      ICM45686_PWR_MGMT0_ACCEL_GYRO_LN);
    fake_spi_set_transaction_response(5U, 1U,
                                      ICM45686_FIFO_CONFIG0_STREAM_2KB);
    fake_spi_set_transaction_response(6U, 1U,
                                      ICM45686_FIFO_CONFIG4_TIMESTAMP);
    fake_spi_set_transaction_response(7U, 1U,
                                      (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES
                                                & UINT16_C(0x00FF)));
    fake_spi_set_transaction_response(8U, 1U,
                                      (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES
                                                >> 8U));
    fake_spi_set_transaction_response(9U, 1U,
                                      ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF);
    if (!expect(initialize_device(&device) == RT_EOK,
                "configuration verification setup must initialize device")
        || !expect(icm45686_verify_acquisition_config(&device) == RT_EOK,
                   "configuration verification must accept expected registers")
        || !expect(fake_spi.call_count == 10U,
                   "configuration verification must read SREG and eight direct registers")
        || !expect_indirect_address_transaction(
            0U, UINT8_C(0xA2), UINT8_C(0x67),
            "configuration verification must select SREG through IREG")
        || !expect_read_transaction(1U, ICM45686_REG_IREG_DATA,
                                    "configuration verification must read SREG data")
        || !expect_read_transaction(2U, ICM45686_REG_ACCEL_CONFIG0,
                                    "configuration verification must read accel config after SREG")
        || !expect_read_transaction(9U, ICM45686_REG_FIFO_CONFIG3,
                                    "configuration verification must read FIFO interface config last"))
    {
        return 1;
    }

    fake_spi_reset(RT_EOK);
    fake_spi_set_transaction_response(1U, 1U, UINT8_C(0x00));
    if (!expect(icm45686_verify_acquisition_config(&device) == -RT_ERROR,
                "configuration verification must reject a mismatched register")
        || !expect(fake_spi.call_count == 2U,
                   "configuration verification must stop at the SREG mismatch"))
    {
        return 1;
    }

    fake_spi_reset(-97);
    if (!expect(icm45686_verify_acquisition_config(&device) == -97,
                "configuration verification must return the SPI error")
        || !expect(fake_spi.call_count == 1U,
                   "configuration verification must stop after an SPI error"))
    {
        return 1;
    }

    return 0;
}

static int test_configure_acquisition_runs_full_startup_sequence(void)
{
    icm45686_t device = {0};

    fake_spi_reset(RT_EOK);
    fake_spi_set_transaction_response(0U, 1U, ICM45686_WHO_AM_I_VALUE);
    fake_spi_set_transaction_response(
        17U, 1U, ICM45686_SREG_CTRL_DATA_BIG_ENDIAN);
    fake_spi_set_transaction_response(
        18U, 1U, ICM45686_ACCEL_CONFIG0_16G_1600HZ);
    fake_spi_set_transaction_response(
        19U, 1U, ICM45686_GYRO_CONFIG0_2000DPS_1600HZ);
    fake_spi_set_transaction_response(
        20U, 1U, ICM45686_PWR_MGMT0_ACCEL_GYRO_LN);
    fake_spi_set_transaction_response(
        21U, 1U, ICM45686_FIFO_CONFIG0_STREAM_2KB);
    fake_spi_set_transaction_response(
        22U, 1U, ICM45686_FIFO_CONFIG4_TIMESTAMP);
    fake_spi_set_transaction_response(
        23U, 1U,
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES & UINT16_C(0x00FF)));
    fake_spi_set_transaction_response(
        24U, 1U,
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES >> 8U));
    fake_spi_set_transaction_response(
        25U, 1U, ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF);

    if (!expect(initialize_device(&device) == RT_EOK,
                "startup sequence setup must initialize device")
        || !expect(icm45686_configure_acquisition(&device) == RT_EOK,
                   "startup sequence must configure and verify acquisition")
        || !expect(fake_spi.call_count == 26U,
                   "startup sequence must perform probe, writes, and indirect readback")
        || !expect_read_transaction(0U, ICM45686_REG_WHO_AM_I,
                                    "startup sequence must probe first")
        || !expect_indirect_write_transaction(
            1U, UINT8_C(0xA2), UINT8_C(0x67),
            ICM45686_SREG_CTRL_DATA_BIG_ENDIAN,
            "startup sequence must select big endian through IREG after probe")
        || !expect_write_transaction(2U, ICM45686_REG_ACCEL_CONFIG0,
                                     ICM45686_ACCEL_CONFIG0_16G_1600HZ,
                                     "startup sequence must configure accel after byte order")
        || !expect_write_transaction(
            10U, ICM45686_REG_FIFO_CONFIG3,
            ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF,
            "startup sequence must finish FIFO setup before INT1")
        || !expect_write_transaction(
            15U, ICM45686_REG_INT1_CONFIG0,
            ICM45686_INT1_CONFIG0_FIFO_THS_ENABLE,
            "startup sequence must enable FIFO watermark after output setup")
        || !expect_indirect_address_transaction(
            16U, UINT8_C(0xA2), UINT8_C(0x67),
            "startup sequence must select SREG before direct-register readback")
        || !expect_read_transaction(
            25U, ICM45686_REG_FIFO_CONFIG3,
            "startup sequence must complete configuration readback"))
    {
        return 1;
    }

    fake_spi_reset(-109);
    if (!expect(initialize_device(&device) == RT_EOK,
                "startup failure setup must initialize device")
        || !expect(icm45686_configure_acquisition(&device) == -109,
                   "startup sequence must return the first SPI failure")
        || !expect(fake_spi.call_count == 1U,
                   "startup sequence must stop when probe fails"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_init() != 0 || test_read_reg() != 0
        || test_write_reg() != 0 || test_probe() != 0
        || test_configure_1600hz_16g_2000dps() != 0
        || test_configure_fifo_accel_gyro_32_samples() != 0
        || test_configure_big_endian() != 0
        || test_read_fifo_count() != 0
        || test_read_fifo_dma() != 0
        || test_configure_int1_fifo_watermark() != 0
        || test_verify_acquisition_config() != 0
        || test_configure_acquisition_runs_full_startup_sequence() != 0)
    {
        return 1;
    }

    puts("icm45686 driver: PASS");
    return 0;
}
