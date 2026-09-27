#include <stdio.h>

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

static int test_arm_rejects_prefilled_result_queue(void)
{
    ai_inference_stats_t stats = {0};
    event_record_t event = {0};

    event.event_id = 1U;
    if (ai_inference_service_start(&transport_ai_model_v1) != RT_EOK
        || ai_inference_service_submit_event(RT_NULL, &event) == RT_EOK)
    {
        fputs("ai queue pressure arm: failed to prefill result queue\n",
              stderr);
        return 1;
    }
    ai_inference_service_get_stats(&stats);
    if (stats.result_queue_depth == 0U
        || ai_inference_service_arm_queue_pressure() != -RT_EBUSY
        || ai_inference_service_queue_pressure_active() != RT_FALSE)
    {
        fputs("ai queue pressure arm: accepted pending result queue\n",
              stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_arm_rejects_prefilled_result_queue() != 0)
    {
        return 1;
    }
    puts("ai inference queue pressure arm: PASS");
    return 0;
}
