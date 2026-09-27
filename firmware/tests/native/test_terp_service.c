#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ota_qspi_candidate.h"
#include "ota_state_app_store.h"
#include "storage_service.h"
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "crash_record_target.h"
#endif
#include "terp_service.h"
#include "terp_uart3.h"

static uint8_t transmitted[TERP_MAX_FRAME_BYTES];
static uint32_t transmitted_length;
static event_log_state_t storage_state = EVENT_LOG_READY;
static int storage_info_failure;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static int crash_backend_ready;
#endif

static void test_terp_buffers_are_not_allocated_on_the_rx_stack(void)
{
    terp_service_t service;

    assert(TERP_UART3_THREAD_STACK_BYTES >= 4096U);
    assert(sizeof(service.list_storage_events)
           == sizeof(event_log_event_info_t) * TERP_LIST_MAXIMUM_COUNT);
}

static void test_terp_rx_requires_a_successful_maintenance_lease(void)
{
    assert(!terp_uart3_rx_maintenance_acquired(-RT_EINVAL, RT_FALSE));
    assert(!terp_uart3_rx_maintenance_acquired(-RT_EBUSY, RT_FALSE));
    assert(!terp_uart3_rx_maintenance_acquired(-RT_ERROR, RT_FALSE));
    assert(!terp_uart3_rx_maintenance_acquired(RT_EOK, RT_FALSE));
    assert(terp_uart3_rx_maintenance_acquired(RT_EOK, RT_TRUE));
}

bool ota_qspi_candidate_init(void)
{
    return false;
}

int ota_qspi_candidate_erase(void *context, uint32_t offset, uint32_t length)
{
    (void)context;
    (void)offset;
    (void)length;
    return -1;
}

int ota_qspi_candidate_write(void *context, uint32_t offset,
                             const uint8_t *data, uint32_t length)
{
    (void)context;
    (void)offset;
    (void)data;
    (void)length;
    return -1;
}

int ota_qspi_candidate_read(void *context, uint32_t offset,
                            uint8_t *data, uint32_t length)
{
    (void)context;
    (void)offset;
    (void)data;
    (void)length;
    return -1;
}

int ota_qspi_recovery_erase(void *context, uint32_t offset, uint32_t length)
{
    return ota_qspi_candidate_erase(context, offset, length);
}

int ota_qspi_recovery_write(void *context, uint32_t offset,
                            const uint8_t *data, uint32_t length)
{
    return ota_qspi_candidate_write(context, offset, data, length);
}

int ota_qspi_recovery_read(void *context, uint32_t offset,
                           uint8_t *data, uint32_t length)
{
    return ota_qspi_candidate_read(context, offset, data, length);
}

int ota_qspi_model_a_erase(void *context, uint32_t offset, uint32_t length)
{
    return ota_qspi_candidate_erase(context, offset, length);
}

int ota_qspi_model_a_write(void *context, uint32_t offset,
                           const uint8_t *data, uint32_t length)
{
    return ota_qspi_candidate_write(context, offset, data, length);
}

int ota_qspi_model_a_read(void *context, uint32_t offset, uint8_t *data,
                          uint32_t length)
{
    return ota_qspi_candidate_read(context, offset, data, length);
}

int ota_qspi_model_b_erase(void *context, uint32_t offset, uint32_t length)
{
    return ota_qspi_candidate_erase(context, offset, length);
}

int ota_qspi_model_b_write(void *context, uint32_t offset,
                           const uint8_t *data, uint32_t length)
{
    return ota_qspi_candidate_write(context, offset, data, length);
}

int ota_qspi_model_b_read(void *context, uint32_t offset, uint8_t *data,
                          uint32_t length)
{
    return ota_qspi_candidate_read(context, offset, data, length);
}

int ota_qspi_model_state_erase(void *context, uint32_t offset, uint32_t length)
{
    return ota_qspi_candidate_erase(context, offset, length);
}

int ota_qspi_model_state_write(void *context, uint32_t offset,
                               const uint8_t *data, uint32_t length)
{
    return ota_qspi_candidate_write(context, offset, data, length);
}

int ota_qspi_model_state_read(void *context, uint32_t offset, uint8_t *data,
                              uint32_t length)
{
    return ota_qspi_candidate_read(context, offset, data, length);
}

int ota_qspi_model_state_bank_erase(void *context, uint8_t bank,
                                    uint32_t offset, uint32_t length)
{
    (void)bank;
    return ota_qspi_model_state_erase(context, offset, length);
}

int ota_qspi_model_state_bank_write(void *context, uint8_t bank,
                                    uint32_t offset, const uint8_t *data,
                                    uint32_t length)
{
    (void)bank;
    return ota_qspi_model_state_write(context, offset, data, length);
}

int ota_qspi_model_state_bank_read(void *context, uint8_t bank,
                                   uint32_t offset, uint8_t *data,
                                   uint32_t length)
{
    (void)bank;
    return ota_qspi_model_state_read(context, offset, data, length);
}

int ota_state_app_store_mark_pending_install(void *context)
{
    (void)context;
    return -1;
}

rt_err_t storage_service_get_status(event_log_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = storage_state;
    status->next_event_id = 2U;
    return RT_EOK;
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
rt_bool_t storage_service_is_ready(void)
{
    return storage_state == EVENT_LOG_READY ? RT_TRUE : RT_FALSE;
}

rt_err_t storage_service_verify_event(uint32_t event_id)
{
    (void)event_id;
    return RT_EOK;
}

int crash_record_target_is_ready(void)
{
    return crash_backend_ready;
}

int crash_record_target_get_visible_record(crash_record_v1_t *record)
{
    (void)record;
    return CRASH_RECORD_TARGET_UNAVAILABLE;
}

crash_record_status_t crash_record_target_ack(uint32_t sequence)
{
    (void)sequence;
    return CRASH_RECORD_STATUS_NO_VALID;
}
#endif

rt_err_t storage_service_get_event_info(uint32_t event_id,
                                        event_log_event_info_t *info)
{
    if (event_id != 1U || storage_info_failure)
    {
        return -RT_ERROR;
    }
    info->event_id = 1U;
    info->ev01_length = 4U;
    info->ev01_crc32 = UINT32_C(0x11223344);
    return RT_EOK;
}

rt_err_t storage_service_list_events(uint32_t after_event_id,
                                     uint16_t maximum_count,
                                     event_log_event_info_t *events,
                                     uint16_t events_capacity,
                                     uint16_t *event_count,
                                     uint32_t *next_event_id)
{
    if (after_event_id != 0U || maximum_count == 0U || events_capacity == 0U)
    {
        return -RT_ERROR;
    }
    memset(&events[0], 0, sizeof(events[0]));
    events[0].event_id = 1U;
    events[0].ev01_length = 4U;
    events[0].ev01_crc32 = UINT32_C(0x11223344);
    *event_count = 1U;
    *next_event_id = 0U;
    return RT_EOK;
}

rt_err_t storage_service_read_event(uint32_t event_id,
                                    uint32_t offset,
                                    uint8_t *data,
                                    uint32_t length,
                                    uint32_t *read_length)
{
    static const uint8_t event[] = {9U, 8U, 7U, 6U};

    if (event_id != 1U || offset > sizeof(event))
    {
        return -RT_ERROR;
    }
    *read_length = length < sizeof(event) - offset ? length
                                                     : sizeof(event) - offset;
    memcpy(data, &event[offset], *read_length);
    return RT_EOK;
}

void health_service_get_snapshot(const health_service_t *service,
                                 health_snapshot_t *snapshot_out)
{
    (void)service;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->state = HEALTH_HEALTHY;
}

int time_service_get_status(time_service_t *service, time_service_status_t *status)
{
    (void)service;
    memset(status, 0, sizeof(*status));
    status->utc_valid = true;
    status->utc_unix_seconds = 1700000000;
    status->epoch_id = 1U;
    return 0;
}

int time_service_set_utc(time_service_t *service, int64_t utc_unix_seconds)
{
    (void)service;
    (void)utc_unix_seconds;
    return 0;
}

static int capture_write(const uint8_t *data, uint32_t length, void *context)
{
    (void)context;
    memcpy(transmitted, data, length);
    transmitted_length = length;
    return 0;
}

static int reject_write(const uint8_t *data, uint32_t length, void *context)
{
    (void)data;
    (void)length;
    (void)context;
    return -1;
}

static void capture_response(const terp_message_t *message, void *context)
{
    *(terp_message_t *)context = *message;
}

static void test_terp_service_reads_through_storage_service(void)
{
    const uint8_t hello_payload[] = {1U};
    const terp_message_t hello_request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 2U,
        .payload = hello_payload,
        .payload_length = sizeof(hello_payload),
    };
    const uint8_t request_payload[] = {
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        4U, 0U, 0U, 0U,
    };
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_READ_EVENT_CHUNK,
        .flags = 0U,
        .sequence = 3U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_service_t service;
    terp_parser_t parser;
    terp_message_t response = {0};
    terp_service_stats_t stats = {0};

    terp_service_init(&service, NULL, NULL, NULL, capture_write, NULL);
    assert(terp_encode_frame(&hello_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    assert(transmitted_length != 0U);

    transmitted_length = 0U;
    assert(terp_encode_frame(&request, encoded, sizeof(encoded), &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    assert(transmitted_length != 0U);

    terp_parser_init(&parser);
    terp_parser_feed(&parser, transmitted, transmitted_length, capture_response,
                     &response);
    assert(response.message_type == terp_response_type(TERP_MESSAGE_READ_EVENT_CHUNK));
    assert(response.payload_length == 23U);
    assert(response.payload[20] == 8U);
    terp_service_get_stats(&service, &stats);
    assert(stats.tx_frame_count == 2U);
    assert(stats.tx_error_count == 0U);
}

static void test_terp_service_reports_storage_not_ready_when_log_is_in_error(void)
{
    const uint8_t hello_payload[] = {1U};
    const terp_message_t hello_request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 10U,
        .payload = hello_payload,
        .payload_length = sizeof(hello_payload),
    };
    const terp_message_t health_request = {
        .message_type = TERP_MESSAGE_GET_HEALTH,
        .flags = 0U,
        .sequence = 11U,
        .payload = NULL,
        .payload_length = 0U,
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_service_t service;
    terp_parser_t parser;
    terp_message_t response = {0};

    storage_state = EVENT_LOG_ERROR;
    terp_service_init(&service, NULL, (const health_service_t *)1,
                      NULL, capture_write, NULL);
    assert(terp_encode_frame(&hello_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    assert(terp_encode_frame(&health_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);

    terp_parser_init(&parser);
    terp_parser_feed(&parser, transmitted, transmitted_length, capture_response,
                     &response);
    assert(response.message_type == terp_response_type(TERP_MESSAGE_GET_HEALTH));
    assert(response.payload_length == 14U);
    assert(response.payload[1] == 0U);
    storage_state = EVENT_LOG_READY;
}

static void test_terp_service_counts_response_write_failures(void)
{
    const uint8_t hello_payload[] = {1U};
    const terp_message_t hello_request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 20U,
        .payload = hello_payload,
        .payload_length = sizeof(hello_payload),
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_service_t service;
    terp_service_stats_t stats = {0};

    terp_service_init(&service, NULL, NULL, NULL, reject_write, NULL);
    assert(terp_encode_frame(&hello_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    terp_service_get_stats(&service, &stats);
    assert(stats.tx_frame_count == 0U);
    assert(stats.tx_error_count == 1U);
}

static void test_terp_service_rejects_time_when_no_time_service_is_bound(void)
{
    const uint8_t hello_payload[] = {1U};
    const terp_message_t hello_request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 30U,
        .payload = hello_payload,
        .payload_length = sizeof(hello_payload),
    };
    const terp_message_t time_request = {
        .message_type = TERP_MESSAGE_GET_TIME,
        .flags = 0U,
        .sequence = 31U,
        .payload = NULL,
        .payload_length = 0U,
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_service_t service;
    terp_parser_t parser;
    terp_message_t response = {0};

    terp_service_init(&service, NULL, NULL, NULL, capture_write, NULL);
    assert(terp_encode_frame(&hello_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    assert(terp_encode_frame(&time_request, encoded, sizeof(encoded),
                             &encoded_length) == 0);
    terp_service_receive(&service, encoded, encoded_length);
    terp_parser_init(&parser);
    terp_parser_feed(&parser, transmitted, transmitted_length, capture_response,
                     &response);
    assert(response.message_type == TERP_MESSAGE_ERROR);
    assert(response.payload[0] == TERP_ERROR_UNSUPPORTED);
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static void test_reliability_bindings_follow_crash_readiness(void)
{
    const terp_device_info_t device_info = {
        .capability_flags = TERP_CAPABILITY_DEVICE_INFO
                              | TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1,
    };
    terp_service_t service;

    crash_backend_ready = 0;
    terp_service_init(&service, &device_info, NULL, NULL, capture_write, NULL);
    assert((service.device.info.capability_flags
            & TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1) == 0U);
    assert(service.device_ops.get_event_evidence == NULL);
    assert(service.device_ops.get_crash_record == NULL);
    assert(service.device_ops.ack_crash_record == NULL);

    crash_backend_ready = 1;
    terp_service_init(&service, &device_info, NULL, NULL, capture_write, NULL);
    assert((service.device.info.capability_flags
            & TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1) != 0U);
    assert(service.device_ops.get_event_evidence != NULL);
    assert(service.device_ops.get_crash_record != NULL);
    assert(service.device_ops.ack_crash_record != NULL);
    crash_backend_ready = 0;
}

static void test_reliability_distinguishes_absent_event_from_storage_failure(void)
{
    const terp_device_info_t device_info = {
        .capability_flags = TERP_CAPABILITY_DEVICE_INFO,
    };
    terp_service_t service;
    terp_event_evidence_t evidence;

    crash_backend_ready = 1;
    storage_info_failure = 1;
    terp_service_init(&service, &device_info, NULL, NULL, capture_write, NULL);
    assert(service.device_ops.get_event_evidence(
               1U, &evidence, &service) == -TERP_ERROR_INTERNAL);
    assert(service.device_ops.get_event_evidence(
               2U, &evidence, &service) == -TERP_ERROR_NOT_FOUND);
    storage_info_failure = 0;
    crash_backend_ready = 0;
}
#endif

int main(void)
{
    test_terp_buffers_are_not_allocated_on_the_rx_stack();
    test_terp_rx_requires_a_successful_maintenance_lease();
    test_terp_service_reads_through_storage_service();
    test_terp_service_reports_storage_not_ready_when_log_is_in_error();
    test_terp_service_counts_response_write_failures();
    test_terp_service_rejects_time_when_no_time_service_is_bound();
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    test_reliability_bindings_follow_crash_readiness();
    test_reliability_distinguishes_absent_event_from_storage_failure();
#endif
    puts("terp service: PASS");
    return 0;
}
