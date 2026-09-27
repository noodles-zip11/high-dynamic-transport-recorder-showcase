#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>

#include "sht4x.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "sht4x: %s\n", message);
        return 0;
    }

    return 1;
}

static int test_crc8_matches_sensirion_reference_vector(void)
{
    static const uint8_t data[] = {0xBEU, 0xEFU};

    return expect(sht4x_crc8(data, sizeof(data)) == 0x92U,
                  "CRC-8 must use polynomial 0x31 and initial value 0xFF");
}

typedef struct
{
    uint8_t response[6];
    uint8_t command;
    uint8_t address;
    uint32_t delay_ms;
    uint8_t acknowledged_address_mask;
} fake_bus_t;

static rt_err_t fake_write(uint8_t address,
                           const uint8_t *data,
                           rt_size_t length,
                           void *context)
{
    fake_bus_t *bus = context;

    if (bus == RT_NULL || data == RT_NULL || length != 1U)
    {
        return -RT_ERROR;
    }
    bus->address = address;
    bus->command = data[0];
    if ((bus->acknowledged_address_mask & (uint8_t)(1U << (address - SHT4X_ADDRESS_0X44))) == 0U)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t fake_read(uint8_t address,
                          uint8_t *data,
                          rt_size_t length,
                          void *context)
{
    fake_bus_t *bus = context;

    if (bus == RT_NULL || data == RT_NULL || length != sizeof(bus->response))
    {
        return -RT_ERROR;
    }
    if (address != bus->address)
    {
        return -RT_ERROR;
    }
    memcpy(data, bus->response, sizeof(bus->response));
    return RT_EOK;
}

static void fake_delay(uint32_t milliseconds, void *context)
{
    fake_bus_t *bus = context;

    bus->delay_ms = milliseconds;
}

static int test_measurement_uses_high_precision_command_and_converts_data(void)
{
    fake_bus_t fake = {0};
    sht4x_bus_t bus = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
        .context = &fake,
    };
    sht4x_t device = {0};
    sht4x_measurement_t measurement = {0};

    fake.response[0] = 0x66U;
    fake.acknowledged_address_mask = 1U;
    fake.response[1] = 0x66U;
    fake.response[2] = sht4x_crc8(fake.response, 2U);
    fake.response[3] = 0x80U;
    fake.response[4] = 0x00U;
    fake.response[5] = sht4x_crc8(&fake.response[3], 2U);

    if (!expect(sht4x_init(&device, &bus, SHT4X_ADDRESS_0X44) == RT_EOK,
                "a complete bus and supported address must initialize")
        || !expect(sht4x_measure(&device, &measurement) == RT_EOK,
                   "a response with both valid CRC bytes must measure")
        || !expect(fake.address == SHT4X_ADDRESS_0X44
                   && fake.command == SHT4X_MEASURE_HIGH_PRECISION_COMMAND,
                   "measurement must issue 0xFD to the selected address")
        || !expect(fake.delay_ms == 10U,
                   "measurement must wait the 10 ms high-precision interval")
        || !expect(measurement.temperature_centi_c == 2500
                   && measurement.humidity_milli_rh == 56500U,
                   "raw words must convert to centi-C and milli-percent RH"))
    {
        return 0;
    }

    return 1;
}

static int test_probe_accepts_exactly_one_of_the_two_supported_addresses(void)
{
    fake_bus_t fake = {.acknowledged_address_mask = 2U};
    sht4x_bus_t bus = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
        .context = &fake,
    };
    uint8_t address = 0U;

    if (!expect(sht4x_probe(&bus, &address) == RT_EOK
                && address == SHT4X_ADDRESS_0X45,
                "probe must accept the sole responder at 0x45"))
    {
        return 0;
    }

    fake.acknowledged_address_mask = 3U;
    return expect(sht4x_probe(&bus, &address) != RT_EOK,
                  "probe must reject two responders instead of choosing one");
}

int main(void)
{
    if (!test_crc8_matches_sensirion_reference_vector()
        || !test_measurement_uses_high_precision_command_and_converts_data()
        || !test_probe_accepts_exactly_one_of_the_two_supported_addresses())
    {
        return 1;
    }

    puts("sht4x: PASS");
    return 0;
}
