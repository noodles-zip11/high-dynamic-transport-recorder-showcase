#ifndef TRANSPORT_RECORDER_STORAGE_SERVICE_H
#define TRANSPORT_RECORDER_STORAGE_SERVICE_H

#include <stdint.h>

#include "event_log.h"
#include "event_service.h"

rt_err_t storage_service_start(void);
void storage_service_get_event_sink(event_export_sink_t *sink);
rt_bool_t storage_service_is_ready(void);
rt_err_t storage_service_event_begin(uint32_t event_id, uint32_t ev01_length);
rt_err_t storage_service_event_write(const uint8_t *data,
                                     rt_size_t length,
                                     void *context);
rt_err_t storage_service_event_abort(void);
rt_err_t storage_service_get_status(event_log_status_t *status);
rt_err_t storage_service_get_recovery_info(event_log_recovery_info_t *info);
rt_err_t storage_service_get_event_info(uint32_t event_id,
                                        event_log_event_info_t *info);
rt_err_t storage_service_list_events(uint32_t after_event_id,
                                     uint16_t maximum_count,
                                     event_log_event_info_t *events,
                                     uint16_t events_capacity,
                                     uint16_t *event_count,
                                     uint32_t *next_event_id);
rt_err_t storage_service_verify_event(uint32_t event_id);
rt_err_t storage_service_read_event(uint32_t event_id,
                                    uint32_t ev01_offset,
                                    uint8_t *data,
                                    uint32_t length,
                                    uint32_t *read_length);
rt_err_t storage_service_request_format(rt_tick_t now);
rt_err_t storage_service_confirm_format(rt_tick_t now);

#endif
