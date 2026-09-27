#include "icm45686.h"


#define ICM45686_SPI_READ_BIT UINT8_C(0x80)
#define ICM45686_REGISTER_TRANSFER_MAX_BYTES 16U




typedef struct
{
    uint8_t register_address;
    uint8_t expected_value;
} icm45686_register_expectation_t;

static const icm45686_register_expectation_t
    icm45686_acquisition_config_expectations[] = {
    {ICM45686_REG_ACCEL_CONFIG0,
     ICM45686_ACCEL_CONFIG0_16G_1600HZ},
    {ICM45686_REG_GYRO_CONFIG0,
     ICM45686_GYRO_CONFIG0_2000DPS_1600HZ},
    {ICM45686_REG_PWR_MGMT0,
     ICM45686_PWR_MGMT0_ACCEL_GYRO_LN},
    {ICM45686_REG_FIFO_CONFIG0,
     ICM45686_FIFO_CONFIG0_STREAM_2KB},
    {ICM45686_REG_FIFO_CONFIG4,
     ICM45686_FIFO_CONFIG4_TIMESTAMP},
    {ICM45686_REG_FIFO_CONFIG1_0,
     (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES
               & UINT16_C(0x00FF))},
    {ICM45686_REG_FIFO_CONFIG1_1,
     (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES >> 8U)},
    {ICM45686_REG_FIFO_CONFIG3,
     ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF},
};

static rt_err_t icm45686_write_indirect_reg(icm45686_t *device,
                                            uint16_t register_address,
                                            uint8_t value)
{
    const uint8_t data[] = {
        (uint8_t)(register_address >> 8U),
        (uint8_t)register_address,
        value,
    };

    return icm45686_write_reg(device,
                              ICM45686_REG_IREG_ADDR_15_8,
                              data,
                              sizeof(data));
}

static rt_err_t icm45686_read_indirect_reg(icm45686_t *device,
                                           uint16_t register_address,
                                           uint8_t *value)
{
    const uint8_t data[] = {
        (uint8_t)(register_address >> 8U),
        (uint8_t)register_address,
    };
    rt_err_t result;

    result = icm45686_write_reg(device,
                                ICM45686_REG_IREG_ADDR_15_8,
                                data,
                                sizeof(data));
    if (result != RT_EOK)
    {
        return result;
    }

    return icm45686_read_reg(device, ICM45686_REG_IREG_DATA, value, 1U);
}



rt_err_t icm45686_init(icm45686_t *device,
                       const icm45686_bus_t *bus)
{
    if (device == RT_NULL || bus == RT_NULL || bus->transfer == RT_NULL)
    {
        return -RT_ERROR;
    }

    device->bus = *bus;
    return RT_EOK;
}

rt_err_t icm45686_read_reg(icm45686_t *device,
                           uint8_t register_address,
                           uint8_t *data,
                           rt_size_t length)
{
    uint8_t tx[ICM45686_REGISTER_TRANSFER_MAX_BYTES + 1U] = {0};
    uint8_t rx[ICM45686_REGISTER_TRANSFER_MAX_BYTES + 1U] = {0};
    volatile const uint8_t *received;
    rt_err_t result;
    rt_size_t index;

    if (device == RT_NULL || device->bus.transfer == RT_NULL
        || data == RT_NULL || length == 0U
        || length > ICM45686_REGISTER_TRANSFER_MAX_BYTES
        || (register_address & ICM45686_SPI_READ_BIT) != 0U)
    {
        return -RT_ERROR;
    }

    tx[0] = ICM45686_SPI_READ_BIT | register_address;

    result = device->bus.transfer(tx, rx, length + 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    /* Keep SPI response copies byte-sized; Cortex-M7 traps unaligned halfwords. */
    received = rx;
    for (index = 0U; index < length; index++)
    {
        data[index] = received[index + 1U];
    }

    return RT_EOK;
}

rt_err_t icm45686_write_reg(icm45686_t *device,
                            uint8_t register_address,
                            const uint8_t *data,
                            rt_size_t length)
{
    uint8_t tx[ICM45686_REGISTER_TRANSFER_MAX_BYTES + 1U] = {0};
    uint8_t rx[ICM45686_REGISTER_TRANSFER_MAX_BYTES + 1U] = {0};
    rt_err_t result;
    rt_size_t index;

    if (device == RT_NULL || device->bus.transfer == RT_NULL
        || data == RT_NULL || length == 0U
        || length > ICM45686_REGISTER_TRANSFER_MAX_BYTES
        || (register_address & ICM45686_SPI_READ_BIT) != 0U)
    {
        return -RT_ERROR;
    }

    tx[0] = register_address;

    for (index = 0U; index < length; index++)
    {
        tx[index + 1U] = data[index];
    }

    result = device->bus.transfer(tx, rx, length + 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    return RT_EOK;
}



rt_err_t icm45686_probe(icm45686_t *device)
{
    uint8_t who_am_i = 0U;
    rt_err_t result;

    result = icm45686_read_reg(device,
                               ICM45686_REG_WHO_AM_I,
                               &who_am_i,
                               1U);
    if (result != RT_EOK)
    {
        return result;
    }

    if (who_am_i != ICM45686_WHO_AM_I_VALUE)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

//写入寄存器，配置陀螺仪
rt_err_t icm45686_configure_1600hz_16g_2000dps(
    icm45686_t *device)
{
    const uint8_t accel_config = ICM45686_ACCEL_CONFIG0_16G_1600HZ;
    const uint8_t gyro_config = ICM45686_GYRO_CONFIG0_2000DPS_1600HZ;
    const uint8_t power_config = ICM45686_PWR_MGMT0_ACCEL_GYRO_LN;
    rt_err_t result;

    result = icm45686_write_reg(device,
                                ICM45686_REG_ACCEL_CONFIG0,
                                &accel_config,
                                1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device,
                                ICM45686_REG_GYRO_CONFIG0,
                                &gyro_config,
                                1U);
    if (result != RT_EOK)
    {
        return result;
    }

    return icm45686_write_reg(device,
                              ICM45686_REG_PWR_MGMT0,
                              &power_config,
                              1U);
}

//写入寄存器，配置FIFO
rt_err_t icm45686_configure_fifo_accel_gyro_32_samples(
    icm45686_t *device)
{
    const uint8_t bypass_config = ICM45686_FIFO_CONFIG0_BYPASS_2KB;
    const uint8_t timestamp_config = ICM45686_FIFO_CONFIG4_TIMESTAMP;
    const uint8_t watermark_low =
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES & UINT16_C(0x00FF));
    const uint8_t watermark_high =
        (uint8_t)(ICM45686_FIFO_WATERMARK_SAMPLES >> 8U);
    const uint8_t stream_config = ICM45686_FIFO_CONFIG0_STREAM_2KB;
    const uint8_t fifo_interface_config =
        ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF;
    rt_err_t result;

    result = icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG0,
                                &bypass_config, 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG4,
                                &timestamp_config, 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG1_0,
                                &watermark_low, 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG1_1,
                                &watermark_high, 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG0,
                                &stream_config, 1U);
    if (result != RT_EOK)
    {
        return result;
    }

    return icm45686_write_reg(device, ICM45686_REG_FIFO_CONFIG3,
                              &fifo_interface_config, 1U);
}
rt_err_t icm45686_configure_big_endian(icm45686_t *device)
{
    return icm45686_write_indirect_reg(
        device,
        ICM45686_INDIRECT_SREG_CTRL_ADDRESS,
        ICM45686_SREG_CTRL_DATA_BIG_ENDIAN);
}



rt_err_t icm45686_read_fifo_count(icm45686_t *device,uint16_t *fifo_count_bytes)
{
    uint8_t raw_count[2];
    rt_err_t result;
    if (fifo_count_bytes == RT_NULL)
    {
        return -RT_ERROR;
    }

    result = icm45686_read_reg(device, ICM45686_REG_FIFO_COUNT_0, raw_count, 2U);
    if (result != RT_EOK)
    {
        return result;
    }

    {
        volatile const uint8_t *count_bytes = raw_count;
        const uint32_t fifo_count_samples =
            ((uint32_t)count_bytes[0] << 8U) | (uint32_t)count_bytes[1];

        if (fifo_count_samples
            > (UINT16_MAX / ICM45686_FIFO_SAMPLE_BYTES))
        {
            return -RT_ERROR;
        }

        *fifo_count_bytes = (uint16_t)(fifo_count_samples
                                       * ICM45686_FIFO_SAMPLE_BYTES);
    }

    return RT_EOK;
}

rt_err_t icm45686_read_fifo_dma(icm45686_t *device,
                                icm45686_transfer_fn transfer_dma,
                                uint8_t *tx,
                                uint8_t *rx,
                                rt_size_t fifo_data_length)
{
    rt_size_t index;

    if (device == RT_NULL || device->bus.transfer == RT_NULL
        || transfer_dma == RT_NULL || tx == RT_NULL || rx == RT_NULL
        || fifo_data_length == 0U || fifo_data_length >= UINT16_MAX)
    {
        return -RT_ERROR;
    }

    tx[0] = ICM45686_SPI_READ_BIT | ICM45686_REG_FIFO_DATA;

    for (index = 1U; index <= fifo_data_length; index++)
    {
        tx[index] = 0U;
    }

    return transfer_dma(tx, rx, fifo_data_length + 1U);
}

rt_err_t icm45686_verify_acquisition_config(icm45686_t *device)
{
    uint8_t actual_value;
    rt_err_t result;
    rt_size_t index;

    result = icm45686_read_indirect_reg(
        device,
        ICM45686_INDIRECT_SREG_CTRL_ADDRESS,
        &actual_value);
    if (result != RT_EOK)
    {
        return result;
    }

    if (actual_value != ICM45686_SREG_CTRL_DATA_BIG_ENDIAN)
    {
        return -RT_ERROR;
    }

    for (index = 0U;
         index < sizeof(icm45686_acquisition_config_expectations)
                 / sizeof(icm45686_acquisition_config_expectations[0]);
         index++)
    {
        result = icm45686_read_reg(
            device,
            icm45686_acquisition_config_expectations[index].register_address,
            &actual_value,
            1U);
        if (result != RT_EOK)
        {
            return result;
        }

        if (actual_value
            != icm45686_acquisition_config_expectations[index].expected_value)
        {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

rt_err_t icm45686_configure_int1_fifo_watermark(
    icm45686_t *device)
{
    rt_err_t result;
    const uint8_t interrupts_disabled = 0x00U;
    const uint8_t int1_output =
        ICM45686_INT1_CONFIG2_PUSH_PULL_PULSE_ACTIVE_HIGH;
    const uint8_t pulse_duration =
        ICM45686_INT1_PULSE_DURATION_100US;
    const uint8_t fifo_threshold_enable =
        ICM45686_INT1_CONFIG0_FIFO_THS_ENABLE;

    if (device == RT_NULL)
    {
        return -RT_ERROR;
    }

    /*
     * 手册要求：修改 INT1 mode/polarity 前，
     * 必须先关闭对应串行接口的全部中断源。
     */
    result = icm45686_write_reg(device,
                                ICM45686_REG_INT1_CONFIG0,
                                &interrupts_disabled,
                                1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device,
                                ICM45686_REG_INT1_CONFIG1,
                                &interrupts_disabled,
                                1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_reg(device,
                                ICM45686_REG_INT1_CONFIG2,
                                &int1_output,
                                1U);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_write_indirect_reg(
        device,
        ICM45686_INDIRECT_INT1_PULSE_ADDRESS,
        pulse_duration);
    if (result != RT_EOK)
    {
        return result;
    }

    return icm45686_write_reg(device,
                              ICM45686_REG_INT1_CONFIG0,
                              &fifo_threshold_enable,
                              1U);
}

rt_err_t icm45686_configure_acquisition(icm45686_t *device)
{
    rt_err_t result;

    if (device == RT_NULL)
    {
        return -RT_ERROR;
    }

    result = icm45686_probe(device);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_configure_big_endian(device);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_configure_1600hz_16g_2000dps(device);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_configure_fifo_accel_gyro_32_samples(device);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_configure_int1_fifo_watermark(device);
    if (result != RT_EOK)
    {
        return result;
    }

    return icm45686_verify_acquisition_config(device);
}
