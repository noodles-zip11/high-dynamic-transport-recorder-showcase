#include "ai_model_data.h"

static const float transport_ai_model_v1_feature_mean[AI_FEATURE_COUNT] = {13021.4561F, 12067.1865F, 2567.48071F, 1.5F, 4.25320721F, 2326.23633F};
static const float transport_ai_model_v1_feature_scale[AI_FEATURE_COUNT] = {13673.2432F, 14145.2266F, 827.607788F, 1.0F, 3.24380302F, 1977.76721F};
static const int8_t transport_ai_model_v1_weights1[AI_FEATURE_COUNT * 8] = {-15, 38, -23, -4, 38, -85, 42, 20, 22, -11, -6, 64, 19, 66, 127, -27, -35, 36, -46, -100, 3, -8, -60, -61, 36, 24, -45, -14, 52, -53, -21, -53, -24, 3, -50, 90, -62, -42, 15, -12, -47, -87, 16, 31, 81, 17, 4, -21};
static const float transport_ai_model_v1_bias1[8] = {0.457533926F, 0.347345322F, 0.161121875F, -0.0396744832F, 0.42965734F, 0.0565428883F, -0.0879177079F, 0.109742396F};
static const int8_t transport_ai_model_v1_weights2[8 * 4] = {24, 86, -76, -6, -89, -21, 25, 25, -53, -22, -22, 127, 95, -59, 26, -47, -9, 84, -47, -29, -18, -49, 109, 5, 95, -45, -8, -38, -42, -4, -57, 53};
static const float transport_ai_model_v1_bias2[4] = {-0.656756461F, 0.565457582F, 0.382351577F, -0.291052639F};
const ai_model_t transport_ai_model_v1 = {
    .magic = AI_MODEL_MAGIC,
    .version = AI_MODEL_VERSION,
    .feature_count = AI_FEATURE_COUNT,
    .hidden_units = 8U,
    .class_count = 4U,
    .input_scale = 0.0222006124F,
    .input_zero_point = 0.0F,
    .hidden_scale = 0.0357039981F,
    .hidden_zero_point = 0.0F,
    .feature_mean = transport_ai_model_v1_feature_mean,
    .feature_scale = transport_ai_model_v1_feature_scale,
    .weights1 = transport_ai_model_v1_weights1,
    .weights1_scale = 0.00729933009F,
    .bias1 = transport_ai_model_v1_bias1,
    .weights2 = transport_ai_model_v1_weights2,
    .weights2_scale = 0.00769379269F,
    .bias2 = transport_ai_model_v1_bias2,
    .model_crc32 = UINT32_C(0xAD10980E),
};
