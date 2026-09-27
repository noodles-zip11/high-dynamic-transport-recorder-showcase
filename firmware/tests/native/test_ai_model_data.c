#include <stdio.h>

#include "ai_model_data.h"

static int test_pilot_model_validates_and_infers(void)
{
    const float features[AI_FEATURE_COUNT] = {0.0F, 0.0F, 0.0F,
                                              0.0F, 0.0F, 0.0F};
    ai_prediction_t prediction = {0};

    if (ai_runtime_validate_model(&transport_ai_model_v1) != RT_EOK
        || ai_runtime_infer(&transport_ai_model_v1, features, &prediction)
               != RT_EOK
        || prediction.class_index >= transport_ai_model_v1.class_count
        || prediction.confidence < 0.5F
        || prediction.confidence > 1.0F)
    {
        fputs("ai model data: validation or inference failed\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_pilot_model_validates_and_infers() != 0)
    {
        return 1;
    }
    puts("ai model data: PASS");
    return 0;
}
