#include "environment_service.h"

#include <limits.h>
#include <string.h>

static rt_bool_t environment_service_lock(environment_service_t *service)
{
    return service != RT_NULL && service->snapshot_mutex_initialized
           && rt_mutex_take(&service->snapshot_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void environment_service_unlock(environment_service_t *service)
{
    (void)rt_mutex_release(&service->snapshot_mutex);
}

static void environment_service_record_error(environment_service_t *service,
                                             rt_err_t error)
{
    if (!environment_service_lock(service))
    {
        return;
    }
    if (service->snapshot.error_count != UINT32_MAX)
    {
        service->snapshot.error_count++;
    }
    service->snapshot.last_error = error;
    environment_service_unlock(service);
}

rt_err_t environment_service_init(environment_service_t *service,
                                  const sht4x_bus_t *bus)
{
    uint8_t address;
    rt_err_t result;

    if (service == RT_NULL || bus == RT_NULL)
    {
        return -RT_ERROR;
    }

    memset(service, 0, sizeof(*service));
    service->snapshot.last_error = RT_EOK;
    if (rt_mutex_init(&service->snapshot_mutex, "env", RT_IPC_FLAG_PRIO)
        != RT_EOK)
    {
        return -RT_ERROR;
    }
    service->snapshot_mutex_initialized = RT_TRUE;

    result = sht4x_probe(bus, &address);
    if (result != RT_EOK)
    {
        environment_service_record_error(service, result);
        return result;
    }

    result = sht4x_init(&service->sht4x, bus, address);
    if (result != RT_EOK)
    {
        environment_service_record_error(service, result);
        return result;
    }

    if (!environment_service_lock(service))
    {
        return -RT_ERROR;
    }
    service->snapshot.address = address;
    environment_service_unlock(service);
    service->initialized = RT_TRUE;
    return RT_EOK;
}

rt_err_t environment_service_poll(environment_service_t *service,
                                  uint64_t now_us)
{
    sht4x_measurement_t measurement;
    rt_err_t result;

    if (service == RT_NULL || !service->initialized)
    {
        return -RT_ERROR;
    }

    result = sht4x_measure(&service->sht4x, &measurement);
    if (result != RT_EOK)
    {
        environment_service_record_error(service, result);
        return result;
    }

    if (!environment_service_lock(service))
    {
        return -RT_ERROR;
    }
    service->snapshot.valid = RT_TRUE;
    service->snapshot.temperature_centi_c = measurement.temperature_centi_c;
    service->snapshot.humidity_milli_rh = measurement.humidity_milli_rh;
    service->snapshot.last_sample_monotonic_us = now_us;
    service->snapshot.last_error = RT_EOK;
    environment_service_unlock(service);
    return RT_EOK;
}

rt_err_t environment_service_get_snapshot(const environment_service_t *service,
                                          environment_snapshot_t *snapshot_out)
{
    environment_service_t *mutable_service = (environment_service_t *)service;

    if (snapshot_out == RT_NULL || !environment_service_lock(mutable_service))
    {
        return -RT_ERROR;
    }

    *snapshot_out = service->snapshot;
    environment_service_unlock(mutable_service);
    return RT_EOK;
}
