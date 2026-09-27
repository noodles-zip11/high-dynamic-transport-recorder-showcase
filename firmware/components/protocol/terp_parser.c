#include "terp_parser.h"

#include <string.h>

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void parser_reset_frame(terp_parser_t *parser)
{
    parser->state = TERP_PARSER_WAIT_SYNC_0;
    parser->header_received = 0U;
    parser->payload_length = 0U;
    parser->payload_received = 0U;
    parser->payload_crc_received = 0U;
}

static void parser_start_frame(terp_parser_t *parser)
{
    parser->header[0] = TERP_SYNC_BYTE_0;
    parser->header[1] = TERP_SYNC_BYTE_1;
    parser->header_received = 2U;
    parser->state = TERP_PARSER_HEADER;
}

static void parser_rescan_after_header_error(terp_parser_t *parser,
                                             terp_parser_message_fn message_callback,
                                             void *context);

static void parser_rescan_after_payload_error(terp_parser_t *parser,
                                              terp_parser_message_fn message_callback,
                                              void *context);

static int parser_header_is_valid(terp_parser_t *parser)
{
    uint32_t header_crc = get_u32_le(&parser->header[16]);

    if (parser->header[2] != TERP_VERSION)
    {
        parser->stats.version_error_count++;
        return 0;
    }
    if (parser->header[3] != TERP_HEADER_LENGTH
        || get_u32_le(&parser->header[12]) > TERP_MAX_PAYLOAD_BYTES)
    {
        parser->stats.length_error_count++;
        return 0;
    }
    if (header_crc != terp_crc32(&parser->header[2], 14U))
    {
        parser->stats.header_crc_error_count++;
        return 0;
    }
    parser->payload_length = get_u32_le(&parser->header[12]);
    return 1;
}

static int parser_deliver(terp_parser_t *parser,
                          terp_parser_message_fn message_callback,
                          void *context)
{
    terp_message_t message;

    if (get_u32_le(parser->payload_crc) != terp_crc32(parser->payload,
                                                       parser->payload_length))
    {
        parser->stats.payload_crc_error_count++;
        return 0;
    }
    message.message_type = get_u16_le(&parser->header[4]);
    message.flags = get_u16_le(&parser->header[6]);
    message.sequence = get_u32_le(&parser->header[8]);
    message.payload = parser->payload_length == 0U ? 0 : parser->payload;
    message.payload_length = parser->payload_length;
    if (!terp_message_type_is_known(message.message_type))
    {
        parser->stats.unknown_message_count++;
    }
    if (message_callback != 0)
    {
        message_callback(&message, context);
    }
    return 1;
}

static void parser_rescan_after_header_error(terp_parser_t *parser,
                                             terp_parser_message_fn message_callback,
                                             void *context)
{
    if (parser->rescan_active)
    {
        parser_reset_frame(parser);
        return;
    }
    parser->rescan_active = 1U;
    parser_reset_frame(parser);
    terp_parser_feed(parser, &parser->header[1], TERP_HEADER_LENGTH - 1U,
                     message_callback, context);
    parser->rescan_active = 0U;
}

static void parser_rescan_after_payload_error(terp_parser_t *parser,
                                              terp_parser_message_fn message_callback,
                                              void *context)
{
    uint32_t payload_length = parser->payload_received;
    uint32_t payload_crc_length = parser->payload_crc_received;

    if (parser->rescan_active)
    {
        parser_reset_frame(parser);
        return;
    }
    parser->rescan_active = 1U;
    parser_reset_frame(parser);
    terp_parser_feed(parser, &parser->header[1], TERP_HEADER_LENGTH - 1U,
                     message_callback, context);
    terp_parser_feed(parser, parser->payload, payload_length,
                     message_callback, context);
    terp_parser_feed(parser, parser->payload_crc, payload_crc_length,
                     message_callback, context);
    parser->rescan_active = 0U;
}

void terp_parser_init(terp_parser_t *parser)
{
    if (parser == 0)
    {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser_reset_frame(parser);
}

void terp_parser_feed(terp_parser_t *parser,
                      const uint8_t *data,
                      uint32_t length,
                      terp_parser_message_fn message_callback,
                      void *context)
{
    uint32_t index;

    if (parser == 0 || (data == 0 && length != 0U))
    {
        return;
    }
    for (index = 0U; index < length; index++)
    {
        uint8_t value = data[index];

        switch (parser->state)
        {
        case TERP_PARSER_WAIT_SYNC_0:
            if (value == TERP_SYNC_BYTE_0)
            {
                parser->state = TERP_PARSER_WAIT_SYNC_1;
            }
            else
            {
                parser->stats.resync_byte_count++;
            }
            break;
        case TERP_PARSER_WAIT_SYNC_1:
            if (value == TERP_SYNC_BYTE_1)
            {
                parser_start_frame(parser);
            }
            else if (value == TERP_SYNC_BYTE_0)
            {
                parser->stats.resync_byte_count++;
            }
            else
            {
                parser->stats.resync_byte_count += 2U;
                parser_reset_frame(parser);
            }
            break;
        case TERP_PARSER_HEADER:
            parser->header[parser->header_received++] = value;
            if (parser->header_received == TERP_HEADER_LENGTH)
            {
                if (!parser_header_is_valid(parser))
                {
                    parser->stats.resync_byte_count++;
                    parser_rescan_after_header_error(parser, message_callback,
                                                      context);
                }
                else if (parser->payload_length == 0U)
                {
                    parser->state = TERP_PARSER_PAYLOAD_CRC;
                }
                else
                {
                    parser->state = TERP_PARSER_PAYLOAD;
                }
            }
            break;
        case TERP_PARSER_PAYLOAD:
            parser->payload[parser->payload_received++] = value;
            if (parser->payload_received == parser->payload_length)
            {
                parser->state = TERP_PARSER_PAYLOAD_CRC;
            }
            break;
        case TERP_PARSER_PAYLOAD_CRC:
            parser->payload_crc[parser->payload_crc_received++] = value;
            if (parser->payload_crc_received == sizeof(parser->payload_crc))
            {
                if (parser_deliver(parser, message_callback, context))
                {
                    parser_reset_frame(parser);
                }
                else
                {
                    parser->stats.resync_byte_count++;
                    parser_rescan_after_payload_error(parser, message_callback,
                                                       context);
                }
            }
            break;
        default:
            parser_reset_frame(parser);
            break;
        }
    }
}

void terp_parser_on_timeout(terp_parser_t *parser)
{
    if (parser != 0 && parser->state != TERP_PARSER_WAIT_SYNC_0)
    {
        parser->stats.timeout_count++;
        parser_reset_frame(parser);
    }
}
