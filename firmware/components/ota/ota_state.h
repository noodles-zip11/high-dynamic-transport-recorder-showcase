#ifndef TRANSPORT_RECORDER_OTA_STATE_H
#define TRANSPORT_RECORDER_OTA_STATE_H

#include <stdint.h>

#define OTA_STATE_RECORD_BYTES 48U
#define OTA_STATE_MAX_TRIALS 3U

#define OTA_STATE_OFFSET_MAGIC 0U
#define OTA_STATE_OFFSET_FORMAT_VERSION 4U
#define OTA_STATE_OFFSET_HEADER_BYTES 6U
#define OTA_STATE_OFFSET_GENERATION 8U
#define OTA_STATE_OFFSET_KIND 12U
#define OTA_STATE_OFFSET_TRIAL_COUNT 16U
#define OTA_STATE_OFFSET_COPY_OFFSET 20U
#define OTA_STATE_OFFSET_RESERVED 24U
#define OTA_STATE_RESERVED_BYTES 16U
#define OTA_STATE_OFFSET_CRC32 40U
#define OTA_STATE_OFFSET_COMMIT_MARKER 44U

typedef enum
{
    OTA_STATE_NORMAL = 0,
    OTA_STATE_PENDING_INSTALL,
    OTA_STATE_INSTALLING,
    OTA_STATE_TRIAL,
    OTA_STATE_CONFIRMED,
    OTA_STATE_ROLLBACK_REQUIRED,
} ota_state_kind_t;

typedef struct
{
    uint32_t generation;
    ota_state_kind_t state;
    uint32_t trial_count;
    uint32_t copy_offset;
} ota_state_record_t;

typedef enum
{
    OTA_STATE_SELECT_PRIMARY = 0,
    OTA_STATE_SELECT_SECONDARY,
    OTA_STATE_SELECT_RECOVERY,
    OTA_STATE_SELECT_UNINITIALIZED,
} ota_state_selection_t;

typedef enum
{
    OTA_STATE_TRANSITION_OK = 0,
    OTA_STATE_TRANSITION_INVALID_ARGUMENT,
    OTA_STATE_TRANSITION_ILLEGAL,
    OTA_STATE_TRANSITION_GENERATION_OVERFLOW,
} ota_state_transition_status_t;

typedef enum
{
    OTA_STATE_TRIAL_CONTINUE = 0,
    OTA_STATE_TRIAL_ROLLBACK_REQUIRED,
    OTA_STATE_TRIAL_INVALID_STATE,
    OTA_STATE_TRIAL_GENERATION_OVERFLOW,
} ota_state_trial_result_t;

void ota_state_record_encode(const ota_state_record_t *record,
                             uint8_t encoded[OTA_STATE_RECORD_BYTES]);
int ota_state_record_decode(const uint8_t encoded[OTA_STATE_RECORD_BYTES],
                            ota_state_record_t *record);
ota_state_selection_t ota_state_select_newest(
    const uint8_t primary[OTA_STATE_RECORD_BYTES],
    const uint8_t secondary[OTA_STATE_RECORD_BYTES],
    ota_state_record_t *selected);
ota_state_transition_status_t ota_state_transition(const ota_state_record_t *current,
                                                   ota_state_kind_t next_state,
                                                   ota_state_record_t *next);
ota_state_trial_result_t ota_state_prepare_trial_failure(
    const ota_state_record_t *current, ota_state_record_t *next);

#endif
