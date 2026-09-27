#include <math.h>
#include <stdio.h>
#include <string.h>

#define AI_TEST_HIDDEN_UNITS 2U
#define AI_TEST_CLASS_COUNT 2U

#include "ai_runtime.h"
#include "ota_crc32.h"

static const float feature_mean[AI_FEATURE_COUNT] = {0};
static const float feature_scale[AI_FEATURE_COUNT] = {1, 1, 1, 1, 1, 1};
static const int8_t weights1[AI_TEST_HIDDEN_UNITS * AI_FEATURE_COUNT] = {
    1, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0,
};
static const float bias1[AI_TEST_HIDDEN_UNITS] = {0, 0};
static const int8_t weights2[AI_TEST_HIDDEN_UNITS * AI_TEST_CLASS_COUNT] = {
    1, -1,
    -1, 1,
};
static const float bias2[AI_TEST_CLASS_COUNT] = {0, 0};

static ai_model_t make_model(void)
{
    ai_model_t model = {
        .magic = AI_MODEL_MAGIC,
        .version = AI_MODEL_VERSION,
        .feature_count = AI_FEATURE_COUNT,
        .hidden_units = AI_TEST_HIDDEN_UNITS,
        .class_count = AI_TEST_CLASS_COUNT,
        .input_scale = 1.0F,
        .input_zero_point = 0.0F,
        .hidden_scale = 1.0F,
        .hidden_zero_point = 0.0F,
        .feature_mean = feature_mean,
        .feature_scale = feature_scale,
        .weights1 = weights1,
        .weights1_scale = 1.0F,
        .bias1 = bias1,
        .weights2 = weights2,
        .weights2_scale = 1.0F,
        .bias2 = bias2,
        .model_crc32 = 0U,
    };
    model.model_crc32 = ai_runtime_model_crc32(&model);
    return model;
}

static int test_model_validates_and_predicts_deterministically(void)
{
    ai_model_t model = make_model();
    ai_prediction_t first = {0};
    ai_prediction_t second = {0};
    const float features[AI_FEATURE_COUNT] = {2, 0, 0, 0, 0, 0};

    if (ai_runtime_validate_model(&model) != RT_EOK
        || ai_runtime_infer(&model, features, &first) != RT_EOK
        || ai_runtime_infer(&model, features, &second) != RT_EOK
        || first.class_index != 0U
        || first.class_index != second.class_index
        || fabsf(first.confidence - second.confidence) > 0.00001F
        || first.confidence <= 0.5F)
    {
        fputs("ai runtime: deterministic prediction failed\n", stderr);
        return 1;
    }
    return 0;
}

static int test_model_rejects_version_and_crc_mismatch(void)
{
    ai_model_t model = make_model();

    model.version++;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: unsupported model version accepted\n", stderr);
        return 1;
    }
    model = make_model();
    model.model_crc32++;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: bad model CRC accepted\n", stderr);
        return 1;
    }
    return 0;
}

static int test_model_rejects_mutation_of_inference_data(void)
{
    ai_model_t model = make_model();
    float changed_feature_mean[AI_FEATURE_COUNT];
    float changed_bias1[AI_TEST_HIDDEN_UNITS];
    float changed_bias2[AI_TEST_CLASS_COUNT];
    int8_t changed_weights1[AI_TEST_HIDDEN_UNITS * AI_FEATURE_COUNT];
    int8_t changed_weights2[AI_TEST_HIDDEN_UNITS * AI_TEST_CLASS_COUNT];

    memcpy(changed_feature_mean, feature_mean, sizeof(changed_feature_mean));
    changed_feature_mean[0] = 1.0F;
    model.feature_mean = changed_feature_mean;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed feature mean accepted\n", stderr);
        return 1;
    }

    model = make_model();
    memcpy(changed_weights1, weights1, sizeof(changed_weights1));
    changed_weights1[0]++;
    model.weights1 = changed_weights1;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed first-layer weight accepted\n", stderr);
        return 1;
    }

    model = make_model();
    memcpy(changed_bias1, bias1, sizeof(changed_bias1));
    changed_bias1[0] = 1.0F;
    model.bias1 = changed_bias1;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed hidden bias accepted\n", stderr);
        return 1;
    }

    model = make_model();
    memcpy(changed_weights2, weights2, sizeof(changed_weights2));
    changed_weights2[0]++;
    model.weights2 = changed_weights2;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed output weight accepted\n", stderr);
        return 1;
    }

    model = make_model();
    memcpy(changed_bias2, bias2, sizeof(changed_bias2));
    changed_bias2[0] = 1.0F;
    model.bias2 = changed_bias2;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed output bias accepted\n", stderr);
        return 1;
    }

    model = make_model();
    model.input_scale = 2.0F;
    if (ai_runtime_validate_model(&model) == RT_EOK)
    {
        fputs("ai runtime: changed input scale accepted\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_model_validates_and_predicts_deterministically() != 0
        || test_model_rejects_version_and_crc_mismatch() != 0
        || test_model_rejects_mutation_of_inference_data() != 0)
    {
        return 1;
    }
    puts("ai runtime: PASS");
    return 0;
}
