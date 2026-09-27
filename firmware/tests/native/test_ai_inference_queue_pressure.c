#include <stdio.h>
#include <string.h>

#include "ai_inference_service.h"
#include "ai_model_data.h"
#include "power_runtime.h"

static uint32_t test_ai_power_blocker_hold_count;

void native_ai_thread_create_observer(const char *thread_name,
                                      uint32_t requested_stack_size,
                                      uint8_t requested_priority,
                                      uint32_t requested_tick)
{
    (void)thread_name;
    (void)requested_stack_size;
    (void)requested_priority;
    (void)requested_tick;
}

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    if (blocker == POWER_BLOCKER_AI)
    {
        test_ai_power_blocker_hold_count++;
    }
    return RT_EOK;
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    if (blocker == POWER_BLOCKER_AI
        && test_ai_power_blocker_hold_count != 0U)
    {
        test_ai_power_blocker_hold_count--;
    }
}

static int prepare_shared_complete_event(sample_block_pool_t *pool,
                                         event_record_t *event,
                                         uint32_t event_id)
{
    enum { TEST_BLOCK_COUNT = EVENT_PRETRIGGER_BLOCK_COUNT
                                  + EVENT_STANDARD_POST_BLOCK_COUNT };
    uint8_t block_index;

    if (pool == RT_NULL || event == RT_NULL)
    {
        return 0;
    }
    memset(event, 0, sizeof(*event));
    event->event_id = event_id;
    event->block_count = TEST_BLOCK_COUNT;
    event->pretrigger_block_count = EVENT_PRETRIGGER_BLOCK_COUNT;
    event->posttrigger_block_count = EVENT_STANDARD_POST_BLOCK_COUNT;
    for (block_index = 0U; block_index < TEST_BLOCK_COUNT; block_index++)
    {
        sample_block_t *block = sample_block_pool_acquire(pool);

        if (block == RT_NULL)
        {
            return 0;
        }
        block->sequence = (event_id * TEST_BLOCK_COUNT + block_index)
                          * SAMPLE_BLOCK_SAMPLE_CAPACITY;
        block->first_monotonic_us = (uint64_t)block->sequence * 625U;
        block->sample_period_ns = 625000U;
        block->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY / 2U;
        if (sample_block_pool_publish(pool, block) != RT_EOK)
        {
            (void)sample_block_pool_abandon(pool, block);
            return 0;
        }
        event->blocks[block_index] = sample_block_pool_take_ready(pool);
        if (event->blocks[block_index] != block)
        {
            if (event->blocks[block_index] != RT_NULL)
            {
                (void)sample_block_pool_release(pool,
                                                event->blocks[block_index]);
            }
            return 0;
        }
    }
    return 1;
}

static void release_event_blocks(sample_block_pool_t *pool,
                                 const event_record_t *event)
{
    uint8_t block_index;

    if (pool == RT_NULL || event == RT_NULL)
    {
        return;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        (void)sample_block_pool_release(pool, event->blocks[block_index]);
    }
}

static int test_cancel_releases_queue_pressure_gate(void)
{
    if (ai_inference_service_start(&transport_ai_model_v1) != RT_EOK
        || ai_inference_service_arm_queue_pressure() != RT_EOK
        || ai_inference_service_queue_pressure_active() != RT_TRUE)
    {
        fputs("ai queue pressure: arm failed\n", stderr);
        return 1;
    }
    ai_inference_service_cancel_queue_pressure();
    if (ai_inference_service_queue_pressure_active() != RT_FALSE)
    {
        fputs("ai queue pressure: cancel left the gate active\n", stderr);
        return 1;
    }
    return 0;
}

static int test_real_queue_pressure_rejects_only_the_third_event(void)
{
    sample_block_pool_t pool;
    event_record_t events[AI_INFERENCE_QUEUE_CAPACITY + 1U] = {0};
    ai_inference_stats_t before = {0};
    ai_inference_stats_t after = {0};
    ai_result_t result = {0};
    uint32_t index;

    sample_block_pool_init(&pool);
    for (index = 0U; index < AI_INFERENCE_QUEUE_CAPACITY; index++)
    {
        if (!prepare_shared_complete_event(&pool, &events[index], index + 1U))
        {
            fputs("ai queue pressure: shared pool event setup failed\n",
                  stderr);
            return 1;
        }
    }
    ai_inference_service_get_stats(&before);
    if (ai_inference_service_arm_queue_pressure() != RT_EOK)
    {
        fputs("ai queue pressure: real queue arm failed\n", stderr);
        return 1;
    }
    for (index = 0U; index < AI_INFERENCE_QUEUE_CAPACITY; index++)
    {
        if (ai_inference_service_submit_event(&pool, &events[index]) != RT_EOK)
        {
            fputs("ai queue pressure: complete event was not retained\n",
                  stderr);
            return 1;
        }
        release_event_blocks(&pool, &events[index]);
    }
    if (sample_block_pool_free_count(&pool) != SAMPLE_BLOCK_POOL_SIZE)
    {
        fputs("ai queue pressure: queued compact events held sample blocks\n",
              stderr);
        return 1;
    }
    if (!prepare_shared_complete_event(&pool, &events[AI_INFERENCE_QUEUE_CAPACITY],
                                       AI_INFERENCE_QUEUE_CAPACITY + 1U))
    {
        fputs("ai queue pressure: third event could not use shared pool\n",
              stderr);
        return 1;
    }
    if (ai_inference_service_test_worker_step() != RT_EOK
        || ai_inference_service_test_worker_step() != RT_EOK)
    {
        fputs("ai queue pressure: worker dequeued while gate was active\n",
              stderr);
        return 1;
    }
    if (ai_inference_service_submit_event(
            &pool,
            &events[AI_INFERENCE_QUEUE_CAPACITY]) == RT_EOK
        || ai_inference_service_queue_pressure_active() != RT_FALSE
        || ai_inference_service_get_result(
               events[AI_INFERENCE_QUEUE_CAPACITY].event_id, &result)
               != RT_EOK
        || result.status != AI_RESULT_STATUS_RESOURCE_LIMIT
        || result.failure_reason != AI_RESULT_FAILURE_RESOURCE_LIMIT
        || (result.quality_flags & AI_RESULT_QUALITY_RESOURCE_LIMIT) == 0U)
    {
        fputs("ai queue pressure: third event was not a real resource reject\n",
              stderr);
        return 1;
    }
    release_event_blocks(&pool, &events[AI_INFERENCE_QUEUE_CAPACITY]);
    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        if (pool.blocks[index].state == SAMPLE_BLOCK_FREE)
        {
            pool.blocks[index].sample_count = 0U;
        }
    }
    if (ai_inference_service_test_worker_step() != RT_EOK
        || ai_inference_service_test_worker_step() != RT_EOK
        || ai_inference_service_test_worker_step() != RT_EOK
        || sample_block_pool_free_count(&pool) != SAMPLE_BLOCK_POOL_SIZE)
    {
        fputs("ai queue pressure: worker did not drain after gate release\n",
              stderr);
        return 1;
    }
    if (ai_inference_service_get_result(2U, &result) != RT_EOK
        || result.sample_count != AI_EVENT_SAMPLE_COUNT)
    {
        fputs("ai queue pressure: compact result read reused block metadata\n",
              stderr);
        return 1;
    }
    ai_inference_service_get_stats(&after);
    if (after.submitted_count != before.submitted_count
                                      + AI_INFERENCE_QUEUE_CAPACITY
        || after.processed_count != before.processed_count
                                  + AI_INFERENCE_QUEUE_CAPACITY
        || after.queue_drop_count != before.queue_drop_count + 1U
        || sample_block_pool_free_count(&pool) != SAMPLE_BLOCK_POOL_SIZE)
    {
        fputs("ai queue pressure: admission or retention accounting is wrong\n",
              stderr);
        return 1;
    }
    ai_inference_service_cancel_queue_pressure();
    return 0;
}

int main(void)
{
    if (test_cancel_releases_queue_pressure_gate() != 0
        || test_real_queue_pressure_rejects_only_the_third_event() != 0)
    {
        return 1;
    }
    puts("ai inference queue pressure: PASS");
    return 0;
}
