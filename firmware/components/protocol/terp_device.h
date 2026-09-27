#ifndef TRANSPORT_RECORDER_TERP_DEVICE_H
#define TRANSPORT_RECORDER_TERP_DEVICE_H

#include <stdint.h>

#define TERP_LIST_MAXIMUM_COUNT UINT16_C(16)

#include "terp_codec.h"

#define TERP_DEVICE_MAX_EVENT_CHUNK_BYTES UINT32_C(4000)
#define TERP_DEVICE_MAX_CRASH_RECORD_CHUNK_BYTES \
    TERP_DEVICE_MAX_EVENT_CHUNK_BYTES
#define TERP_EVENT_EVIDENCE_RESPONSE_BYTES UINT32_C(20)
#define TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES UINT32_C(20)

#define TERP_CAPABILITY_DEVICE_INFO UINT32_C(1)
#define TERP_CAPABILITY_HEALTH UINT32_C(1 << 1)
#define TERP_CAPABILITY_TIME UINT32_C(1 << 2)
#define TERP_CAPABILITY_EVENT_READ UINT32_C(1 << 3)
#define TERP_CAPABILITY_LIVE_PREVIEW UINT32_C(1 << 4)
#define TERP_CAPABILITY_OTA UINT32_C(1 << 5)
#define TERP_CAPABILITY_AI_RESULTS UINT32_C(1 << 6)
#define TERP_CAPABILITY_MODEL_OTA UINT32_C(1 << 7)
#define TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1 (UINT32_C(1) << 8)

typedef enum
{
    TERP_ERROR_MALFORMED = 1,
    TERP_ERROR_UNSUPPORTED,
    TERP_ERROR_NOT_FOUND,
    TERP_ERROR_BUSY,
    TERP_ERROR_INTERNAL,
    TERP_ERROR_INCOMPATIBLE,
    TERP_ERROR_HANDSHAKE_REQUIRED,
} terp_error_code_t;

typedef struct
{
    const char *model;
    uint8_t model_length;
    const char *firmware_version;
    uint8_t firmware_version_length;
    const char *hardware_version;
    uint8_t hardware_version_length;
    const char *serial_number;
    uint8_t serial_number_length;
    uint32_t capability_flags;
} terp_device_info_t;

typedef struct
{
    uint8_t state;
    uint8_t storage_ready;
    uint32_t free_log_bytes;
    uint32_t storage_error_count;
    uint32_t event_export_error_count;
} terp_health_info_t;

typedef struct
{
    uint32_t event_id;
    uint32_t total_length;
    uint32_t event_crc32;
} terp_event_info_t;

typedef struct
{
    uint32_t total_bytes;
    uint32_t verified_bytes;
    uint8_t pending_install;
} terp_ota_progress_t;

typedef struct
{
    uint32_t total_bytes;
    uint32_t verified_bytes;
    uint8_t pending_install;
    uint8_t active_slot;
    uint8_t model_valid;
} terp_model_ota_progress_t;

typedef struct
{
    uint32_t event_id;
    uint16_t model_version;
    uint8_t status;
    uint8_t class_index;
    uint8_t class_count;
    uint8_t quality_flags;
    uint16_t event_flags;
    uint32_t sample_count;
    uint32_t model_crc32;
    float confidence;
    float logits[4];
    uint16_t failure_reason;
    uint32_t result_sequence;
} terp_ai_result_t;

typedef struct
{
    uint32_t event_id;
    uint16_t evidence_version;
    uint8_t verdict;
    uint8_t ai_decision;
    uint32_t reason_flags;
    uint8_t storage_state;
    uint8_t ai_result_status;
    uint16_t ai_failure_reason;
    uint32_t ai_result_sequence;
} terp_event_evidence_t;

typedef struct
{
    uint32_t sequence;
    uint32_t total_length;
    uint32_t actual_length;
} terp_crash_record_chunk_t;

typedef int (*terp_device_get_health_fn)(terp_health_info_t *health,
                                         void *context);
typedef int (*terp_device_get_time_fn)(int64_t *utc_unix_seconds,
                                       uint32_t *epoch_id,
                                       void *context);
typedef int (*terp_device_set_time_fn)(int64_t utc_unix_seconds,
                                       uint32_t *epoch_id,
                                       void *context);
typedef int (*terp_device_list_events_fn)(uint32_t after_event_id,
                                          uint16_t maximum_count,
                                          terp_event_info_t *events,
                                          uint16_t events_capacity,
                                          uint16_t *event_count,
                                          uint32_t *next_event_id,
                                          void *context);
typedef int (*terp_device_get_event_info_fn)(uint32_t event_id,
                                              terp_event_info_t *info,
                                              void *context);
typedef int (*terp_device_read_event_fn)(uint32_t event_id,
                                         uint32_t offset,
                                         uint8_t *data,
                                         uint32_t requested_length,
                                         uint32_t *actual_length,
                                         void *context);
typedef int (*terp_device_set_live_fn)(int enabled, void *context);
typedef int (*terp_device_get_ai_result_fn)(uint32_t event_id,
                                            terp_ai_result_t *result,
                                            void *context);
typedef int (*terp_device_get_event_evidence_fn)(
    uint32_t event_id, terp_event_evidence_t *evidence, void *context);
typedef int (*terp_device_get_crash_record_fn)(
    uint32_t sequence,
    uint32_t offset,
    uint8_t *data,
    uint32_t requested_length,
    uint32_t data_capacity,
    terp_crash_record_chunk_t *chunk,
    void *context);
typedef int (*terp_device_ack_crash_record_fn)(uint32_t sequence,
                                               void *context);
typedef int (*terp_device_ota_begin_fn)(uint32_t total_bytes,
                                        terp_ota_progress_t *progress,
                                        void *context);
typedef int (*terp_device_ota_write_fn)(uint32_t offset, const uint8_t *data,
                                        uint32_t length, uint32_t crc32,
                                        terp_ota_progress_t *progress,
                                        void *context);
typedef int (*terp_device_ota_query_fn)(terp_ota_progress_t *progress,
                                        void *context);
typedef int (*terp_device_ota_finalize_fn)(terp_ota_progress_t *progress,
                                           void *context);
typedef int (*terp_device_ota_cancel_fn)(void *context);
typedef int (*terp_device_model_ota_begin_fn)(
    uint32_t total_bytes, terp_model_ota_progress_t *progress, void *context);
typedef int (*terp_device_model_ota_write_fn)(
    uint32_t offset, const uint8_t *data, uint32_t length, uint32_t crc32,
    terp_model_ota_progress_t *progress, void *context);
typedef int (*terp_device_model_ota_query_fn)(
    terp_model_ota_progress_t *progress, void *context);
typedef int (*terp_device_model_ota_finalize_fn)(
    terp_model_ota_progress_t *progress, void *context);
typedef int (*terp_device_model_ota_cancel_fn)(void *context);

typedef struct
{
    terp_device_get_health_fn get_health;
    terp_device_get_time_fn get_time;
    terp_device_set_time_fn set_time;
    terp_device_list_events_fn list_events;
    terp_device_get_event_info_fn get_event_info;
    terp_device_read_event_fn read_event;
    terp_device_set_live_fn set_live;
    terp_device_get_ai_result_fn get_ai_result;
    terp_device_get_event_evidence_fn get_event_evidence;
    terp_device_get_crash_record_fn get_crash_record;
    terp_device_ack_crash_record_fn ack_crash_record;
    terp_device_ota_begin_fn ota_begin;
    terp_device_ota_write_fn ota_write;
    terp_device_ota_query_fn ota_query;
    terp_device_ota_finalize_fn ota_finalize;
    terp_device_ota_cancel_fn ota_cancel;
    terp_device_ota_begin_fn recovery_begin;
    terp_device_ota_write_fn recovery_write;
    terp_device_ota_query_fn recovery_query;
    terp_device_ota_finalize_fn recovery_finalize;
    terp_device_ota_cancel_fn recovery_cancel;
    terp_device_model_ota_begin_fn model_ota_begin;
    terp_device_model_ota_write_fn model_ota_write;
    terp_device_model_ota_query_fn model_ota_query;
    terp_device_model_ota_finalize_fn model_ota_finalize;
    terp_device_model_ota_cancel_fn model_ota_cancel;
} terp_device_ops_t;

typedef struct
{
    const terp_device_ops_t *ops;
    void *context;
    terp_device_info_t info;
    uint32_t maximum_event_chunk_bytes;
    uint8_t session_ready;
} terp_device_t;

void terp_device_init(terp_device_t *device,
                      const terp_device_ops_t *ops,
                      void *context);
void terp_device_set_info(terp_device_t *device,
                          const terp_device_info_t *info);
void terp_device_reset_session(terp_device_t *device);
int terp_device_handle_request(terp_device_t *device,
                               const terp_message_t *request,
                               uint8_t *work_buffer,
                               uint32_t work_capacity,
                               uint8_t *response_buffer,
                               uint32_t response_capacity,
                               uint32_t *response_length);

#endif
