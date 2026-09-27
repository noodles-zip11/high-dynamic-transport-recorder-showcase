#ifndef TRANSPORT_RECORDER_HEALTH_SERVICE_H
#define TRANSPORT_RECORDER_HEALTH_SERVICE_H

#include <stdint.h>

#include <rtthread.h>

typedef enum
{
    HEALTH_STARTING = 0,
    HEALTH_HEALTHY,
    HEALTH_DEGRADED,
    HEALTH_FAULT,
} health_state_t;

typedef enum
{
    HEALTH_POWER_STATE_UNAVAILABLE = 0,
    HEALTH_POWER_STATE_NORMAL,
    HEALTH_POWER_STATE_DROOP,
} health_power_state_t;

typedef struct
{
    rt_bool_t ready;
    uint32_t progress_sequence;
    uint32_t error_count;
} health_provider_status_t;

typedef struct
{
    health_provider_status_t acquisition;
    health_provider_status_t event;
    health_provider_status_t storage;
    rt_bool_t storage_progress_required;
    rt_bool_t utc_valid;
    int64_t utc_unix_seconds;
    uint32_t time_epoch_id;
    rt_bool_t environment_valid;
    rt_bool_t environment_fresh;
    int16_t temperature_centi_c;
    uint32_t humidity_milli_rh;
    uint32_t environment_age_seconds;
    uint32_t reset_raw_flags;
    health_power_state_t power_state;
    uint16_t context_valid_flags;
    uint32_t free_log_bytes;
    uint16_t sample_pool_min_free;
    uint32_t imu_transport_error_count;
    uint32_t imu_dma_error_count;
    uint32_t sample_pool_backpressure_count;
    uint32_t storage_error_count;
    uint32_t event_export_error_count;
} health_inputs_t;

typedef struct
{
    health_state_t state;
    rt_bool_t utc_valid;
    int64_t utc_unix_seconds;
    uint32_t time_epoch_id;
    rt_bool_t environment_valid;
    rt_bool_t environment_fresh;
    int16_t temperature_centi_c;
    uint32_t humidity_milli_rh;
    uint32_t environment_age_seconds;
    uint32_t reset_raw_flags;
    health_power_state_t power_state;
    uint16_t context_valid_flags;
    uint32_t free_log_bytes;
    uint16_t sample_pool_min_free;
    uint32_t imu_transport_error_count;
    uint32_t imu_dma_error_count;
    uint32_t sample_pool_backpressure_count;
    uint32_t storage_error_count;
    uint32_t event_export_error_count;
    uint32_t last_fault_code;
    uint32_t transition_sequence;
} health_snapshot_t;

typedef struct
{
    struct rt_mutex snapshot_mutex;
    rt_bool_t snapshot_mutex_initialized;
    health_snapshot_t snapshot;
    uint32_t last_acquisition_progress;
    uint32_t last_event_progress;
    uint32_t last_storage_progress;
    uint32_t last_acquisition_errors;
    uint32_t last_event_errors;
    uint32_t last_storage_errors;
    uint64_t last_acquisition_advance_us;
    uint64_t last_event_advance_us;
    uint64_t last_storage_advance_us;
    uint32_t last_backpressure_count;
    uint64_t backpressure_rising_since_us;
    uint8_t fault_good_passes;
    rt_bool_t initialized;
} health_service_t;

void health_service_init(health_service_t *service);
void health_service_evaluate(health_service_t *service,
                             const health_inputs_t *inputs,
                             uint64_t now_us);
void health_service_get_snapshot(const health_service_t *service,
                                 health_snapshot_t *snapshot_out);
rt_bool_t health_service_watchdog_feed_allowed(const health_service_t *service);

#endif
