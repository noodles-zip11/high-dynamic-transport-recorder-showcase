#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "terp_device.h"
#include "terp_parser.h"

typedef struct
{
    int evidence_result;
    int crash_result;
    int ack_result;
    uint32_t evidence_calls;
    uint32_t crash_calls;
    uint32_t ack_calls;
    uint32_t last_crash_sequence;
    uint32_t last_crash_offset;
    uint32_t last_crash_length;
    terp_event_evidence_t evidence;
    uint8_t crash_data[128];
    terp_crash_record_chunk_t crash_chunk;
} fake_reliability_t;

typedef struct
{
    uint32_t count;
    terp_message_t message;
    uint8_t payload[TERP_MAX_PAYLOAD_BYTES];
} response_capture_t;

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static void capture_response(const terp_message_t *message, void *context)
{
    response_capture_t *capture = context;

    capture->count++;
    capture->message = *message;
    memcpy(capture->payload, message->payload, message->payload_length);
    capture->message.payload = capture->payload;
}

static int get_event_evidence(uint32_t event_id,
                              terp_event_evidence_t *evidence,
                              void *context)
{
    fake_reliability_t *fake = context;

    fake->evidence_calls++;
    if (fake->evidence_result != 0)
    {
        return fake->evidence_result;
    }
    *evidence = fake->evidence;
    evidence->event_id = event_id;
    return 0;
}

static int get_crash_record(uint32_t sequence,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t requested_length,
                            uint32_t data_capacity,
                            terp_crash_record_chunk_t *chunk,
                            void *context)
{
    fake_reliability_t *fake = context;

    fake->crash_calls++;
    fake->last_crash_sequence = sequence;
    fake->last_crash_offset = offset;
    fake->last_crash_length = requested_length;
    if (fake->crash_result != 0)
    {
        return fake->crash_result;
    }
    assert(fake->crash_chunk.actual_length <= data_capacity);
    memcpy(data, fake->crash_data, fake->crash_chunk.actual_length);
    *chunk = fake->crash_chunk;
    return 0;
}

static int ack_crash_record(uint32_t sequence, void *context)
{
    fake_reliability_t *fake = context;

    fake->ack_calls++;
    fake->last_crash_sequence = sequence;
    return fake->ack_result;
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
                                      TERP_MAX_FRAME_BYTES,
                                      response_length) == 0);
}

static void parse_response(const uint8_t *encoded,
                           uint32_t encoded_length,
                           response_capture_t *capture)
{
    terp_parser_t parser;

    terp_parser_init(&parser);
    terp_parser_feed(&parser, encoded, encoded_length, capture_response, capture);
    assert(capture->count == 1U);
}

static void assert_error(const response_capture_t *capture,
                         terp_error_code_t error,
                         uint16_t request_type)
{
    assert(capture->message.message_type == TERP_MESSAGE_ERROR);
    assert(capture->message.payload_length == 4U);
    assert(get_u16_le(&capture->payload[0]) == (uint16_t)error);
    assert(get_u16_le(&capture->payload[2]) == request_type);
}

static int handle(terp_device_t *device,
                  const terp_message_t *request,
                  response_capture_t *capture)
{
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    assert(terp_device_handle_request(device, request, work, sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, capture);
    return 0;
}

static void test_event_evidence_status_mapping(void)
{
    fake_reliability_t fake = {
        .evidence = {
            .event_id = 0U,
            .evidence_version = 1U,
            .verdict = 3U,
            .ai_decision = 0U,
            .reason_flags = UINT32_C(0x10203040),
            .storage_state = 1U,
            .ai_result_status = 5U,
            .ai_failure_reason = 6U,
            .ai_result_sequence = 7U,
        },
    };
    const terp_device_ops_t ops = {
        .get_event_evidence = get_event_evidence,
    };
    const uint8_t request_payload[] = {42U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_GET_EVENT_EVIDENCE,
        .flags = 0U,
        .sequence = 2U,
        .payload = request_payload,
        .payload_length = sizeof(request_payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    terp_device_init(&device, &ops, &fake);
    complete_hello(&device, work, response, &response_length);
    handle(&device, &request, &capture);

    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_GET_EVENT_EVIDENCE));
    assert(capture.message.payload_length == 20U);
    assert(get_u32_le(&capture.payload[0]) == 42U);
    assert(get_u16_le(&capture.payload[4]) == 1U);
    assert(capture.payload[6] == 3U && capture.payload[7] == 0U);
    assert(get_u32_le(&capture.payload[8]) == UINT32_C(0x10203040));
    assert(capture.payload[12] == 1U && capture.payload[13] == 5U);
    assert(get_u16_le(&capture.payload[14]) == 6U);
    assert(get_u32_le(&capture.payload[16]) == 7U);
    assert(fake.evidence_calls == 1U);
}

static void test_event_evidence_errors_and_handshake(void)
{
    fake_reliability_t fake = {0};
    const terp_device_ops_t ops = {.get_event_evidence = get_event_evidence};
    const uint8_t valid_payload[] = {42U, 0U, 0U, 0U};
    const uint8_t short_payload[] = {42U, 0U, 0U};
    const terp_message_t valid_request = {
        .message_type = TERP_MESSAGE_GET_EVENT_EVIDENCE,
        .flags = 0U,
        .sequence = 3U,
        .payload = valid_payload,
        .payload_length = sizeof(valid_payload),
    };
    const terp_message_t short_request = {
        .message_type = TERP_MESSAGE_GET_EVENT_EVIDENCE,
        .flags = 0U,
        .sequence = 4U,
        .payload = short_payload,
        .payload_length = sizeof(short_payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    terp_device_init(&device, &ops, &fake);
    assert(terp_device_handle_request(&device, &valid_request, work,
                                      sizeof(work), response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert_error(&capture, TERP_ERROR_HANDSHAKE_REQUIRED,
                 TERP_MESSAGE_GET_EVENT_EVIDENCE);
    assert(fake.evidence_calls == 0U);

    complete_hello(&device, work, response, &response_length);
    capture.count = 0U;
    handle(&device, &short_request, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_GET_EVENT_EVIDENCE);
    assert(fake.evidence_calls == 0U);

    device.ops = 0;
    capture.count = 0U;
    handle(&device, &valid_request, &capture);
    assert_error(&capture, TERP_ERROR_UNSUPPORTED,
                 TERP_MESSAGE_GET_EVENT_EVIDENCE);
}

static void test_event_evidence_callback_errors_are_whitelisted(void)
{
    fake_reliability_t fake = {.evidence_result = -TERP_ERROR_NOT_FOUND};
    const terp_device_ops_t ops = {.get_event_evidence = get_event_evidence};
    const uint8_t payload[] = {42U, 0U, 0U, 0U};
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_GET_EVENT_EVIDENCE,
        .flags = 0U,
        .sequence = 5U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    terp_device_init(&device, &ops, &fake);
    complete_hello(&device, work, response, &response_length);
    handle(&device, &request, &capture);
    assert_error(&capture, TERP_ERROR_NOT_FOUND,
                 TERP_MESSAGE_GET_EVENT_EVIDENCE);

    fake.evidence_result = -TERP_ERROR_BUSY;
    capture.count = 0U;
    handle(&device, &request, &capture);
    assert_error(&capture, TERP_ERROR_INTERNAL,
                 TERP_MESSAGE_GET_EVENT_EVIDENCE);
}

static void test_crash_chunk_bounds_identity_and_ack(void)
{
    fake_reliability_t fake = {0};
    const terp_device_ops_t ops = {
        .get_crash_record = get_crash_record,
        .ack_crash_record = ack_crash_record,
    };
    const uint8_t latest_payload[] = {
        0U, 0U, 0U, 0U,
        2U, 0U, 0U, 0U,
        4U, 0U, 0U, 0U,
    };
    const uint8_t exact_payload[] = {
        9U, 0U, 0U, 0U,
        2U, 0U, 0U, 0U,
        4U, 0U, 0U, 0U,
    };
    const uint8_t ack_payload[] = {9U, 0U, 0U, 0U};
    const terp_message_t latest_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 6U,
        .payload = latest_payload,
        .payload_length = sizeof(latest_payload),
    };
    const terp_message_t exact_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 7U,
        .payload = exact_payload,
        .payload_length = sizeof(exact_payload),
    };
    const terp_message_t ack_request = {
        .message_type = TERP_MESSAGE_ACK_CRASH_RECORD,
        .flags = 0U,
        .sequence = 8U,
        .payload = ack_payload,
        .payload_length = sizeof(ack_payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    fake.crash_chunk.sequence = 9U;
    fake.crash_chunk.total_length = 128U;
    fake.crash_chunk.actual_length = 4U;
    memcpy(fake.crash_data, "CRSH", 4U);
    terp_device_init(&device, &ops, &fake);
    complete_hello(&device, work, response, &response_length);

    handle(&device, &latest_request, &capture);
    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_GET_CRASH_RECORD));
    assert(capture.message.payload_length == 24U);
    assert(get_u32_le(&capture.payload[0]) == 9U);
    assert(get_u32_le(&capture.payload[4]) == 2U);
    assert(get_u32_le(&capture.payload[8]) == 128U);
    assert(get_u32_le(&capture.payload[12]) == 4U);
    assert(get_u32_le(&capture.payload[16])
           == terp_crc32(&capture.payload[20], 4U));
    assert(memcmp(&capture.payload[20], "CRSH", 4U) == 0);
    assert(fake.last_crash_sequence == 0U);
    assert(fake.last_crash_offset == 2U);
    assert(fake.last_crash_length == 4U);

    capture.count = 0U;
    handle(&device, &exact_request, &capture);
    assert(capture.message.payload_length == 24U);
    assert(fake.last_crash_sequence == 9U);

    capture.count = 0U;
    handle(&device, &ack_request, &capture);
    assert(capture.message.message_type
           == terp_response_type(TERP_MESSAGE_ACK_CRASH_RECORD));
    assert(capture.message.payload_length == 4U);
    assert(get_u32_le(capture.payload) == 9U);
    assert(fake.ack_calls == 1U);

    capture.count = 0U;
    handle(&device, &ack_request, &capture);
    assert(get_u32_le(capture.payload) == 9U);
    assert(fake.ack_calls == 2U);
}

static void test_crash_chunk_errors(void)
{
    fake_reliability_t fake = {.crash_result = -TERP_ERROR_NOT_FOUND};
    const terp_device_ops_t ops = {.get_crash_record = get_crash_record};
    const uint8_t not_found_payload[] = {0U, 0U, 0U, 0U,
                                         0U, 0U, 0U, 0U,
                                         1U, 0U, 0U, 0U};
    const uint8_t zero_length_payload[] = {0U, 0U, 0U, 0U,
                                           0U, 0U, 0U, 0U,
                                           0U, 0U, 0U, 0U};
    const uint8_t overflow_payload[] = {0U, 0U, 0U, 0U,
                                         0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                         2U, 0U, 0U, 0U};
    const terp_message_t not_found_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 9U,
        .payload = not_found_payload,
        .payload_length = sizeof(not_found_payload),
    };
    const terp_message_t zero_length_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 10U,
        .payload = zero_length_payload,
        .payload_length = sizeof(zero_length_payload),
    };
    const terp_message_t overflow_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 11U,
        .payload = overflow_payload,
        .payload_length = sizeof(overflow_payload),
    };
    const uint8_t ack_zero_payload[] = {0U, 0U, 0U, 0U};
    const terp_message_t ack_zero_request = {
        .message_type = TERP_MESSAGE_ACK_CRASH_RECORD,
        .flags = 0U,
        .sequence = 12U,
        .payload = ack_zero_payload,
        .payload_length = sizeof(ack_zero_payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[TERP_MAX_PAYLOAD_BYTES];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    terp_device_init(&device, &ops, &fake);
    complete_hello(&device, work, response, &response_length);
    handle(&device, &not_found_request, &capture);
    assert_error(&capture, TERP_ERROR_NOT_FOUND, TERP_MESSAGE_GET_CRASH_RECORD);

    capture.count = 0U;
    handle(&device, &zero_length_request, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_GET_CRASH_RECORD);
    assert(fake.crash_calls == 1U);

    capture.count = 0U;
    handle(&device, &overflow_request, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_GET_CRASH_RECORD);
    assert(fake.crash_calls == 1U);

    capture.count = 0U;
    device.ops = 0;
    handle(&device, &ack_zero_request, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_ACK_CRASH_RECORD);
}

static void test_crash_chunk_respects_all_size_limits(void)
{
    fake_reliability_t fake = {0};
    const terp_device_ops_t ops = {.get_crash_record = get_crash_record};
    const uint8_t payload[] = {
        0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        9U, 0U, 0U, 0U,
    };
    const uint8_t work_limited_payload[] = {
        0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        13U, 0U, 0U, 0U,
    };
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 13U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    const terp_message_t work_limited_request = {
        .message_type = TERP_MESSAGE_GET_CRASH_RECORD,
        .flags = 0U,
        .sequence = 14U,
        .payload = work_limited_payload,
        .payload_length = sizeof(work_limited_payload),
    };
    terp_device_t device;
    response_capture_t capture = {0};
    uint8_t work[32];
    uint8_t response[TERP_MAX_FRAME_BYTES];
    uint32_t response_length = 0U;

    fake.crash_chunk.sequence = 9U;
    fake.crash_chunk.total_length = 128U;
    fake.crash_chunk.actual_length = 4U;
    terp_device_init(&device, &ops, &fake);
    complete_hello(&device, work, response, &response_length);

    device.maximum_event_chunk_bytes = 8U;
    handle(&device, &request, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_GET_CRASH_RECORD);
    assert(fake.crash_calls == 0U);

    device.maximum_event_chunk_bytes = TERP_DEVICE_MAX_EVENT_CHUNK_BYTES;
    capture.count = 0U;
    assert(terp_device_handle_request(&device, &work_limited_request, work,
                                      sizeof(work),
                                      response, sizeof(response),
                                      &response_length) == 0);
    parse_response(response, response_length, &capture);
    assert_error(&capture, TERP_ERROR_MALFORMED,
                 TERP_MESSAGE_GET_CRASH_RECORD);
    assert(fake.crash_calls == 0U);
}

int main(void)
{
    assert(terp_message_type_is_known(TERP_MESSAGE_GET_EVENT_EVIDENCE));
    assert(terp_message_type_is_known(TERP_MESSAGE_GET_CRASH_RECORD));
    assert(terp_message_type_is_known(TERP_MESSAGE_ACK_CRASH_RECORD));
    assert(TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1 == UINT32_C(0x100));
    test_event_evidence_status_mapping();
    test_event_evidence_errors_and_handshake();
    test_event_evidence_callback_errors_are_whitelisted();
    test_crash_chunk_bounds_identity_and_ack();
    test_crash_chunk_errors();
    test_crash_chunk_respects_all_size_limits();
    puts("terp reliability device: PASS");
    return 0;
}
