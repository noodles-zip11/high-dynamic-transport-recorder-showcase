#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "terp_device.h"
#include "terp_parser.h"

typedef struct
{
    uint8_t bytes[6];
} test_storage_t;

typedef struct
{
    uint16_t maximum_count;
} list_capture_t;

typedef struct
{
    uint32_t count;
    terp_message_t message;
    uint8_t payload[TERP_MAX_PAYLOAD_BYTES];
} capture_t;

typedef struct
{
    uint32_t begin_total_bytes;
    uint32_t write_offset;
    uint32_t write_crc32;
    uint32_t write_length;
    uint32_t query_count;
    uint32_t finalize_count;
    uint32_t cancel_count;
} ota_capture_t;

typedef struct
{
    uint32_t begin_total_bytes;
    uint32_t write_offset;
    uint32_t write_length;
    uint32_t finalize_count;
    uint32_t cancel_count;
    int finalize_result;
} model_ota_capture_t;

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void capture_response(const terp_message_t *message, void *context)
{
    capture_t *capture = context;

    capture->count++;
    capture->message = *message;
    memcpy(capture->payload, message->payload, message->payload_length);
    capture->message.payload = capture->payload;
}

static int get_event_info(uint32_t event_id,
                          terp_event_info_t *info,
                          void *context)
{
    (void)context;
    if (event_id != 42U)
    {
        return -1;
    }
    info->event_id = 42U;
    info->total_length = 6U;
    info->event_crc32 = UINT32_C(0xA1B2C3D4);
    return 0;
}

static int read_event(uint32_t event_id,
                      uint32_t offset,
                      uint8_t *data,
                      uint32_t requested_length,
                      uint32_t *actual_length,
                      void *context)
{
    const test_storage_t *storage = context;
    uint32_t available;

    if (event_id != 42U || offset > sizeof(storage->bytes))
    {
        return -1;
    }
    available = (uint32_t)sizeof(storage->bytes) - offset;
    *actual_length = requested_length < available ? requested_length : available;
    memcpy(data, &storage->bytes[offset], *actual_length);
    return 0;
}

static int list_events(uint32_t after_event_id,
                       uint16_t maximum_count,
                       terp_event_info_t *events,
                       uint16_t events_capacity,
                       uint16_t *event_count,
                       uint32_t *next_event_id,
                       void *context)
{
    list_capture_t *capture = context;

    (void)after_event_id;
    (void)events;
    (void)events_capacity;
    capture->maximum_count = maximum_count;
    *event_count = 0U;
    *next_event_id = 0U;
    return 0;
}

static int ota_begin(uint32_t total_bytes, terp_ota_progress_t *progress,
                     void *context)
{
    ota_capture_t *capture = context;

    capture->begin_total_bytes = total_bytes;
    progress->total_bytes = total_bytes;
    progress->verified_bytes = 0U;
    progress->pending_install = 0U;
    return 0;
}

static int ota_write(uint32_t offset, const uint8_t *data, uint32_t length,
                     uint32_t crc32, terp_ota_progress_t *progress,
                     void *context)
{
    ota_capture_t *capture = context;

    assert(length == 3U);
    assert(memcmp(data, "OTA", length) == 0);
    capture->write_offset = offset;
    capture->write_crc32 = crc32;
    capture->write_length = length;
    progress->total_bytes = 12U;
    progress->verified_bytes = offset + length;
    progress->pending_install = 0U;
    return 0;
}

static int ota_query(terp_ota_progress_t *progress, void *context)
{
    ota_capture_t *capture = context;

    capture->query_count++;
    progress->total_bytes = 12U;
    progress->verified_bytes = 3U;
    progress->pending_install = 0U;
    return 0;
}

static int ota_finalize(terp_ota_progress_t *progress, void *context)
{
    ota_capture_t *capture = context;

    capture->finalize_count++;
    progress->total_bytes = 12U;
    progress->verified_bytes = 12U;
    progress->pending_install = 1U;
    return 0;
}

static int ota_cancel(void *context)
{
    ota_capture_t *capture = context;

    capture->cancel_count++;
    return 0;
}

static int model_ota_begin(uint32_t total_bytes,
                           terp_model_ota_progress_t *progress,
                           void *context)
{
    model_ota_capture_t *capture = context;

    capture->begin_total_bytes = total_bytes;
    progress->total_bytes = total_bytes;
    progress->verified_bytes = 0U;
    progress->pending_install = 0U;
    progress->active_slot = 0U;
    progress->model_valid = 1U;
    return 0;
}

static int model_ota_write(uint32_t offset, const uint8_t *data, uint32_t length,
                           uint32_t crc32,
                           terp_model_ota_progress_t *progress,
                           void *context)
{
    model_ota_capture_t *capture = context;

    assert(length == 3U);
    assert(memcmp(data, "MOD", length) == 0);
    (void)crc32;
    capture->write_offset = offset;
    capture->write_length = length;
    progress->total_bytes = 12U;
    progress->verified_bytes = offset + length;
    progress->pending_install = 0U;
    progress->active_slot = 0U;
    progress->model_valid = 1U;
    return 0;
}

static int model_ota_query(terp_model_ota_progress_t *progress, void *context)
{
    (void)context;
    progress->total_bytes = 12U;
    progress->verified_bytes = 3U;
    progress->pending_install = 0U;
    progress->active_slot = 0U;
    progress->model_valid = 1U;
    return 0;
}

static int model_ota_finalize(terp_model_ota_progress_t *progress, void *context)
{
    model_ota_capture_t *capture = context;

    capture->finalize_count++;
    if (capture->finalize_result != 0)
    {
        return capture->finalize_result;
    }
    progress->total_bytes = 12U;
    progress->verified_bytes = 12U;
    progress->pending_install = 0U;
    progress->active_slot = 1U;
    progress->model_valid = 1U;
    return 0;
}

static int model_ota_cancel(void *context)
{
    model_ota_capture_t *capture = context;

    capture->cancel_count++;
    return 0;
}

static int get_ai_result(uint32_t event_id,
                         terp_ai_result_t *result,
                         void *context)
{
    (void)context;
    if (event_id != 42U || result == NULL)
    {
        return -TERP_ERROR_NOT_FOUND;
    }
    memset(result, 0, sizeof(*result));
    result->event_id = event_id;
    result->model_version = 2U;
    result->status = 1U;
    result->class_index = 1U;
    result->class_count = 2U;
    result->quality_flags = 3U;
    result->event_flags = 8U;
    result->sample_count = 2400U;
    result->model_crc32 = UINT32_C(0xAABBCCDD);
    result->confidence = 0.875F;
    result->logits[0] = -1.0F;
    result->logits[1] = 1.0F;
    result->failure_reason = 0U;
    result->result_sequence = 9U;
    return 0;
}

static void parse_response(const uint8_t *encoded,
                           uint32_t encoded_length,
                           capture_t *capture)
{
    terp_parser_t parser;

    terp_parser_init(&parser);
    terp_parser_feed(&parser, encoded, encoded_length, capture_response, capture);
    assert(capture->count == 1U);
}

static void complete_hello(terp_device_t *device,
                           uint8_t *work,
                           uint8_t *response,
                           uint32_t *response_length)
{
    const uint8_t payload[] = {1U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 1U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };

    assert(terp_device_handle_request(device, &request, work,
                                      TERP_MAX_PAYLOAD_BYTES, response,
                                      TERP_MAX_FRAME_BYTES, response_length) == 0);
}

static void test_event_chunk_is_repeatable(void)
{
    const test_storage_t storage = {.bytes = {10U, 11U, 12U, 13U, 14U, 15U}};
    const terp_device_ops_t ops = {
        .get_event_info = get_event_info,
        .read_event = read_event,
    };
    terp_device_t device;
    const uint8_t request_payload[] = {
        42U, 0U, 0U, 0U,
        2U, 0U, 0U, 0U,
        4U, 0U, 0U, 0U,
    };
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_READ_EVENT_CHUNK,
        .flags = 0U,
        .sequence = 7U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint8_t repeated_response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    uint32_t repeated_response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, (void *)&storage);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      repeated_response, sizeof(repeated_response),
                                      &repeated_response_length) == 0);
    assert(response_length == repeated_response_length);
    assert(memcmp(response, repeated_response, response_length) == 0);

    parse_response(response, response_length, &capture);
    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_READ_EVENT_CHUNK));
    assert(get_u32_le(capture.payload) == 42U);
    assert(get_u32_le(&capture.payload[4]) == 2U);
    assert(get_u32_le(&capture.payload[8]) == 6U);
    assert(get_u32_le(&capture.payload[12]) == 4U);
    assert(memcmp(&capture.payload[20], &storage.bytes[2], 4U) == 0);
}

static void test_delete_returns_a_structured_unsupported_error(void)
{
    terp_device_t device;
    const uint8_t request_payload[] = {42U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_DELETE_EVENT,
        .flags = 0U,
        .sequence = 9U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, 0, 0);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == TERP_ERROR_UNSUPPORTED);
    assert(capture.payload[2] == (uint8_t)TERP_MESSAGE_DELETE_EVENT);
}

static void test_oversized_event_chunk_is_malformed(void)
{
    const test_storage_t storage = {.bytes = {10U, 11U, 12U, 13U, 14U, 15U}};
    const terp_device_ops_t ops = {
        .get_event_info = get_event_info,
        .read_event = read_event,
    };
    const uint8_t request_payload[] = {
        42U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        0xA1U, 0x0FU, 0U, 0U,
    };
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_READ_EVENT_CHUNK,
        .flags = 0U,
        .sequence = 10U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    terp_device_t device;
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, (void *)&storage);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == TERP_ERROR_MALFORMED);
}

static void test_list_event_count_uses_full_u32_request_field(void)
{
    const terp_device_ops_t ops = {.list_events = list_events};
    const uint8_t request_payload[] = {
        0U, 0U, 0U, 0U,
        1U, 0U, 1U, 0U,
    };
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_LIST_EVENTS,
        .flags = 0U,
        .sequence = 11U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    terp_device_t device;
    list_capture_t list_capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    terp_device_init(&device, &ops, &list_capture);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    assert(list_capture.maximum_count == 16U);
}

static void test_event_read_requires_hello_before_the_session_is_ready(void)
{
    const test_storage_t storage = {.bytes = {10U, 11U, 12U, 13U, 14U, 15U}};
    const terp_device_ops_t ops = {
        .get_event_info = get_event_info,
        .read_event = read_event,
    };
    const uint8_t chunk_payload[] = {
        42U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        4U, 0U, 0U, 0U,
    };
    const uint8_t hello_payload[] = {1U};
    const terp_message_t chunk_request = {
        .message_type = TERP_MESSAGE_READ_EVENT_CHUNK,
        .flags = 0U,
        .sequence = 12U,
        .payload = chunk_payload,
        .payload_length = sizeof(chunk_payload),
    };
    const terp_message_t hello_request = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 13U,
        .payload = hello_payload,
        .payload_length = sizeof(hello_payload),
    };
    terp_device_t device;
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, (void *)&storage);
    assert(terp_device_handle_request(&device, &chunk_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == 7U);
    assert(capture.payload[2] == (uint8_t)TERP_MESSAGE_READ_EVENT_CHUNK);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &hello_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == terp_response_type(TERP_MESSAGE_HELLO));

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &chunk_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_READ_EVENT_CHUNK));

    terp_device_reset_session(&device);
    capture.count = 0U;
    assert(terp_device_handle_request(&device, &chunk_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == TERP_ERROR_HANDSHAKE_REQUIRED);
}

static void test_ota_requests_dispatch_and_return_progress(void)
{
    const terp_device_ops_t ops = {
        .ota_begin = ota_begin,
        .ota_write = ota_write,
        .ota_query = ota_query,
        .ota_finalize = ota_finalize,
        .ota_cancel = ota_cancel,
    };
    const uint8_t begin_payload[] = {12U, 0U, 0U, 0U};
    const uint8_t write_payload[] = {
        3U, 0U, 0U, 0U,
        0x78U, 0x56U, 0x34U, 0x12U,
        'O', 'T', 'A',
    };
    const terp_message_t begin_request = {
        .message_type = TERP_MESSAGE_OTA_BEGIN,
        .flags = 0U,
        .sequence = 20U,
        .payload = begin_payload,
        .payload_length = sizeof(begin_payload),
    };
    const terp_message_t write_request = {
        .message_type = TERP_MESSAGE_OTA_WRITE_CHUNK,
        .flags = 0U,
        .sequence = 21U,
        .payload = write_payload,
        .payload_length = sizeof(write_payload),
    };
    const terp_message_t query_request = {
        .message_type = TERP_MESSAGE_OTA_QUERY,
        .flags = 0U,
        .sequence = 22U,
        .payload = 0,
        .payload_length = 0U,
    };
    const terp_message_t finalize_request = {
        .message_type = TERP_MESSAGE_OTA_FINALIZE,
        .flags = 0U,
        .sequence = 23U,
        .payload = 0,
        .payload_length = 0U,
    };
    const terp_message_t cancel_request = {
        .message_type = TERP_MESSAGE_OTA_CANCEL,
        .flags = 0U,
        .sequence = 24U,
        .payload = 0,
        .payload_length = 0U,
    };
    terp_device_t device;
    ota_capture_t ota = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, &ota);
    complete_hello(&device, work, response, &response_length);

    assert(terp_device_handle_request(&device, &begin_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(ota.begin_total_bytes == 12U);
    assert(capture.message.message_type == terp_response_type(TERP_MESSAGE_OTA_BEGIN));
    assert(get_u32_le(capture.payload) == 12U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &write_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(ota.write_offset == 3U);
    assert(ota.write_crc32 == UINT32_C(0x12345678));
    assert(ota.write_length == 3U);
    assert(get_u32_le(&capture.payload[4]) == 6U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &query_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(ota.query_count == 1U);
    assert(get_u32_le(&capture.payload[4]) == 3U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &finalize_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(ota.finalize_count == 1U);
    assert(capture.payload[8] == 1U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &cancel_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(ota.cancel_count == 1U);
    assert(capture.message.payload_length == 0U);
}

static void test_ota_request_is_unsupported_when_no_ota_service_is_bound(void)
{
    const terp_device_ops_t ops = {0};
    const uint8_t payload[] = {1U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_OTA_BEGIN,
        .flags = 0U,
        .sequence = 25U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    terp_device_t device;
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, 0);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == TERP_ERROR_UNSUPPORTED);
}

static void test_model_ota_requests_dispatch_and_return_extended_progress(void)
{
    const terp_device_ops_t ops = {
        .model_ota_begin = model_ota_begin,
        .model_ota_write = model_ota_write,
        .model_ota_query = model_ota_query,
        .model_ota_finalize = model_ota_finalize,
        .model_ota_cancel = model_ota_cancel,
    };
    const uint8_t begin_payload[] = {12U, 0U, 0U, 0U};
    const uint8_t write_payload[] = {
        3U, 0U, 0U, 0U,
        0x78U, 0x56U, 0x34U, 0x12U,
        'M', 'O', 'D',
    };
    const terp_message_t begin_request = {
        .message_type = TERP_MESSAGE_MODEL_OTA_BEGIN,
        .flags = 0U,
        .sequence = 28U,
        .payload = begin_payload,
        .payload_length = sizeof(begin_payload),
    };
    const terp_message_t write_request = {
        .message_type = TERP_MESSAGE_MODEL_OTA_WRITE_CHUNK,
        .flags = 0U,
        .sequence = 29U,
        .payload = write_payload,
        .payload_length = sizeof(write_payload),
    };
    const terp_message_t finalize_request = {
        .message_type = TERP_MESSAGE_MODEL_OTA_FINALIZE,
        .flags = 0U,
        .sequence = 30U,
        .payload = 0,
        .payload_length = 0U,
    };
    terp_device_t device;
    model_ota_capture_t model_ota = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, &model_ota);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &begin_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(model_ota.begin_total_bytes == 12U);
    assert(capture.message.payload_length == 11U);
    assert(get_u32_le(&capture.payload[4]) == 0U);
    assert(capture.payload[9] == 0U && capture.payload[10] == 1U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &write_request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(model_ota.write_offset == 3U && model_ota.write_length == 3U);

    capture.count = 0U;
    assert(terp_device_handle_request(&device, &finalize_request, work,
                                      sizeof(work), response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(model_ota.finalize_count == 1U);
    assert(capture.payload[8] == 0U && capture.payload[9] == 1U);

    model_ota.finalize_result = -TERP_ERROR_INCOMPATIBLE;
    capture.count = 0U;
    assert(terp_device_handle_request(&device, &finalize_request, work,
                                      sizeof(work), response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == TERP_MESSAGE_ERROR);
    assert(capture.payload[0] == TERP_ERROR_INCOMPATIBLE);
}

static void test_recovery_begin_dispatches_without_requesting_install(void)
{
    const terp_device_ops_t ops = {
        .recovery_begin = ota_begin,
    };
    const uint8_t payload[] = {12U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = UINT16_C(0x0106),
        .flags = 0U,
        .sequence = 26U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    terp_device_t device;
    ota_capture_t ota = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, &ota);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type == terp_response_type(UINT16_C(0x0106)));
    assert(ota.begin_total_bytes == 12U);
    assert(capture.payload[8] == 0U);
}

static void test_ai_result_request_returns_traceable_prediction(void)
{
    const terp_device_ops_t ops = {.get_ai_result = get_ai_result};
    const uint8_t payload[] = {42U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_GET_AI_RESULT,
        .flags = 0U,
        .sequence = 27U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    terp_device_t device;
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;
    capture_t capture = {0};

    terp_device_init(&device, &ops, NULL);
    complete_hello(&device, work, response, &response_length);
    assert(terp_device_handle_request(&device, &request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_GET_AI_RESULT));
    assert(capture.message.payload_length == 48U);
    assert(get_u32_le(&capture.payload[0]) == 42U);
    assert(capture.payload[4] == 2U && capture.payload[5] == 0U);
    assert(capture.payload[6] == 1U && capture.payload[7] == 1U);
    assert(capture.payload[8] == 2U && capture.payload[9] == 3U);
    assert(capture.payload[10] == 8U && capture.payload[11] == 0U);
    assert(get_u32_le(&capture.payload[12]) == 2400U);
    assert(get_u32_le(&capture.payload[16]) == UINT32_C(0xAABBCCDD));
    assert(get_u32_le(&capture.payload[44]) == 9U);
}

int main(void)
{
    test_event_chunk_is_repeatable();
    test_delete_returns_a_structured_unsupported_error();
    test_oversized_event_chunk_is_malformed();
    test_list_event_count_uses_full_u32_request_field();
    test_event_read_requires_hello_before_the_session_is_ready();
    test_ota_requests_dispatch_and_return_progress();
    test_ota_request_is_unsupported_when_no_ota_service_is_bound();
    test_model_ota_requests_dispatch_and_return_extended_progress();
    test_recovery_begin_dispatches_without_requesting_install();
    test_ai_result_request_returns_traceable_prediction();
    puts("terp device: PASS");
    return 0;
}
