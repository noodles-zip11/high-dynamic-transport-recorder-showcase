#ifndef TRANSPORT_RECORDER_ENVIRONMENT_SERVICE_H
#define TRANSPORT_RECORDER_ENVIRONMENT_SERVICE_H

#include <stdint.h>

#include "sht4x.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    rt_bool_t valid;
    int16_t temperature_centi_c;
    uint32_t humidity_milli_rh;
    uint64_t last_sample_monotonic_us;
    uint32_t error_count;
    rt_err_t last_error;
    uint8_t address;
} environment_snapshot_t;

typedef struct
{
    sht4x_t sht4x;
    struct rt_mutex snapshot_mutex;
    rt_bool_t snapshot_mutex_initialized;
    environment_snapshot_t snapshot;
    rt_bool_t initialized;
} environment_service_t;

rt_err_t environment_service_init(environment_service_t *service,
                                  const sht4x_bus_t *bus);
rt_err_t environment_service_poll(environment_service_t *service,
                                  uint64_t now_us);
rt_err_t environment_service_get_snapshot(const environment_service_t *service,
                                          environment_snapshot_t *snapshot_out);

#ifdef __cplusplus
}
#endif

#endif
