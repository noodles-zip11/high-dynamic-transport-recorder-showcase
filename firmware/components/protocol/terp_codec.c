#include "terp_codec.h"

#include <string.h>

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

uint32_t terp_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t index;
    uint8_t bit;

    if (data == 0 && length != 0U)
    {
        return 0U;
    }
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ UINT32_C(0xEDB88320)
                                     : crc >> 1U;
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

uint16_t terp_response_type(uint16_t request_type)
{
    return (uint16_t)(request_type | TERP_FLAG_RESPONSE);
}

int terp_message_type_is_known(uint16_t message_type)
{
    uint16_t base_type;

    if (message_type == TERP_MESSAGE_ERROR)
    {
        return 1;
    }
    base_type = (uint16_t)(message_type
                            & (uint16_t)~(TERP_FLAG_RESPONSE
                                         | TERP_FLAG_NOTIFICATION));

    switch (base_type)
    {
    case TERP_MESSAGE_HELLO:
    case TERP_MESSAGE_GET_DEVICE_INFO:
    case TERP_MESSAGE_GET_HEALTH:
    case TERP_MESSAGE_GET_TIME:
    case TERP_MESSAGE_SET_TIME:
    case TERP_MESSAGE_LIST_EVENTS:
    case TERP_MESSAGE_GET_EVENT_INFO:
    case TERP_MESSAGE_READ_EVENT_CHUNK:
    case TERP_MESSAGE_DELETE_EVENT:
    case TERP_MESSAGE_START_LIVE:
    case TERP_MESSAGE_STOP_LIVE:
    case TERP_MESSAGE_OTA_BEGIN:
    case TERP_MESSAGE_OTA_WRITE_CHUNK:
    case TERP_MESSAGE_OTA_QUERY:
    case TERP_MESSAGE_OTA_FINALIZE:
    case TERP_MESSAGE_OTA_CANCEL:
    case TERP_MESSAGE_OTA_RESULT:
    case TERP_MESSAGE_OTA_RECOVERY_BEGIN:
    case TERP_MESSAGE_OTA_RECOVERY_WRITE_CHUNK:
    case TERP_MESSAGE_OTA_RECOVERY_QUERY:
    case TERP_MESSAGE_OTA_RECOVERY_FINALIZE:
    case TERP_MESSAGE_OTA_RECOVERY_CANCEL:
    case TERP_MESSAGE_GET_EVENT_EVIDENCE:
    case TERP_MESSAGE_GET_CRASH_RECORD:
    case TERP_MESSAGE_ACK_CRASH_RECORD:
    case TERP_MESSAGE_GET_AI_RESULT:
    case TERP_MESSAGE_ERROR:
        return 1;
    default:
        return 0;
    }
}

int terp_encode_frame(const terp_message_t *message,
                      uint8_t *output,
                      uint32_t output_capacity,
                      uint32_t *output_length)
{
    uint32_t frame_length;
    uint32_t header_crc;

    if (message == 0 || output == 0 || output_length == 0
        || message->payload_length > TERP_MAX_PAYLOAD_BYTES
        || (message->payload_length != 0U && message->payload == 0))
    {
        return -1;
    }
    frame_length = TERP_FRAME_OVERHEAD_BYTES + message->payload_length;
    if (output_capacity < frame_length)
    {
        return -1;
    }

    output[0] = TERP_SYNC_BYTE_0;
    output[1] = TERP_SYNC_BYTE_1;
    output[2] = TERP_VERSION;
    output[3] = TERP_HEADER_LENGTH;
    put_u16_le(&output[4], message->message_type);
    put_u16_le(&output[6], message->flags);
    put_u32_le(&output[8], message->sequence);
    put_u32_le(&output[12], message->payload_length);
    header_crc = terp_crc32(&output[2], 14U);
    put_u32_le(&output[16], header_crc);
    if (message->payload_length != 0U)
    {
        memcpy(&output[20], message->payload, message->payload_length);
    }
    put_u32_le(&output[20U + message->payload_length],
               terp_crc32(message->payload, message->payload_length));
    *output_length = frame_length;
    return 0;
}
