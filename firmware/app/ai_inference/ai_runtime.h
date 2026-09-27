#ifndef TRANSPORT_RECORDER_AI_RUNTIME_H
#define TRANSPORT_RECORDER_AI_RUNTIME_H

#include <stdint.h>

#include "ai_features.h"

#define AI_MODEL_MAGIC UINT32_C(0x314D4941) /* "AIM1" in little-endian */
#define AI_MODEL_VERSION 2U
#define AI_MODEL_MAX_HIDDEN_UNITS 64U
#define AI_MODEL_MAX_CLASSES 4U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t feature_count;
    uint8_t hidden_units;
    uint8_t class_count;
    uint8_t reserved;
    float input_scale;
    float input_zero_point;
    float hidden_scale;
    float hidden_zero_point;
    const float *feature_mean;
    const float *feature_scale;
    const int8_t *weights1;
    float weights1_scale;
    const float *bias1;
    const int8_t *weights2;
    float weights2_scale;
    const float *bias2;
    uint32_t model_crc32;
} ai_model_t;

/* Fixed storage used when a validated model package is loaded from QSPI. */
typedef struct
{
    float feature_mean[AI_FEATURE_COUNT];
    float feature_scale[AI_FEATURE_COUNT];
    int8_t weights1[AI_FEATURE_COUNT * AI_MODEL_MAX_HIDDEN_UNITS];
    float bias1[AI_MODEL_MAX_HIDDEN_UNITS];
    int8_t weights2[AI_MODEL_MAX_HIDDEN_UNITS * AI_MODEL_MAX_CLASSES];
    float bias2[AI_MODEL_MAX_CLASSES];
} ai_model_storage_t;

typedef struct
{
    uint8_t class_index;
    float logits[AI_MODEL_MAX_CLASSES];
    float confidence;
} ai_prediction_t;

rt_err_t ai_runtime_validate_model(const ai_model_t *model);

uint32_t ai_runtime_model_crc32(const ai_model_t *model);

/* Decode the canonical little-endian payload used by the model OTA package. */
rt_err_t ai_runtime_decode_model(const uint8_t *payload, uint32_t payload_length,
                                 ai_model_storage_t *storage,
                                 ai_model_t *model);

rt_err_t ai_runtime_infer(const ai_model_t *model,
                          const float features[AI_FEATURE_COUNT],
                          ai_prediction_t *prediction);

#endif
