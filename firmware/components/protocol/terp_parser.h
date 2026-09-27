#ifndef TRANSPORT_RECORDER_TERP_PARSER_H
#define TRANSPORT_RECORDER_TERP_PARSER_H

#include <stdint.h>

#include "terp_codec.h"

typedef struct
{
    uint32_t header_crc_error_count;
    uint32_t payload_crc_error_count;
    uint32_t length_error_count;
    uint32_t version_error_count;
    uint32_t unknown_message_count;
    uint32_t resync_byte_count;
    uint32_t timeout_count;
} terp_parser_stats_t;

typedef void (*terp_parser_message_fn)(const terp_message_t *message,
                                       void *context);

typedef enum
{
    TERP_PARSER_WAIT_SYNC_0 = 0,
    TERP_PARSER_WAIT_SYNC_1,
    TERP_PARSER_HEADER,
    TERP_PARSER_PAYLOAD,
    TERP_PARSER_PAYLOAD_CRC,
} terp_parser_state_t;

typedef struct
{
    terp_parser_state_t state;
    uint8_t header[TERP_HEADER_LENGTH];
    uint8_t payload[TERP_MAX_PAYLOAD_BYTES];
    uint8_t payload_crc[4];
    uint32_t header_received;
    uint32_t payload_length;
    uint32_t payload_received;
    uint32_t payload_crc_received;
    uint8_t rescan_active;
    terp_parser_stats_t stats;
} terp_parser_t;

void terp_parser_init(terp_parser_t *parser);
void terp_parser_feed(terp_parser_t *parser,
                      const uint8_t *data,
                      uint32_t length,
                      terp_parser_message_fn message_callback,
                      void *context);
void terp_parser_on_timeout(terp_parser_t *parser);

#endif
