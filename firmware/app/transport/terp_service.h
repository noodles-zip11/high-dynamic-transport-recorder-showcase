#ifndef TRANSPORT_RECORDER_TERP_SERVICE_H
#define TRANSPORT_RECORDER_TERP_SERVICE_H

#include <stdint.h>

#include "event_log.h"
#include "health_service.h"
#include "ota_download_service.h"
#include "ota_model_lifecycle.h"
#include "terp_device.h"
#include "terp_parser.h"
#include "time_service.h"
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "reliability_evidence.h"
#endif

typedef int (*terp_service_write_fn)(const uint8_t *data,
                                     uint32_t length,
                                     void *context);

typedef struct
{
    uint32_t tx_frame_count;
    uint32_t tx_error_count;
} terp_service_stats_t;

typedef struct
{
    terp_device_t device;
    terp_device_ops_t device_ops;
    terp_parser_t parser;
    const health_service_t *health_service;
    time_service_t *time_service;
    ota_download_service_t ota_download;
    ota_download_service_t recovery_download;
    ota_model_lifecycle_t model_lifecycle;
    terp_service_write_fn write;
    void *write_context;
    event_log_event_info_t list_storage_events[TERP_LIST_MAXIMUM_COUNT];
    uint8_t work_buffer[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response_buffer[TERP_MAX_FRAME_BYTES];
    terp_service_stats_t stats;
    uint8_t ota_available;
    uint8_t model_ota_available;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    reliability_evidence_t reliability_evidence;
#endif
} terp_service_t;

void terp_service_init(terp_service_t *service,
                       const terp_device_info_t *device_info,
                       const health_service_t *health_service,
                       time_service_t *time_service,
                       terp_service_write_fn write,
                       void *write_context);
void terp_service_receive(terp_service_t *service,
                          const uint8_t *data,
                          uint32_t length);
void terp_service_on_timeout(terp_service_t *service);
void terp_service_get_stats(const terp_service_t *service,
                            terp_service_stats_t *stats);
void terp_service_set_runtime_model_activation(
    terp_service_t *service,
    ota_model_runtime_prepare_fn prepare_runtime_model,
    ota_model_runtime_publish_fn publish_runtime_model,
    ota_model_runtime_abort_fn abort_runtime_model,
    ota_model_runtime_quarantine_fn quarantine_runtime_model,
    void *context);
const ai_model_t *terp_service_get_runtime_model(
    const terp_service_t *service);

#endif
