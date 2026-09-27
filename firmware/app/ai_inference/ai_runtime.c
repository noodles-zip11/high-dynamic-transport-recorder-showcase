#include "ai_runtime.h"

#include <math.h>
#include <string.h>

#include "ota_crc32.h"

static int8_t ai_runtime_quantize(float value, float scale, float zero_point)
{
    float quantized = value / scale + zero_point;
    if (quantized > 127.0F)
    {
        return 127;
    }
    if (quantized < -128.0F)
    {
        return -128;
    }
    return (int8_t)(quantized >= 0.0F
                    ? floorf(quantized + 0.5F)
                    : ceilf(quantized - 0.5F));
}

static float ai_runtime_dequantize(int8_t value, float scale, float zero_point)
{
    return ((float)value - zero_point) * scale;
}

static void ai_runtime_crc_u8(ota_crc32_t *context, uint8_t value)
{
    ota_crc32_update(context, &value, sizeof(value));
}

static void ai_runtime_crc_u16(ota_crc32_t *context, uint16_t value)
{
    const uint8_t bytes[2] = {
        (uint8_t)(value & UINT16_C(0xFF)),
        (uint8_t)(value >> 8U),
    };

    ota_crc32_update(context, bytes, sizeof(bytes));
}

static void ai_runtime_crc_u32(ota_crc32_t *context, uint32_t value)
{
    const uint8_t bytes[4] = {
        (uint8_t)(value & UINT32_C(0xFF)),
        (uint8_t)((value >> 8U) & UINT32_C(0xFF)),
        (uint8_t)((value >> 16U) & UINT32_C(0xFF)),
        (uint8_t)(value >> 24U),
    };

    ota_crc32_update(context, bytes, sizeof(bytes));
}

static void ai_runtime_crc_float(ota_crc32_t *context, float value)
{
    uint32_t bits = 0U;

    memcpy(&bits, &value, sizeof(bits));
    ai_runtime_crc_u32(context, bits);
}

static uint16_t ai_runtime_payload_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ai_runtime_payload_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static float ai_runtime_payload_float(const uint8_t *data)
{
    uint32_t bits = ai_runtime_payload_u32(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

rt_err_t ai_runtime_decode_model(const uint8_t *payload, uint32_t payload_length,
                                 ai_model_storage_t *storage,
                                 ai_model_t *model)
{
    uint32_t offset = 0U;
    uint32_t expected_length;
    uint32_t index;
    uint8_t feature_count;
    uint8_t hidden_units;
    uint8_t class_count;

    if (payload == RT_NULL || storage == RT_NULL || model == RT_NULL
        || payload_length < 10U)
    {
        return -RT_ERROR;
    }
    feature_count = payload[6];
    hidden_units = payload[7];
    class_count = payload[8];
    if (ai_runtime_payload_u32(payload) != AI_MODEL_MAGIC
        || ai_runtime_payload_u16(&payload[4]) != AI_MODEL_VERSION
        || feature_count != AI_FEATURE_COUNT
        || hidden_units == 0U || hidden_units > AI_MODEL_MAX_HIDDEN_UNITS
        || class_count < 2U || class_count > AI_MODEL_MAX_CLASSES)
    {
        return -RT_ERROR;
    }
    expected_length = 10U + 16U + (uint32_t)feature_count * 8U
                      + (uint32_t)feature_count * hidden_units + 4U
                      + (uint32_t)hidden_units * 4U
                      + (uint32_t)hidden_units * class_count + 4U
                      + (uint32_t)class_count * 4U + 4U;
    if (payload_length != expected_length)
    {
        return -RT_ERROR;
    }

    model->magic = ai_runtime_payload_u32(&payload[offset]);
    offset += 4U;
    model->version = ai_runtime_payload_u16(&payload[offset]);
    offset += 2U;
    model->feature_count = payload[offset++];
    model->hidden_units = payload[offset++];
    model->class_count = payload[offset++];
    model->reserved = payload[offset++];
    model->input_scale = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    model->input_zero_point = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    model->hidden_scale = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    model->hidden_zero_point = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    for (index = 0U; index < feature_count; index++)
    {
        storage->feature_mean[index] =
            ai_runtime_payload_float(&payload[offset]);
        offset += 4U;
        storage->feature_scale[index] =
            ai_runtime_payload_float(&payload[offset]);
        offset += 4U;
    }
    memcpy(storage->weights1, &payload[offset],
           (uint32_t)feature_count * hidden_units);
    offset += (uint32_t)feature_count * hidden_units;
    model->weights1_scale = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    for (index = 0U; index < hidden_units; index++)
    {
        storage->bias1[index] = ai_runtime_payload_float(&payload[offset]);
        offset += 4U;
    }
    memcpy(storage->weights2, &payload[offset],
           (uint32_t)hidden_units * class_count);
    offset += (uint32_t)hidden_units * class_count;
    model->weights2_scale = ai_runtime_payload_float(&payload[offset]);
    offset += 4U;
    for (index = 0U; index < class_count; index++)
    {
        storage->bias2[index] = ai_runtime_payload_float(&payload[offset]);
        offset += 4U;
    }
    model->feature_mean = storage->feature_mean;
    model->feature_scale = storage->feature_scale;
    model->weights1 = storage->weights1;
    model->bias1 = storage->bias1;
    model->weights2 = storage->weights2;
    model->bias2 = storage->bias2;
    model->model_crc32 = ai_runtime_payload_u32(&payload[offset]);
    return ai_runtime_validate_model(model);
}

uint32_t ai_runtime_model_crc32(const ai_model_t *model)
{
    ota_crc32_t context;
    uint32_t index;

    if (model == RT_NULL)
    {
        return 0U;
    }
    ota_crc32_init(&context);
    ai_runtime_crc_u32(&context, model->magic);
    ai_runtime_crc_u16(&context, model->version);
    ai_runtime_crc_u8(&context, model->feature_count);
    ai_runtime_crc_u8(&context, model->hidden_units);
    ai_runtime_crc_u8(&context, model->class_count);
    ai_runtime_crc_u8(&context, model->reserved);
    ai_runtime_crc_float(&context, model->input_scale);
    ai_runtime_crc_float(&context, model->input_zero_point);
    ai_runtime_crc_float(&context, model->hidden_scale);
    ai_runtime_crc_float(&context, model->hidden_zero_point);
    for (index = 0U; index < model->feature_count; index++)
    {
        ai_runtime_crc_float(&context, model->feature_mean[index]);
        ai_runtime_crc_float(&context, model->feature_scale[index]);
    }
    ota_crc32_update(&context,
                     (const uint8_t *)model->weights1,
                     (uint32_t)model->feature_count * model->hidden_units);
    ai_runtime_crc_float(&context, model->weights1_scale);
    for (index = 0U; index < model->hidden_units; index++)
    {
        ai_runtime_crc_float(&context, model->bias1[index]);
    }
    ota_crc32_update(&context,
                     (const uint8_t *)model->weights2,
                     (uint32_t)model->hidden_units * model->class_count);
    ai_runtime_crc_float(&context, model->weights2_scale);
    for (index = 0U; index < model->class_count; index++)
    {
        ai_runtime_crc_float(&context, model->bias2[index]);
    }
    return ota_crc32_final(&context);
}

rt_err_t ai_runtime_validate_model(const ai_model_t *model)
{
    uint8_t index;

    if (model == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (model->magic != AI_MODEL_MAGIC
        || model->version != AI_MODEL_VERSION
        || model->feature_count != AI_FEATURE_COUNT
        || model->hidden_units == 0U
        || model->hidden_units > AI_MODEL_MAX_HIDDEN_UNITS
        || model->class_count < 2U
        || model->class_count > AI_MODEL_MAX_CLASSES
        || model->feature_mean == RT_NULL
        || model->feature_scale == RT_NULL
        || model->weights1 == RT_NULL || model->bias1 == RT_NULL
        || model->weights2 == RT_NULL || model->bias2 == RT_NULL
        || !isfinite(model->input_scale) || !(model->input_scale > 0.0F)
        || !isfinite(model->input_zero_point)
        || !isfinite(model->hidden_scale) || !(model->hidden_scale > 0.0F)
        || !isfinite(model->hidden_zero_point)
        || !isfinite(model->weights1_scale)
        || !(model->weights1_scale > 0.0F)
        || !isfinite(model->weights2_scale)
        || !(model->weights2_scale > 0.0F))
    {
        return -RT_ERROR;
    }
    for (index = 0U; index < AI_FEATURE_COUNT; index++)
    {
        if (!isfinite(model->feature_mean[index])
            || !isfinite(model->feature_scale[index])
            || !(model->feature_scale[index] > 0.0F))
        {
            return -RT_ERROR;
        }
    }
    for (index = 0U; index < model->hidden_units; index++)
    {
        if (!isfinite(model->bias1[index]))
        {
            return -RT_ERROR;
        }
    }
    for (index = 0U; index < model->class_count; index++)
    {
        if (!isfinite(model->bias2[index]))
        {
            return -RT_ERROR;
        }
    }
    if (ai_runtime_model_crc32(model) != model->model_crc32)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t ai_runtime_infer(const ai_model_t *model,
                          const float features[AI_FEATURE_COUNT],
                          ai_prediction_t *prediction)
{
    float hidden[AI_MODEL_MAX_HIDDEN_UNITS];
    float probabilities[AI_MODEL_MAX_CLASSES] = {0.0F};
    uint8_t feature_index;
    uint8_t hidden_index;
    uint8_t class_index;
    float probability_sum = 0.0F;
    float max_logit;
    uint8_t best_class = 0U;

    if (prediction == RT_NULL || features == RT_NULL
        || ai_runtime_validate_model(model) != RT_EOK)
    {
        return -RT_ERROR;
    }
    for (feature_index = 0U; feature_index < AI_FEATURE_COUNT; feature_index++)
    {
        if (!isfinite(features[feature_index]))
        {
            return -RT_ERROR;
        }
    }

    for (hidden_index = 0U; hidden_index < model->hidden_units; hidden_index++)
    {
        float sum = model->bias1[hidden_index];
        for (feature_index = 0U; feature_index < AI_FEATURE_COUNT; feature_index++)
        {
            float normalized = (features[feature_index] - model->feature_mean[feature_index])
                             / model->feature_scale[feature_index];
            int8_t input_q = ai_runtime_quantize(normalized,
                                                  model->input_scale,
                                                  model->input_zero_point);
            float input = ai_runtime_dequantize(input_q,
                                                model->input_scale,
                                                model->input_zero_point);
            sum += input
                   * ((float)model->weights1[hidden_index * AI_FEATURE_COUNT
                                              + feature_index]
                      * model->weights1_scale);
        }
        hidden[hidden_index] = sum > 0.0F ? sum : 0.0F;
        hidden[hidden_index] = ai_runtime_dequantize(
            ai_runtime_quantize(hidden[hidden_index], model->hidden_scale,
                                model->hidden_zero_point),
            model->hidden_scale, model->hidden_zero_point);
    }

    for (class_index = 0U; class_index < model->class_count; class_index++)
    {
        float logit = model->bias2[class_index];
        for (hidden_index = 0U; hidden_index < model->hidden_units; hidden_index++)
        {
            logit += hidden[hidden_index]
                     * ((float)model->weights2[hidden_index * model->class_count
                                               + class_index]
                        * model->weights2_scale);
        }
        prediction->logits[class_index] = logit;
        if (class_index == 0U || logit > prediction->logits[best_class])
        {
            best_class = class_index;
        }
    }
    max_logit = prediction->logits[best_class];
    for (class_index = 0U; class_index < model->class_count; class_index++)
    {
        probabilities[class_index] = expf(prediction->logits[class_index] - max_logit);
        probability_sum += probabilities[class_index];
    }
    prediction->class_index = best_class;
    prediction->confidence = probabilities[best_class] / probability_sum;
    for (; class_index < AI_MODEL_MAX_CLASSES; class_index++)
    {
        prediction->logits[class_index] = 0.0F;
    }
    return RT_EOK;
}
