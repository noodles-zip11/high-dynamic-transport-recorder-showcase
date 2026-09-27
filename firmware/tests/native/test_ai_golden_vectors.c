#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ai_model_data.h"

#define AI_GOLDEN_VECTOR_PATH "ai/tests/fixtures/ai_model_v1_four_class_golden_vectors.csv"

static int near_value(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001F;
}

static int test_shared_golden_vectors(void)
{
    FILE *fixture = fopen(AI_GOLDEN_VECTOR_PATH, "r");
    char line[512];
    unsigned int vector_count = 0U;

    if (fixture == NULL || fgets(line, sizeof(line), fixture) == NULL)
    {
        fputs("ai golden vectors: fixture could not be opened\n", stderr);
        if (fixture != NULL)
        {
            fclose(fixture);
        }
        return 1;
    }
    while (fgets(line, sizeof(line), fixture) != NULL)
    {
        char name[64];
        float features[AI_FEATURE_COUNT];
        float expected_logits[AI_MODEL_MAX_CLASSES] = {0};
        float expected_probabilities[AI_MODEL_MAX_CLASSES] = {0};
        unsigned int expected_class_index;
        float expected_confidence;
        ai_prediction_t prediction = {0};
        float probabilities[AI_MODEL_MAX_CLASSES] = {0};
        float probability_sum = 0.0F;
        float maximum = 0.0F;
        int parsed = 0;

        if (transport_ai_model_v1.class_count == 4U)
        {
            parsed = sscanf(
                line,
                "%63[^,],%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%u,%f",
                name,
                &features[0], &features[1], &features[2],
                &features[3], &features[4], &features[5],
                &expected_logits[0], &expected_logits[1],
                &expected_logits[2], &expected_logits[3],
                &expected_probabilities[0], &expected_probabilities[1],
                &expected_probabilities[2], &expected_probabilities[3],
                &expected_class_index, &expected_confidence);
        }
        else if (transport_ai_model_v1.class_count == 2U)
        {
            parsed = sscanf(
                line,
                "%63[^,],%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%u,%f",
                name,
                &features[0], &features[1], &features[2],
                &features[3], &features[4], &features[5],
                &expected_logits[0], &expected_logits[1],
                &expected_probabilities[0], &expected_probabilities[1],
                &expected_class_index, &expected_confidence);
        }
        if ((transport_ai_model_v1.class_count == 4U && parsed != 17)
            || (transport_ai_model_v1.class_count == 2U && parsed != 13)
            || ai_runtime_infer(&transport_ai_model_v1, features, &prediction)
                   != RT_EOK)
        {
            fputs("ai golden vectors: malformed or rejected vector\n", stderr);
            fclose(fixture);
            return 1;
        }
        maximum = prediction.logits[0];
        for (unsigned int class_index = 1U;
             class_index < transport_ai_model_v1.class_count; class_index++)
        {
            if (prediction.logits[class_index] > maximum)
            {
                maximum = prediction.logits[class_index];
            }
        }
        for (unsigned int class_index = 0U;
             class_index < transport_ai_model_v1.class_count; class_index++)
        {
            probabilities[class_index] =
                expf(prediction.logits[class_index] - maximum);
            probability_sum += probabilities[class_index];
        }
        for (unsigned int class_index = 0U;
             class_index < transport_ai_model_v1.class_count; class_index++)
        {
            probabilities[class_index] /= probability_sum;
        }
        if (prediction.class_index != expected_class_index
            || !near_value(prediction.confidence, expected_confidence))
        {
            fprintf(stderr, "ai golden vectors: mismatch in %s\n", name);
            fclose(fixture);
            return 1;
        }
        for (unsigned int class_index = 0U;
             class_index < transport_ai_model_v1.class_count; class_index++)
        {
            if (!near_value(prediction.logits[class_index],
                            expected_logits[class_index])
                || !near_value(probabilities[class_index],
                               expected_probabilities[class_index]))
            {
                fprintf(stderr, "ai golden vectors: mismatch in %s\n", name);
                fclose(fixture);
                return 1;
            }
        }
        vector_count++;
    }
    fclose(fixture);
    if (vector_count == 0U)
    {
        fputs("ai golden vectors: fixture has no vectors\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_shared_golden_vectors() != 0)
    {
        return 1;
    }
    puts("ai golden vectors: PASS");
    return 0;
}
