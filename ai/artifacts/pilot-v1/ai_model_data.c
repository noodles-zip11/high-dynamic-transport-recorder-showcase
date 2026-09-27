#include "ai_model_data.h"

static const float transport_ai_model_v1_feature_mean[AI_FEATURE_COUNT] = {7398.30566F, 5919.57861F, 2104.78027F, 1.5F, 3.43874097F, 1702.76404F};
static const float transport_ai_model_v1_feature_scale[AI_FEATURE_COUNT] = {6406.55859F, 6571.03418F, 61.4663658F, 1.0F, 2.82456422F, 1360.44397F};
static const int8_t transport_ai_model_v1_weights1[AI_FEATURE_COUNT * 8] = {-60, -94, -10, -2, -70, -112, 31, 14, 6, 9, 22, 115, 95, 68, 28, -24, 2, 25, -25, -56, -53, 39, -127, -111, -21, 28, -22, -34, -3, -8, 27, 17, 0, -17, -41, -9, -17, -11, -39, 9, -12, -71, -32, -42, -19, 16, 7, -24};
static const float transport_ai_model_v1_bias1[8] = {0.240387708F, 0.521083295F, 0.298832089F, 0.22861056F, -0.017469842F, -0.0564903654F, 0.0832552314F, 0.0400326625F};
static const int8_t transport_ai_model_v1_weights2[8 * 2] = {111, -91, -55, 121, -48, 89, 82, -127, 34, 17, 15, -33, 33, -43, 48, -7};
static const float transport_ai_model_v1_bias2[2] = {-0.449899703F, 0.449899703F};
const ai_model_t transport_ai_model_v1 = {
    .magic = AI_MODEL_MAGIC,
    .version = AI_MODEL_VERSION,
    .feature_count = AI_FEATURE_COUNT,
    .hidden_units = 8U,
    .class_count = 2U,
    .input_scale = 0.0321918242F,
    .input_zero_point = 0.0F,
    .hidden_scale = 0.0270916279F,
    .hidden_zero_point = 0.0F,
    .feature_mean = transport_ai_model_v1_feature_mean,
    .feature_scale = transport_ai_model_v1_feature_scale,
    .weights1 = transport_ai_model_v1_weights1,
    .weights1_scale = 0.00522423768F,
    .bias1 = transport_ai_model_v1_bias1,
    .weights2 = transport_ai_model_v1_weights2,
    .weights2_scale = 0.00647856947F,
    .bias2 = transport_ai_model_v1_bias2,
    .model_crc32 = UINT32_C(0xA815E0C1),
};
