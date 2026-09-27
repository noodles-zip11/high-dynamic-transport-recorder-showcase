#ifndef TRANSPORT_RECORDER_EVENT_SERVICE_H
#define TRANSPORT_RECORDER_EVENT_SERVICE_H

#include <stdint.h>

#include "event_assembler.h"
#include "event_export_debug.h"
#include "health_service.h"

typedef enum
{
    EVENT_EXPORT_SINK_UNAVAILABLE = 0,
    EVENT_EXPORT_SINK_READY,
    EVENT_EXPORT_SINK_ERROR,
} event_export_sink_state_t;

typedef struct
{
    event_export_sink_state_t state;
    uint32_t next_event_id;
} event_export_sink_status_t;

typedef struct
{
    rt_bool_t (*is_ready)(void *context);
    rt_err_t (*begin)(void *context, uint32_t event_id, uint32_t ev01_length);
    event_export_write_fn write;
    rt_err_t (*abort)(void *context);
    rt_err_t (*get_status)(void *context, event_export_sink_status_t *status);
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    rt_err_t (*verify)(void *context, uint32_t event_id);
    rt_err_t (*read)(void *context,
                     uint32_t event_id,
                     uint32_t ev01_offset,
                     uint8_t *data,
                     uint32_t length,
                     uint32_t *read_length);
#endif
    void *context;
} event_export_sink_t;

typedef struct
{
    event_state_t state;
    uint8_t pretrigger_block_count;
    uint32_t busy_trigger_count;
    uint32_t processed_block_count;
    uint32_t export_error_count;
    uint32_t resource_reject_count;
    uint32_t natural_trigger_count;
    uint32_t cooldown_rejection_count;
    uint32_t cooldown_remaining_ms;
} event_service_stats_t;

rt_err_t event_service_start(const event_export_sink_t *sink);

void event_service_set_health_service(const health_service_t *service);

void event_service_get_stats(event_service_stats_t *stats);

rt_err_t event_service_request_test_trigger(void);

rt_err_t event_service_request_test_trigger_after(uint32_t delay_ms);

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
rt_err_t event_service_inject_quality_case(uint32_t event_case);
rt_err_t event_service_inject_queue_pressure(void);
#endif

rt_err_t event_service_export_ready(void);

rt_err_t event_service_clear_ready(void);

#endif
