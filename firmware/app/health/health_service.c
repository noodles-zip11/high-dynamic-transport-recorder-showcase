#include "health_service.h"

#define HEALTH_CORE_PROGRESS_TIMEOUT_US UINT64_C(3000000)
#define HEALTH_STORAGE_PROGRESS_TIMEOUT_US UINT64_C(10000000)
#define HEALTH_FAULT_ACQUISITION_STALE 1U
#define HEALTH_FAULT_EVENT_STALE 2U
#define HEALTH_FAULT_STORAGE_STALE 3U
#define HEALTH_FAULT_PROVIDER_UNAVAILABLE 4U
#define HEALTH_FAULT_PROVIDER_ERROR 5U
#define HEALTH_FAULT_BACKPRESSURE 6U

static rt_bool_t health_service_lock(health_service_t *service)
{
    return service != RT_NULL && service->snapshot_mutex_initialized
           && rt_mutex_take(&service->snapshot_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void health_service_unlock(health_service_t *service)
{
    (void)rt_mutex_release(&service->snapshot_mutex);
}

static rt_bool_t health_progress_advanced(uint32_t current,
                                          uint32_t *previous,
                                          uint64_t now_us,
                                          uint64_t *last_advance_us)
{
    if (current != *previous)
    {
        *previous = current;
        *last_advance_us = now_us;
        return RT_TRUE;
    }
    return RT_FALSE;
}

static void health_set_state(health_service_t *service,
                             health_state_t state,
                             uint32_t fault_code)
{
    if (service->snapshot.state != state)
    {
        service->snapshot.state = state;
        service->snapshot.transition_sequence++;
    }
    if (fault_code != 0U)
    {
        service->snapshot.last_fault_code = fault_code;
    }
}

void health_service_init(health_service_t *service)
{
    if (service != RT_NULL)
    {
        *service = (health_service_t){0};
        service->snapshot.state = HEALTH_STARTING;
        if (rt_mutex_init(&service->snapshot_mutex, "health", RT_IPC_FLAG_PRIO)
            == RT_EOK)
        {
            service->snapshot_mutex_initialized = RT_TRUE;
        }
    }
}

static void health_service_evaluate_locked(health_service_t *service,
                                           const health_inputs_t *inputs,
                                           uint64_t now_us)
{
    uint32_t fault_code = 0U;

    if (service == RT_NULL || inputs == RT_NULL)
    {
        return;
    }

    service->snapshot.utc_valid = inputs->utc_valid;
    service->snapshot.utc_unix_seconds = inputs->utc_unix_seconds;
    service->snapshot.time_epoch_id = inputs->time_epoch_id;
    service->snapshot.environment_valid = inputs->environment_valid;
    service->snapshot.environment_fresh = inputs->environment_fresh;
    service->snapshot.temperature_centi_c = inputs->temperature_centi_c;
    service->snapshot.humidity_milli_rh = inputs->humidity_milli_rh;
    service->snapshot.environment_age_seconds = inputs->environment_age_seconds;
    service->snapshot.reset_raw_flags = inputs->reset_raw_flags;
    service->snapshot.power_state = inputs->power_state;
    service->snapshot.context_valid_flags = inputs->context_valid_flags;
    service->snapshot.free_log_bytes = inputs->free_log_bytes;
    service->snapshot.sample_pool_min_free = inputs->sample_pool_min_free;
    service->snapshot.imu_transport_error_count = inputs->imu_transport_error_count;
    service->snapshot.imu_dma_error_count = inputs->imu_dma_error_count;
    service->snapshot.sample_pool_backpressure_count = inputs->sample_pool_backpressure_count;
    service->snapshot.storage_error_count = inputs->storage_error_count;
    service->snapshot.event_export_error_count = inputs->event_export_error_count;

    if (!inputs->acquisition.ready || !inputs->event.ready || !inputs->storage.ready)
    {
        health_set_state(service, service->initialized ? HEALTH_FAULT : HEALTH_STARTING,
                         service->initialized ? HEALTH_FAULT_PROVIDER_UNAVAILABLE : 0U);
        return;
    }

    if (!service->initialized)
    {
        service->last_acquisition_progress = inputs->acquisition.progress_sequence;
        service->last_event_progress = inputs->event.progress_sequence;
        service->last_storage_progress = inputs->storage.progress_sequence;
        service->last_acquisition_errors = inputs->acquisition.error_count;
        service->last_event_errors = inputs->event.error_count;
        service->last_storage_errors = inputs->storage.error_count;
        service->last_backpressure_count = inputs->sample_pool_backpressure_count;
        service->last_acquisition_advance_us = now_us;
        service->last_event_advance_us = now_us;
        service->last_storage_advance_us = now_us;
        service->initialized = RT_TRUE;
    }
    else
    {
        (void)health_progress_advanced(inputs->acquisition.progress_sequence,
                                       &service->last_acquisition_progress,
                                       now_us, &service->last_acquisition_advance_us);
        (void)health_progress_advanced(inputs->event.progress_sequence,
                                       &service->last_event_progress,
                                       now_us, &service->last_event_advance_us);
        (void)health_progress_advanced(inputs->storage.progress_sequence,
                                       &service->last_storage_progress,
                                       now_us, &service->last_storage_advance_us);
        if (inputs->acquisition.error_count > service->last_acquisition_errors
            || inputs->event.error_count > service->last_event_errors
            || inputs->storage.error_count > service->last_storage_errors)
        {
            fault_code = HEALTH_FAULT_PROVIDER_ERROR;
        }
        service->last_acquisition_errors = inputs->acquisition.error_count;
        service->last_event_errors = inputs->event.error_count;
        service->last_storage_errors = inputs->storage.error_count;

        if (inputs->sample_pool_backpressure_count > service->last_backpressure_count)
        {
            if (service->backpressure_rising_since_us == 0U)
            {
                service->backpressure_rising_since_us = now_us;
            }
        }
        else
        {
            service->backpressure_rising_since_us = 0U;
        }
        service->last_backpressure_count = inputs->sample_pool_backpressure_count;
    }

    if (fault_code == 0U
        && now_us - service->last_acquisition_advance_us > HEALTH_CORE_PROGRESS_TIMEOUT_US)
    {
        fault_code = HEALTH_FAULT_ACQUISITION_STALE;
    }
    else if (fault_code == 0U
             && now_us - service->last_event_advance_us > HEALTH_CORE_PROGRESS_TIMEOUT_US)
    {
        fault_code = HEALTH_FAULT_EVENT_STALE;
    }
    else if (fault_code == 0U && inputs->storage_progress_required
             && now_us - service->last_storage_advance_us > HEALTH_STORAGE_PROGRESS_TIMEOUT_US)
    {
        fault_code = HEALTH_FAULT_STORAGE_STALE;
    }
    else if (fault_code == 0U && service->backpressure_rising_since_us != 0U
             && now_us - service->backpressure_rising_since_us >= HEALTH_CORE_PROGRESS_TIMEOUT_US)
    {
        fault_code = HEALTH_FAULT_BACKPRESSURE;
    }

    if (fault_code != 0U)
    {
        service->fault_good_passes = 0U;
        health_set_state(service, HEALTH_FAULT, fault_code);
    }
    else if (service->snapshot.state == HEALTH_FAULT && service->fault_good_passes++ < 2U)
    {
        return;
    }
    else if (!inputs->utc_valid || !inputs->environment_valid || !inputs->environment_fresh
             || inputs->power_state == HEALTH_POWER_STATE_DROOP)
    {
        health_set_state(service, HEALTH_DEGRADED, 0U);
    }
    else
    {
        health_set_state(service, HEALTH_HEALTHY, 0U);
    }
}

void health_service_evaluate(health_service_t *service,
                             const health_inputs_t *inputs,
                             uint64_t now_us)
{
    if (inputs == RT_NULL || !health_service_lock(service))
    {
        return;
    }
    health_service_evaluate_locked(service, inputs, now_us);
    health_service_unlock(service);
}

rt_bool_t health_service_watchdog_feed_allowed(const health_service_t *service)
{
    health_service_t *mutable_service = (health_service_t *)service;
    rt_bool_t allowed;

    if (!health_service_lock(mutable_service))
    {
        return RT_FALSE;
    }
    allowed = service->initialized
              && (service->snapshot.state == HEALTH_HEALTHY
                  || service->snapshot.state == HEALTH_DEGRADED);
    health_service_unlock(mutable_service);
    return allowed;
}

void health_service_get_snapshot(const health_service_t *service,
                                 health_snapshot_t *snapshot_out)
{
    if (service != RT_NULL && snapshot_out != RT_NULL)
    {
        health_service_t *mutable_service = (health_service_t *)service;

        if (health_service_lock(mutable_service))
        {
            *snapshot_out = service->snapshot;
            health_service_unlock(mutable_service);
        }
    }
}
