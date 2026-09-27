#ifndef TRANSPORT_RECORDER_AI_RESULT_H
#define TRANSPORT_RECORDER_AI_RESULT_H

#include <stdint.h>

#include "ai_runtime.h"

#define AI_RESULT_QUALITY_EVENT_FLAGS_VALID UINT8_C(1 << 0)
#define AI_RESULT_QUALITY_SAMPLE_COUNT_VALID UINT8_C(1 << 1)
#define AI_RESULT_QUALITY_DATA_LOSS UINT8_C(1 << 2)
#define AI_RESULT_QUALITY_PRETRIGGER_SHORT UINT8_C(1 << 3)
#define AI_RESULT_QUALITY_DURATION_CAPPED UINT8_C(1 << 4)
#define AI_RESULT_QUALITY_RESOURCE_LIMIT UINT8_C(1 << 5)

typedef enum
{
    AI_RESULT_STATUS_NONE = 0,
    AI_RESULT_STATUS_PREDICTION,
    AI_RESULT_STATUS_MODEL_UNAVAILABLE,
    AI_RESULT_STATUS_FEATURE_ERROR,
    AI_RESULT_STATUS_RUNTIME_ERROR,
    AI_RESULT_STATUS_RESOURCE_LIMIT,
} ai_result_status_t;

typedef enum
{
    AI_RESULT_FAILURE_NONE = 0,
    AI_RESULT_FAILURE_NO_MODEL,
    AI_RESULT_FAILURE_FEATURE_EXTRACTION,
    AI_RESULT_FAILURE_RUNTIME,
    AI_RESULT_FAILURE_SIDECAR,
    AI_RESULT_FAILURE_RESOURCE_LIMIT,
} ai_result_failure_t;

typedef struct
{
    uint32_t result_sequence;
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
    float logits[AI_MODEL_MAX_CLASSES];
    uint16_t failure_reason;
} ai_result_t;

#endif
