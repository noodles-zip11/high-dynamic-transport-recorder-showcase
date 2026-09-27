#include <stdio.h>
#include <string.h>

#include "ai_model_data.h"
#include "ai_inference_service.h"
#include "power_runtime.h"

static uint32_t observed_ai_stack_size;
static uint32_t test_ai_power_blocker_hold_count;

void native_ai_thread_create_observer(const char *thread_name,
                                      uint32_t requested_stack_size,
                                      uint8_t requested_priority,
                                      uint32_t requested_tick)
{
    (void)requested_priority;
    (void)requested_tick;
    if (thread_name != RT_NULL && strcmp(thread_name, "ai") == 0)
    {
        observed_ai_stack_size = requested_stack_size;
    }
}

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    if (blocker == POWER_BLOCKER_AI)
    {
        ++test_ai_power_blocker_hold_count;
    }
    return RT_EOK;
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    if (blocker == POWER_BLOCKER_AI && test_ai_power_blocker_hold_count != 0U)
    {
        --test_ai_power_blocker_hold_count;
    }
}

static int test_uninstalled_model_queues_failure_and_holds_blocker(void)
{
    ai_inference_stats_t stats = {0};
    event_record_t event = {0};

    event.event_id = 1U;

    observed_ai_stack_size = 0U;
    if (ai_inference_service_start(RT_NULL) != RT_EOK
        || ai_inference_service_submit_event(RT_NULL, &event) == RT_EOK)
    {
        fputs("ai service: uninstalled model fallback submission failed\n", stderr);
        return 1;
    }
    if (test_ai_power_blocker_hold_count == 0U)
    {
        fputs("ai service: queued failure did not hold the AI blocker\n", stderr);
        return 1;
    }
    if (observed_ai_stack_size < 2560U)
    {
        fputs("ai service: worker stack request is below 2560 bytes\n", stderr);
        return 1;
    }
    ai_inference_service_get_stats(&stats);
    if (stats.model_ready != RT_FALSE || stats.submitted_count != 0U
        || stats.queue_drop_count != 0U)
    {
        fputs("ai service: fallback state changed queue counters\n", stderr);
        return 1;
    }
    return 0;
}

static int test_completed_event_is_retained_and_queued(void)
{
    enum { TEST_BLOCK_COUNT = (AI_EVENT_SAMPLE_COUNT +
                               SAMPLE_BLOCK_SAMPLE_CAPACITY - 1U) /
                              SAMPLE_BLOCK_SAMPLE_CAPACITY };
    sample_block_pool_t pool;
    event_record_t event = {0};
    ai_inference_stats_t stats = {0};
    uint8_t block_index;

    sample_block_pool_init(&pool);
    event.event_id = 42U;
    event.block_count = TEST_BLOCK_COUNT;
    for (block_index = 0U; block_index < TEST_BLOCK_COUNT; block_index++)
    {
        uint16_t remaining = (uint16_t)(AI_EVENT_SAMPLE_COUNT
                                        - block_index * SAMPLE_BLOCK_SAMPLE_CAPACITY);
        pool.blocks[block_index].state = SAMPLE_BLOCK_CONSUMING;
        pool.blocks[block_index].ref_count = 1U;
        pool.blocks[block_index].sample_count = remaining > SAMPLE_BLOCK_SAMPLE_CAPACITY
                                                  ? SAMPLE_BLOCK_SAMPLE_CAPACITY
                                                  : remaining;
        event.blocks[block_index] = &pool.blocks[block_index];
    }

    if (ai_inference_service_start(&transport_ai_model_v1) != RT_EOK
        || ai_inference_service_submit_event(&pool, &event) != RT_EOK)
    {
        fputs("ai service: completed event was not queued\n", stderr);
        return 1;
    }
    ai_inference_service_get_stats(&stats);
    if (stats.model_ready != RT_TRUE || stats.submitted_count != 1U
        || stats.processed_count != 0U || stats.feature_error_count != 0U
        || pool.blocks[0].ref_count != 2U)
    {
        fputs("ai service: queued event state is invalid\n", stderr);
        return 1;
    }
    return 0;
}

static void prepare_complete_event(sample_block_pool_t *pool,
                                   event_record_t *event,
                                   uint32_t event_id)
{
    enum { TEST_BLOCK_COUNT = (AI_EVENT_SAMPLE_COUNT +
                               SAMPLE_BLOCK_SAMPLE_CAPACITY - 1U) /
                              SAMPLE_BLOCK_SAMPLE_CAPACITY };
    uint8_t block_index;

    sample_block_pool_init(pool);
    event->event_id = event_id;
    event->block_count = TEST_BLOCK_COUNT;
    for (block_index = 0U; block_index < TEST_BLOCK_COUNT; block_index++)
    {
        uint16_t remaining = (uint16_t)(AI_EVENT_SAMPLE_COUNT
                                        - block_index * SAMPLE_BLOCK_SAMPLE_CAPACITY);
        pool->blocks[block_index].state = SAMPLE_BLOCK_CONSUMING;
        pool->blocks[block_index].ref_count = 1U;
        pool->blocks[block_index].sample_count = remaining > SAMPLE_BLOCK_SAMPLE_CAPACITY
                                                  ? SAMPLE_BLOCK_SAMPLE_CAPACITY
                                                  : remaining;
        event->blocks[block_index] = &pool->blocks[block_index];
    }
}

static int test_resource_drop_is_addressable(void)
{
    sample_block_pool_t first_pool;
    sample_block_pool_t dropped_pool;
    event_record_t first_event = {0};
    event_record_t dropped_event = {0};
    ai_result_t result = {0};

    prepare_complete_event(&first_pool, &first_event, 43U);
    prepare_complete_event(&dropped_pool, &dropped_event, 44U);
    if (ai_inference_service_submit_event(&first_pool, &first_event) != RT_EOK
        || ai_inference_service_submit_event(&dropped_pool, &dropped_event) == RT_EOK
        || ai_inference_service_get_result(dropped_event.event_id, &result) != RT_EOK
        || result.status != AI_RESULT_STATUS_RESOURCE_LIMIT
        || result.failure_reason != AI_RESULT_FAILURE_RESOURCE_LIMIT
        || (result.quality_flags & AI_RESULT_QUALITY_RESOURCE_LIMIT) == 0U)
    {
        fputs("ai service: resource failure result is not addressable\n", stderr);
        return 1;
    }
    return 0;
}

static int test_model_failure_is_addressable_by_event_id(void)
{
    ai_result_t result = {0};
    ai_result_t queued_failure = {0};
    event_record_t event = {0};
    event_record_t later_event = {0};

    event.event_id = 77U;
    event.flags = EVENT_FLAG_DATA_LOSS;
    if (ai_inference_service_submit_event(RT_NULL, &event) == RT_EOK
        || ai_inference_service_get_result(event.event_id, &result) != RT_EOK
        || result.event_id != event.event_id
        || result.status != AI_RESULT_STATUS_MODEL_UNAVAILABLE
        || result.failure_reason != AI_RESULT_FAILURE_NO_MODEL
        || result.result_sequence != 2U
        || (result.quality_flags & AI_RESULT_QUALITY_DATA_LOSS) == 0U)
    {
        fputs("ai service: model failure result is not addressable\n", stderr);
        return 1;
    }
    later_event.event_id = 78U;
    if (ai_inference_service_submit_event(RT_NULL, &later_event) == RT_EOK
        || ai_inference_service_get_result(event.event_id, &queued_failure)
           != RT_EOK
        || queued_failure.event_id != event.event_id
        || queued_failure.result_sequence != result.result_sequence)
    {
        fputs("ai service: queued failure was hidden by a later result\n", stderr);
        return 1;
    }
    return 0;
}

static int test_result_queue_backpressure_is_explicit(void)
{
    ai_inference_stats_t stats = {0};
    ai_inference_stats_t after = {0};
    ai_result_t latest = {0};
    event_record_t event = {0};
    uint32_t next_event_id = 200U;
    uint32_t overflow_event_id;

    ai_inference_service_get_stats(&stats);
    while (stats.result_queue_depth < AI_RESULT_PERSIST_FAILURE_CAPACITY)
    {
        event.event_id = next_event_id++;
        if (ai_inference_service_submit_event(RT_NULL, &event) == RT_EOK)
        {
            fputs("ai service: failure queue unexpectedly accepted without a pool\n",
                  stderr);
            return 1;
        }
        ai_inference_service_get_stats(&stats);
    }
    overflow_event_id = next_event_id;
    event.event_id = overflow_event_id;
    if (ai_inference_service_submit_event(RT_NULL, &event) == RT_EOK
        || ai_inference_service_get_result(overflow_event_id, &latest)
           != AI_INFERENCE_RESULT_NOT_FOUND
        || ai_inference_service_get_result(0U, &latest) != RT_EOK
        || latest.event_id == overflow_event_id)
    {
        fputs("ai service: full result queue did not apply backpressure\n",
              stderr);
        return 1;
    }
    ai_inference_service_get_stats(&after);
    if (after.result_queue_depth != AI_RESULT_PERSIST_FAILURE_CAPACITY
        || after.result_queue_high_watermark
           < AI_RESULT_PERSIST_FAILURE_CAPACITY
        || after.result_queue_backpressure_count
           != stats.result_queue_backpressure_count + 1U
        || after.result_queue_drop_count != stats.result_queue_drop_count + 1U)
    {
        fputs("ai service: result queue backpressure counters are invalid\n",
              stderr);
        return 1;
    }
    if (test_ai_power_blocker_hold_count == 0U)
    {
        fputs("ai service: queued persistence retry released the AI blocker\n",
              stderr);
        return 1;
    }
    return 0;
}

static int test_runtime_model_switch_is_validated(void)
{
    ai_inference_stats_t stats = {0};

    if (ai_inference_service_set_model(RT_NULL) == RT_EOK
        || ai_inference_service_prepare_model(&transport_ai_model_v1)
               != RT_EOK)
    {
        fputs("ai service: runtime model switch validation failed\n", stderr);
        return 1;
    }
    if (ai_inference_service_set_model(&transport_ai_model_v1) == RT_EOK)
    {
        ai_inference_service_abort_model();
        fputs("ai service: prepared transition was not exclusive\n", stderr);
        return 1;
    }
    ai_inference_service_abort_model();
    if (ai_inference_service_set_model(&transport_ai_model_v1) != RT_EOK)
    {
        fputs("ai service: runtime model switch did not recover after abort\n",
              stderr);
        return 1;
    }
    ai_inference_service_get_stats(&stats);
    if (stats.model_ready != RT_TRUE)
    {
        fputs("ai service: runtime model switch did not become ready\n", stderr);
        return 1;
    }
    ai_inference_service_quarantine_model();
    ai_inference_service_get_stats(&stats);
    if (stats.model_ready != RT_FALSE
        || ai_inference_service_set_model(&transport_ai_model_v1) == RT_EOK
        || ai_inference_service_publish_model(&transport_ai_model_v1) == RT_EOK
        || ai_inference_service_start(&transport_ai_model_v1) == RT_EOK)
    {
        fputs("ai service: quarantine latch did not hold\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_uninstalled_model_queues_failure_and_holds_blocker() != 0
        || test_model_failure_is_addressable_by_event_id() != 0
        || test_completed_event_is_retained_and_queued() != 0
        || test_resource_drop_is_addressable() != 0
        || test_result_queue_backpressure_is_explicit() != 0
        || test_runtime_model_switch_is_validated() != 0)
    {
        return 1;
    }
    puts("ai inference service: PASS");
    return 0;
}
