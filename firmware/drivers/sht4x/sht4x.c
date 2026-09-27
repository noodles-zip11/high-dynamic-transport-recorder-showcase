#include "sht4x.h"

uint8_t sht4x_crc8(const uint8_t *data, rt_size_t length)
{
    uint8_t crc = 0xFFU;
    rt_size_t index;

    for (index = 0U; index < length; ++index)
    {
        uint8_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 0x80U) != 0U ? (uint8_t)((crc << 1U) ^ 0x31U)
                                       : (uint8_t)(crc << 1U);
        }
    }

    return crc;
}

rt_err_t sht4x_init(sht4x_t *device, const sht4x_bus_t *bus, uint8_t address)
{
    if (device == RT_NULL || bus == RT_NULL || bus->write == RT_NULL
        || bus->read == RT_NULL || bus->delay_ms == RT_NULL
        || (address != SHT4X_ADDRESS_0X44 && address != SHT4X_ADDRESS_0X45))
    {
        return -RT_ERROR;
    }

    device->bus = *bus;
    device->address = address;
    return RT_EOK;
}

rt_err_t sht4x_probe(const sht4x_bus_t *bus, uint8_t *address_out)
{
    static const uint8_t addresses[] = {
        SHT4X_ADDRESS_0X44,
        SHT4X_ADDRESS_0X45,
    };
    uint8_t command = SHT4X_MEASURE_HIGH_PRECISION_COMMAND;
    uint8_t found_address = 0U;
    uint8_t found_count = 0U;
    uint8_t index;

    if (bus == RT_NULL || address_out == RT_NULL || bus->write == RT_NULL)
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < sizeof(addresses); ++index)
    {
        if (bus->write(addresses[index], &command, sizeof(command), bus->context) == RT_EOK)
        {
            found_address = addresses[index];
            found_count++;
        }
    }

    if (found_count != 1U)
    {
        return -RT_ERROR;
    }

    *address_out = found_address;
    return RT_EOK;
}

rt_err_t sht4x_measure(sht4x_t *device, sht4x_measurement_t *measurement)
{
    uint8_t command = SHT4X_MEASURE_HIGH_PRECISION_COMMAND;
    uint8_t response[6];
    uint16_t temperature_raw;
    uint16_t humidity_raw;
    int32_t humidity_milli_rh;

    if (device == RT_NULL || measurement == RT_NULL || device->bus.write == RT_NULL
        || device->bus.read == RT_NULL || device->bus.delay_ms == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (device->bus.write(device->address, &command, sizeof(command),
                          device->bus.context) != RT_EOK)
    {
        return -RT_ERROR;
    }

    device->bus.delay_ms(10U, device->bus.context);
    if (device->bus.read(device->address, response, sizeof(response),
                         device->bus.context) != RT_EOK
        || sht4x_crc8(response, 2U) != response[2]
        || sht4x_crc8(&response[3], 2U) != response[5])
    {
        return -RT_ERROR;
    }

    temperature_raw = ((uint16_t)response[0] << 8U) | response[1];
    humidity_raw = ((uint16_t)response[3] << 8U) | response[4];
    measurement->temperature_centi_c = (int16_t)(-4500
        + ((int32_t)17500 * temperature_raw + 32767) / 65535);
    humidity_milli_rh = -6000
                         + (int32_t)(((uint64_t)125000U * humidity_raw) / 65535U);
    if (humidity_milli_rh < 0)
    {
        humidity_milli_rh = 0;
    }
    else if (humidity_milli_rh > 100000)
    {
        humidity_milli_rh = 100000;
    }
    measurement->humidity_milli_rh = (uint32_t)humidity_milli_rh;
    return RT_EOK;
}
