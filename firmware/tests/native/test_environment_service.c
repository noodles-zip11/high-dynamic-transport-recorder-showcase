#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rtthread.h>

#include "environment_service.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "environment_service: %s\n", message);
        return 0;
    }

    return 1;
}

typedef struct
{
    uint8_t response[6];
    uint8_t acknowledged_address_mask;
    uint32_t write_count;
    uint32_t read_count;
    rt_err_t read_result;
} fake_bus_t;

static rt_err_t fake_write(uint8_t address,
                           const uint8_t *data,
                           rt_size_t length,
                           void *context)
{
    fake_bus_t *bus = context;

    if (bus == RT_NULL || data == RT_NULL || length != 1U
        || data[0] != SHT4X_MEASURE_HIGH_PRECISION_COMMAND)
    {
        return -RT_ERROR;
    }

    bus->write_count++;
    if ((bus->acknowledged_address_mask
         & (uint8_t)(1U << (address - SHT4X_ADDRESS_0X44))) == 0U)
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

    (void)address;
    if (bus == RT_NULL || data == RT_NULL || length != sizeof(bus->response))
    {
        return -RT_ERROR;
    }

    bus->read_count++;
    if (bus->read_result != RT_EOK)
    {
        return bus->read_result;
    }

    memcpy(data, bus->response, sizeof(bus->response));
    return RT_EOK;
}

static void fake_delay(uint32_t milliseconds, void *context)
{
    (void)milliseconds;
    (void)context;
}

static void set_valid_response(fake_bus_t *bus, uint16_t temperature_raw,
                               uint16_t humidity_raw)
{
    bus->response[0] = (uint8_t)(temperature_raw >> 8U);
    bus->response[1] = (uint8_t)temperature_raw;
    bus->response[2] = sht4x_crc8(bus->response, 2U);
    bus->response[3] = (uint8_t)(humidity_raw >> 8U);
    bus->response[4] = (uint8_t)humidity_raw;
    bus->response[5] = sht4x_crc8(&bus->response[3], 2U);
}

static int test_init_probes_once_and_publishes_selected_address(void)
{
    fake_bus_t fake = {.acknowledged_address_mask = 2U};
    sht4x_bus_t bus = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
        .context = &fake,
    };
    environment_service_t service = {0};
    environment_snapshot_t snapshot = {0};

    if (!expect(environment_service_init(&service, &bus) == RT_EOK,
                "initialization must accept exactly one SHT4x responder")
        || !expect(service.snapshot_mutex_initialized,
                   "initialization must create the snapshot publication mutex")
        || !expect(environment_service_get_snapshot(&service, &snapshot) == RT_EOK,
                   "initialization must publish a snapshot")
        || !expect(!snapshot.valid && snapshot.address == SHT4X_ADDRESS_0X45,
                   "the selected address must be published before a sample succeeds")
        || !expect(fake.write_count == 2U,
                   "initialization must probe exactly the two supported addresses")
        || !expect(service.snapshot_mutex.take_count
                   == service.snapshot_mutex.release_count
                   && service.snapshot_mutex.take_count >= 2U,
                   "snapshot publication and copy must use the same mutex"))
    {
        return 0;
    }

    return 1;
}

static int test_poll_publishes_successful_measurement_at_caller_time(void)
{
    fake_bus_t fake = {.acknowledged_address_mask = 1U};
    sht4x_bus_t bus = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
        .context = &fake,
    };
    environment_service_t service = {0};
    environment_snapshot_t snapshot = {0};

    set_valid_response(&fake, 0x6666U, 0x8000U);
    if (!expect(environment_service_init(&service, &bus) == RT_EOK,
                "the fake sensor must initialize")
        || !expect(environment_service_poll(&service, 1234567U) == RT_EOK,
                   "a valid SHT4x measurement must poll successfully")
        || !expect(environment_service_get_snapshot(&service, &snapshot) == RT_EOK,
                   "a successful poll must publish a snapshot")
        || !expect(snapshot.valid
                   && snapshot.temperature_centi_c == 2500
                   && snapshot.humidity_milli_rh == 56500U
                   && snapshot.last_sample_monotonic_us == 1234567U,
                   "the snapshot must preserve converted values and caller time")
        || !expect(snapshot.error_count == 0U && snapshot.last_error == RT_EOK,
                   "a first successful sample must have no error")
        || !expect(fake.read_count == 1U,
                   "polling must take exactly one SHT4x measurement"))
    {
        return 0;
    }

    return 1;
}

static int test_failed_poll_keeps_last_valid_sample_and_records_error(void)
{
    fake_bus_t fake = {.acknowledged_address_mask = 1U};
    sht4x_bus_t bus = {
        .write = fake_write,
        .read = fake_read,
        .delay_ms = fake_delay,
        .context = &fake,
    };
    environment_service_t service = {0};
    environment_snapshot_t snapshot = {0};

    set_valid_response(&fake, 0x6666U, 0x8000U);
    if (!expect(environment_service_init(&service, &bus) == RT_EOK,
                "the fake sensor must initialize")
        || !expect(environment_service_poll(&service, 100U) == RT_EOK,
                   "the first sample must succeed"))
    {
        return 0;
    }

    fake.read_result = -RT_ERROR;
    if (!expect(environment_service_poll(&service, 200U) != RT_EOK,
                "a failed SHT4x read must fail the poll")
        || !expect(environment_service_get_snapshot(&service, &snapshot) == RT_EOK,
                   "a failed poll must still publish the previous snapshot")
        || !expect(snapshot.valid
                   && snapshot.temperature_centi_c == 2500
                   && snapshot.humidity_milli_rh == 56500U
                   && snapshot.last_sample_monotonic_us == 100U,
                   "a failed poll must not overwrite the last valid sample")
        || !expect(snapshot.error_count == 1U && snapshot.last_error != RT_EOK,
                   "a failed poll must increment and expose the error state"))
    {
        return 0;
    }

    return 1;
}

int main(void)
{
    if (!test_init_probes_once_and_publishes_selected_address()
        || !test_poll_publishes_successful_measurement_at_caller_time()
        || !test_failed_poll_keeps_last_valid_sample_and_records_error())
    {
        return 1;
    }

    puts("environment_service: PASS");
    return 0;
}
