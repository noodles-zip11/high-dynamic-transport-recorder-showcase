#ifndef TRANSPORT_RECORDER_TERP_CODEC_H
#define TRANSPORT_RECORDER_TERP_CODEC_H

#include <stdint.h>

#include "terp_messages_generated.h"

#define TERP_SYNC_BYTE_0 UINT8_C(0x54)
#define TERP_SYNC_BYTE_1 UINT8_C(0x52)
#define TERP_VERSION UINT8_C(1)
#define TERP_HEADER_LENGTH UINT8_C(20)
#define TERP_MAX_PAYLOAD_BYTES UINT32_C(4096)
#define TERP_FRAME_OVERHEAD_BYTES UINT32_C(24)
#define TERP_MAX_FRAME_BYTES (TERP_MAX_PAYLOAD_BYTES + TERP_FRAME_OVERHEAD_BYTES)

#define TERP_FLAG_RESPONSE UINT16_C(0x8000)
#define TERP_FLAG_NOTIFICATION UINT16_C(0x4000)

typedef struct
{
    uint16_t message_type;
    uint16_t flags;
    uint32_t sequence;
    const uint8_t *payload;
    uint32_t payload_length;
} terp_message_t;

uint32_t terp_crc32(const uint8_t *data, uint32_t length);
uint16_t terp_response_type(uint16_t request_type);
int terp_message_type_is_known(uint16_t message_type);
int terp_encode_frame(const terp_message_t *message,
                      uint8_t *output,
                      uint32_t output_capacity,
                      uint32_t *output_length);

#endif
