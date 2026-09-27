#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "terp_codec.h"
#include "terp_parser.h"

typedef struct
{
    uint32_t count;
    terp_message_t last_message;
    uint8_t payload[TERP_MAX_PAYLOAD_BYTES];
} capture_t;

static uint32_t read_golden_vector(const char *filename,
                                   uint8_t *data,
                                   uint32_t capacity)
{
    static const char *const prefixes[] = {
        "protocol/golden/",
        "../../../protocol/golden/",
    };
    char path[128];
    FILE *file = NULL;
    size_t length;
    uint32_t index;

    for (index = 0U; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
    {
        (void)snprintf(path, sizeof(path), "%s%s", prefixes[index], filename);
        file = fopen(path, "rb");
        if (file != NULL)
        {
            break;
        }
    }
    assert(file != NULL);
    length = fread(data, 1U, capacity, file);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    return (uint32_t)length;
}

static void capture_message(const terp_message_t *message, void *context)
{
    capture_t *capture = context;

    capture->count++;
    capture->last_message = *message;
    if (message->payload_length > 0U)
    {
        memcpy(capture->payload, message->payload, message->payload_length);
        capture->last_message.payload = capture->payload;
    }
}

static void test_round_trip_with_single_byte_input(void)
{
    const uint8_t payload[] = {0x01U, 0x02U};
    const terp_message_t message = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = UINT32_C(0x10203040),
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    uint32_t index;
    terp_parser_t parser;
    capture_t capture = {0};

    assert(terp_encode_frame(&message, encoded, sizeof(encoded), &encoded_length) == 0);
    terp_parser_init(&parser);
    for (index = 0U; index < encoded_length; index++)
    {
        terp_parser_feed(&parser, &encoded[index], 1U, capture_message, &capture);
    }

    assert(capture.count == 1U);
    assert(capture.last_message.message_type == TERP_MESSAGE_HELLO);
    assert(capture.last_message.sequence == UINT32_C(0x10203040));
    assert(capture.last_message.payload_length == sizeof(payload));
    assert(memcmp(capture.payload, payload, sizeof(payload)) == 0);
}

static void test_parser_recovers_after_payload_crc_error(void)
{
    const terp_message_t message = {
        .message_type = TERP_MESSAGE_GET_DEVICE_INFO,
        .flags = 0U,
        .sequence = 1U,
        .payload = NULL,
        .payload_length = 0U,
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_parser_t parser;
    capture_t capture = {0};

    assert(terp_encode_frame(&message, encoded, sizeof(encoded), &encoded_length) == 0);
    encoded[encoded_length - 1U] ^= 0x01U;
    terp_parser_init(&parser);
    terp_parser_feed(&parser, encoded, encoded_length, capture_message, &capture);
    assert(capture.count == 0U);
    assert(parser.stats.payload_crc_error_count == 1U);

    assert(terp_encode_frame(&message, encoded, sizeof(encoded), &encoded_length) == 0);
    terp_parser_feed(&parser, encoded, encoded_length, capture_message, &capture);
    assert(capture.count == 1U);
}

static void test_parser_recovers_a_valid_frame_inside_a_corrupt_payload(void)
{
    const terp_message_t inner = {
        .message_type = TERP_MESSAGE_GET_DEVICE_INFO,
        .flags = 0U,
        .sequence = UINT32_C(0x55667788),
        .payload = NULL,
        .payload_length = 0U,
    };
    terp_message_t outer = {
        .message_type = TERP_MESSAGE_HELLO,
        .flags = 0U,
        .sequence = 1U,
    };
    uint8_t inner_encoded[TERP_MAX_FRAME_BYTES];
    uint8_t outer_encoded[TERP_MAX_FRAME_BYTES];
    uint32_t inner_length = 0U;
    uint32_t outer_length = 0U;
    terp_parser_t parser;
    capture_t capture = {0};

    assert(terp_encode_frame(&inner, inner_encoded, sizeof(inner_encoded),
                             &inner_length) == 0);
    outer.payload = inner_encoded;
    outer.payload_length = inner_length;
    assert(terp_encode_frame(&outer, outer_encoded, sizeof(outer_encoded),
                             &outer_length) == 0);
    outer_encoded[outer_length - 1U] ^= 0x01U;

    terp_parser_init(&parser);
    terp_parser_feed(&parser, outer_encoded, outer_length, capture_message, &capture);
    assert(parser.stats.payload_crc_error_count == 1U);
    assert(capture.count == 1U);
    assert(capture.last_message.message_type == TERP_MESSAGE_GET_DEVICE_INFO);
    assert(capture.last_message.sequence == UINT32_C(0x55667788));
}

static void test_shared_golden_vectors(void)
{
    const terp_message_t request = {
        .message_type = TERP_MESSAGE_GET_DEVICE_INFO,
        .flags = 0U,
        .sequence = 1U,
        .payload = NULL,
        .payload_length = 0U,
    };
    uint8_t golden[TERP_MAX_FRAME_BYTES];
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t golden_length;
    uint32_t encoded_length;
    terp_parser_t parser;
    capture_t capture = {0};

    golden_length = read_golden_vector("get_device_info_request.bin", golden,
                                       sizeof(golden));
    assert(terp_encode_frame(&request, encoded, sizeof(encoded), &encoded_length) == 0);
    assert(encoded_length == golden_length);
    assert(memcmp(encoded, golden, encoded_length) == 0);

    golden_length = read_golden_vector("get_device_info_response.bin", golden,
                                       sizeof(golden));
    terp_parser_init(&parser);
    terp_parser_feed(&parser, golden, golden_length, capture_message, &capture);
    assert(capture.count == 1U);
    assert(capture.last_message.message_type
           == terp_response_type(TERP_MESSAGE_GET_DEVICE_INFO));

    memset(&capture, 0, sizeof(capture));
    golden_length = read_golden_vector("event_chunk.bin", golden, sizeof(golden));
    terp_parser_feed(&parser, golden, golden_length, capture_message, &capture);
    assert(capture.count == 1U);
    assert(capture.last_message.message_type
           == terp_response_type(TERP_MESSAGE_READ_EVENT_CHUNK));
    assert(capture.last_message.payload_length == 26U);
    assert(capture.payload[20] == UINT8_C(0xA0));

    golden[golden_length - 1U] ^= 1U;
    terp_parser_feed(&parser, golden, golden_length, capture_message, &capture);
    assert(parser.stats.payload_crc_error_count == 1U);
}

static void test_structured_error_is_not_an_unknown_message(void)
{
    const uint8_t payload[] = {UINT8_C(2), 0U, 9U, 0U};
    const terp_message_t message = {
        .message_type = TERP_MESSAGE_ERROR,
        .flags = TERP_FLAG_RESPONSE,
        .sequence = 1U,
        .payload = payload,
        .payload_length = sizeof(payload),
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    terp_parser_t parser;
    capture_t capture = {0};

    assert(terp_encode_frame(&message, encoded, sizeof(encoded), &encoded_length) == 0);
    terp_parser_init(&parser);
    terp_parser_feed(&parser, encoded, encoded_length, capture_message, &capture);
    assert(capture.count == 1U);
    assert(parser.stats.unknown_message_count == 0U);
}

static void test_recovers_from_one_thousand_corrupted_frames(void)
{
    const terp_message_t message = {
        .message_type = TERP_MESSAGE_GET_DEVICE_INFO,
        .flags = 0U,
        .sequence = 99U,
        .payload = NULL,
        .payload_length = 0U,
    };
    uint8_t encoded[TERP_MAX_FRAME_BYTES];
    uint8_t corrupt[TERP_MAX_FRAME_BYTES];
    uint32_t encoded_length = 0U;
    uint32_t index;

    assert(terp_encode_frame(&message, encoded, sizeof(encoded), &encoded_length) == 0);
    for (index = 0U; index < 1000U; index++)
    {
        terp_parser_t parser;
        capture_t capture = {0};
        uint32_t corrupt_index = index % encoded_length;

        memcpy(corrupt, encoded, encoded_length);
        corrupt[corrupt_index] ^= (uint8_t)((index % UINT32_C(255)) + 1U);
        terp_parser_init(&parser);
        terp_parser_feed(&parser, corrupt, encoded_length, capture_message, &capture);
        terp_parser_feed(&parser, encoded, encoded_length, capture_message, &capture);
        assert(capture.count == 1U);
        assert(capture.last_message.sequence == 99U);
    }
}

int main(void)
{
    test_round_trip_with_single_byte_input();
    test_parser_recovers_after_payload_crc_error();
    test_parser_recovers_a_valid_frame_inside_a_corrupt_payload();
    test_shared_golden_vectors();
    test_structured_error_is_not_an_unknown_message();
    test_recovers_from_one_thousand_corrupted_frames();
    puts("terp: PASS");
    return 0;
}
