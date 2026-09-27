#include "event_service.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "event_export_debug.h"
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
#include "ai_inference_service.h"
#endif
#include "health_service.h"
#include "imu_acquisition.h"
#include "imu_sample_batcher.h"
#include "power_runtime.h"
#include "time/monotonic_clock.h"

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#define EVENT_SERVICE_THREAD_STACK_SIZE 4096U
#else
#define EVENT_SERVICE_THREAD_STACK_SIZE 2048U
#endif
#define EVENT_SERVICE_THREAD_PRIORITY 9U
#define EVENT_SERVICE_THREAD_TICK 10U
#define EVENT_IMPACT_TRIGGER_THRESHOLD_COUNTS 5120U
#define EVENT_IMPACT_TRIGGER_THRESHOLD_MAGNITUDE_SQ \
    (EVENT_IMPACT_TRIGGER_THRESHOLD_COUNTS \
     * EVENT_IMPACT_TRIGGER_THRESHOLD_COUNTS)
#define EVENT_IMPACT_TRIGGER_CONSECUTIVE_SAMPLES 2U
#define EVENT_DROP_TRIGGER_THRESHOLD_COUNTS 1536U
#define EVENT_DROP_TRIGGER_THRESHOLD_MAGNITUDE_SQ \
    (EVENT_DROP_TRIGGER_THRESHOLD_COUNTS \
     * EVENT_DROP_TRIGGER_THRESHOLD_COUNTS)
#define EVENT_DROP_TRIGGER_CONSECUTIVE_SAMPLES 8U
#define EVENT_NATURAL_TRIGGER_COOLDOWN_US UINT64_C(30000000)
#define EVENT_FIXED_SAMPLE_COUNT 2400U

static event_assembler_t event_assembler;
static trigger_detector_t event_impact_detector;
static trigger_detector_t event_drop_detector;
static event_export_sink_t event_sink;
static struct rt_mutex event_service_mutex;
static rt_bool_t event_service_started;
static rt_bool_t event_service_mutex_initialized;
static rt_bool_t event_sink_configured;
static volatile rt_bool_t event_test_trigger_pending;
static volatile uint64_t event_test_trigger_deadline_us;
static uint32_t event_export_error_count;
static uint32_t event_processed_block_count;
static rt_bool_t event_natural_trigger_latched;
static rt_bool_t event_cooldown_rejection_latched;
static uint64_t event_natural_cooldown_deadline_us;
static uint32_t event_natural_accepted_count;
static uint32_t event_cooldown_rejection_count;
static const health_service_t *event_health_service;
static rt_bool_t event_power_activation_pending;
static rt_bool_t event_power_active_owned;

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static uint32_t event_fault_injection_case_pending;
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
static rt_bool_t event_queue_pressure_active;
static uint32_t event_queue_pressure_submitted_count;
#define EVENT_QUEUE_PRESSURE_TARGET_EVENTS \
    (AI_INFERENCE_QUEUE_CAPACITY + 1U)
#endif
#define EVENT_POOL_PRESSURE_RESERVED_CAPACITY \
    (SAMPLE_BLOCK_POOL_SIZE - SAMPLE_BLOCK_HANDOFF_COUNT)
static sample_block_t *event_pool_pressure_reserved[
    EVENT_POOL_PRESSURE_RESERVED_CAPACITY];
static uint16_t event_pool_pressure_reserved_count;
#endif

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static void event_service_cancel_queue_pressure_locked(void);
#endif

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
typedef enum
{
    EVENT_SERVICE_STORAGE_NONE = 0,
    EVENT_SERVICE_STORAGE_COMMITTED,
    EVENT_SERVICE_STORAGE_FAILURE,
} event_service_storage_outcome_t;

static event_quality_result_t event_last_quality;
static rt_bool_t event_last_quality_valid;
static event_service_storage_outcome_t event_last_storage_outcome;
#endif

static void event_service_publish_power_state(event_state_t state_before,
                                              event_state_t state_after)
{
    power_policy_snapshot_t snapshot;

    if (state_before == EVENT_ARMED && state_after != EVENT_ARMED)
    {
        event_power_activation_pending = RT_TRUE;
    }

    if (state_after != EVENT_ARMED && event_power_activation_pending)
    {
        power_runtime_get_snapshot(&snapshot);
        if (snapshot.mode != POWER_MODE_MAINTENANCE
            && snapshot.mode != POWER_MODE_FAULT_FALLBACK
            && power_runtime_event_active(RT_TRUE) == RT_EOK)
        {
            event_power_activation_pending = RT_FALSE;
            event_power_active_owned = RT_TRUE;
        }
    }
    else if (state_before != EVENT_ARMED && state_after == EVENT_ARMED)
    {
        event_power_activation_pending = RT_FALSE;
        power_runtime_get_snapshot(&snapshot);
        if (event_power_active_owned
            || (snapshot.blockers & POWER_BLOCKER_EVENT) != 0U)
        {
            (void)power_runtime_event_active(RT_FALSE);
        }
        event_power_active_owned = RT_FALSE;
    }
}

static rt_err_t event_service_lock(void)
{
    if (!event_service_mutex_initialized
        || rt_mutex_take(&event_service_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void event_service_unlock(void)
{
    if (event_service_mutex_initialized)
    {
        (void)rt_mutex_release(&event_service_mutex);
    }
}

static sample_block_pool_t *event_service_sample_pool(void)
{
    return imu_acquisition_sample_pool();
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static void event_service_cancel_queue_pressure_locked(void)
{
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    event_queue_pressure_active = RT_FALSE;
    event_queue_pressure_submitted_count = 0U;
    ai_inference_service_cancel_queue_pressure();
#endif
}

static rt_bool_t event_service_queue_pressure_is_active_locked(void)
{
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    if (event_queue_pressure_active
        && ai_inference_service_queue_pressure_active() != RT_TRUE)
    {
        event_service_cancel_queue_pressure_locked();
    }
    return event_queue_pressure_active;
#else
    return RT_FALSE;
#endif
}

static void event_service_abandon_pool_pressure_locked(void)
{
    sample_block_pool_t *pool = event_service_sample_pool();
    uint16_t index;

    for (index = 0U; index < event_pool_pressure_reserved_count; index++)
    {
        if (pool != RT_NULL && event_pool_pressure_reserved[index] != RT_NULL)
        {
            (void)sample_block_pool_abandon(
                pool, event_pool_pressure_reserved[index]);
        }
        event_pool_pressure_reserved[index] = RT_NULL;
    }
    event_pool_pressure_reserved_count = 0U;
}

static rt_err_t event_service_reserve_pool_pressure_locked(
    uint16_t target_free_count)
{
    sample_block_pool_t *pool = event_service_sample_pool();
    uint16_t free_count;
    uint16_t reserve_count;
    uint16_t index;

    if (pool == RT_NULL || event_pool_pressure_reserved_count != 0U
        || target_free_count >= SAMPLE_BLOCK_POOL_SIZE)
    {
        return -RT_ERROR;
    }
    free_count = sample_block_pool_free_count(pool);
    if (free_count <= target_free_count)
    {
        return RT_EOK;
    }
    if ((uint32_t)(free_count - target_free_count)
               > (uint32_t)EVENT_POOL_PRESSURE_RESERVED_CAPACITY)
    {
        return -RT_ERROR;
    }
    reserve_count = (uint16_t)(free_count - target_free_count);
    for (index = 0U; index < reserve_count; index++)
    {
        sample_block_t *block = sample_block_pool_acquire(pool);

        if (block == RT_NULL)
        {
            event_service_abandon_pool_pressure_locked();
            return -RT_ERROR;
        }
        event_pool_pressure_reserved[
            event_pool_pressure_reserved_count++] = block;
    }
    if (sample_block_pool_free_count(pool) > target_free_count)
    {
        event_service_abandon_pool_pressure_locked();
        return -RT_ERROR;
    }
    return RT_EOK;
}
#endif

static rt_bool_t event_service_sink_is_ready(void)
{
    return event_sink_configured && event_sink.is_ready != RT_NULL
           && event_sink.is_ready(event_sink.context);
}

static uint32_t event_service_sample_sequence(const sample_block_t *block,
                                              uint16_t sample_index)
{
    return block->sequence + sample_index;
}

static rt_err_t event_service_ev01_length(uint32_t *length)
{
    const event_record_t *event = event_assembler_event(&event_assembler);
    uint8_t block_index;
    uint32_t total = EVENT_EXPORT_HEADER_SIZE;

    if (event == RT_NULL || length == RT_NULL)
    {
        return -RT_ERROR;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        if (event->blocks[block_index] == RT_NULL
            || event->blocks[block_index]->sample_count
               > (UINT32_MAX - total) / EVENT_EXPORT_SAMPLE_SIZE)
        {
            return -RT_ERROR;
        }
        total += event->blocks[block_index]->sample_count * EVENT_EXPORT_SAMPLE_SIZE;
    }
    *length = total;
    return RT_EOK;
}

#ifndef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static rt_bool_t event_service_event_has_fixed_sample_count_locked(void)
{
    const event_record_t *event = event_assembler_event(&event_assembler);
    uint32_t sample_count = 0U;
    uint32_t expected_sequence = 0U;
    uint32_t expected_sample_period_ns = 0U;
    uint8_t block_index;

    if (event == RT_NULL
        || event->block_count
           != EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_STANDARD_POST_BLOCK_COUNT
        || event->pretrigger_block_count != EVENT_PRETRIGGER_BLOCK_COUNT
        || event->posttrigger_block_count != EVENT_STANDARD_POST_BLOCK_COUNT
        || (event->flags & (EVENT_FLAG_PRETRIGGER_SHORT | EVENT_FLAG_DATA_LOSS))
           != 0U)
    {
        return RT_FALSE;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        const sample_block_t *block = event->blocks[block_index];

        if (block == RT_NULL || block->sample_count != IMU_SAMPLE_BATCH_SIZE
            || block->sample_period_ns == 0U
            || (block_index != 0U
                && block->sample_period_ns != expected_sample_period_ns)
            || (block_index != 0U && block->sequence != expected_sequence))
        {
            return RT_FALSE;
        }

        if (block_index == 0U)
        {
            expected_sample_period_ns = block->sample_period_ns;
        }
        if (sample_count > EVENT_FIXED_SAMPLE_COUNT - block->sample_count)
        {
            return RT_FALSE;
        }
        sample_count += event->blocks[block_index]->sample_count;
        expected_sequence = block->sequence + block->sample_count;
    }

    return sample_count == EVENT_FIXED_SAMPLE_COUNT;
}
#endif

static void event_service_reset_detector_counts_locked(void)
{
    event_impact_detector.consecutive_count = 0U;
    event_drop_detector.consecutive_count = 0U;
}

static void event_service_reset_policy_on_restart_locked(void)
{
    event_service_reset_detector_counts_locked();
    event_natural_trigger_latched = RT_FALSE;
    event_cooldown_rejection_latched = RT_FALSE;
    event_natural_cooldown_deadline_us = 0U;
    event_power_activation_pending = RT_FALSE;
    event_power_active_owned = RT_FALSE;
}

static void event_service_start_cooldown_locked(void)
{
    uint64_t now_us = monotonic_clock_now_us();

    event_service_reset_detector_counts_locked();
    event_natural_trigger_latched = RT_FALSE;
    event_cooldown_rejection_latched = RT_FALSE;
    if (now_us > UINT64_MAX - EVENT_NATURAL_TRIGGER_COOLDOWN_US)
    {
        event_natural_cooldown_deadline_us = UINT64_MAX;
    }
    else
    {
        event_natural_cooldown_deadline_us =
            now_us + EVENT_NATURAL_TRIGGER_COOLDOWN_US;
    }
}

static void event_service_expire_cooldown_if_due_locked(uint64_t now_us)
{
    if (event_natural_cooldown_deadline_us != 0U
        && now_us >= event_natural_cooldown_deadline_us)
    {
        event_natural_cooldown_deadline_us = 0U;
        event_cooldown_rejection_latched = RT_FALSE;
        event_service_reset_detector_counts_locked();
    }
}

static uint32_t event_service_cooldown_remaining_ms_locked(uint64_t now_us)
{
    uint64_t remaining_us;
    uint64_t max_ms_us = (uint64_t)UINT32_MAX * UINT64_C(1000);

    if (event_natural_cooldown_deadline_us == 0U
        || now_us >= event_natural_cooldown_deadline_us)
    {
        return 0U;
    }
    remaining_us = event_natural_cooldown_deadline_us - now_us;
    if (remaining_us > max_ms_us)
    {
        return UINT32_MAX;
    }
    remaining_us = (remaining_us + UINT64_C(999)) / UINT64_C(1000);
    return remaining_us > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining_us;
}

static void event_service_record_cooldown_rejection_locked(void)
{
    if (!event_cooldown_rejection_latched)
    {
        if (event_cooldown_rejection_count != UINT32_MAX)
        {
            event_cooldown_rejection_count++;
        }
        event_cooldown_rejection_latched = RT_TRUE;
    }
    event_service_reset_detector_counts_locked();
}

static void event_service_accept_natural_trigger_locked(void)
{
    if (event_natural_accepted_count != UINT32_MAX)
    {
        event_natural_accepted_count++;
    }
    event_natural_trigger_latched = RT_TRUE;
    event_service_reset_detector_counts_locked();
}

static void event_service_rearm_after_event_locked(void)
{
    if (event_assembler.state == EVENT_ARMED)
    {
        event_service_start_cooldown_locked();
    }
}

static const trigger_fact_t *event_service_find_trigger(const sample_block_t *block,
                                                         trigger_fact_t *trigger)
{
    uint16_t index;
    uint64_t sample_time_us;
    uint64_t sample_offset_us;
    uint64_t now_us;
    trigger_fact_t impact_trigger;
    trigger_fact_t drop_trigger;
    bool impact_hit;
    bool drop_hit;

    if (event_test_trigger_pending)
    {
        if (event_assembler.state != EVENT_ARMED
            || !event_service_sink_is_ready())
        {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
            if (event_fault_injection_case_pending
                    == EVENT_QUALITY_FAULT_POOL_PRESSURE)
            {
                event_fault_injection_case_pending = 0U;
                event_test_trigger_pending = RT_FALSE;
                event_service_abandon_pool_pressure_locked();
            }
#endif
            return RT_NULL;
        }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        if (event_fault_injection_case_pending
                == EVENT_QUALITY_FAULT_POOL_PRESSURE
            && event_service_reserve_pool_pressure_locked(
                   SAMPLE_BLOCK_HANDOFF_COUNT - 1U) != RT_EOK)
        {
            event_fault_injection_case_pending = 0U;
            event_test_trigger_pending = RT_FALSE;
            event_service_abandon_pool_pressure_locked();
            return RT_NULL;
        }
#endif
        event_test_trigger_pending = RT_FALSE;
        trigger->sample_sequence = event_service_sample_sequence(block, 0U);
        trigger->sample_index = 0U;
        trigger->magnitude_sq = 0U;
        trigger->threshold_magnitude_sq =
            event_impact_detector.config.threshold_magnitude_sq;
        trigger->axis_mask = 0U;
        return trigger;
    }

    if (event_test_trigger_deadline_us != 0U)
    {
        if (event_assembler.state != EVENT_ARMED
            || !event_service_sink_is_ready())
        {
            return RT_NULL;
        }
        for (index = 0U; index < block->sample_count; index++)
        {
            sample_offset_us = ((uint64_t)index * block->sample_period_ns) / 1000U;
            sample_time_us = (block->first_monotonic_us > UINT64_MAX - sample_offset_us)
                           ? UINT64_MAX
                           : block->first_monotonic_us + sample_offset_us;
            if (sample_time_us >= event_test_trigger_deadline_us)
            {
                event_test_trigger_deadline_us = 0U;
                trigger->sample_sequence = event_service_sample_sequence(block,
                                                                          index);
                trigger->sample_index = index;
                trigger->magnitude_sq = 0U;
                trigger->threshold_magnitude_sq =
                    event_impact_detector.config.threshold_magnitude_sq;
                trigger->axis_mask = 0U;
                rt_kprintf("event trigger delayed start\n");
                return trigger;
            }
        }
    }

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (event_service_queue_pressure_is_active_locked())
    {
        if (!event_service_sink_is_ready())
        {
            event_service_cancel_queue_pressure_locked();
            return RT_NULL;
        }
        if (event_assembler.pretrigger.count
                >= EVENT_PRETRIGGER_BLOCK_COUNT)
        {
            trigger->sample_sequence = event_service_sample_sequence(block, 0U);
            trigger->sample_index = 0U;
            trigger->magnitude_sq = 0U;
            trigger->threshold_magnitude_sq =
                event_impact_detector.config.threshold_magnitude_sq;
            trigger->axis_mask = 0U;
            return trigger;
        }
    }
#endif

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (event_assembler.state == EVENT_POST_TRIGGER_CAPTURING
        && event_fault_injection_case_pending
               == EVENT_QUALITY_FAULT_DURATION_CAP
        && event_assembler.event.posttrigger_block_count
               == EVENT_MAX_POST_BLOCK_COUNT
                      - EVENT_STANDARD_POST_BLOCK_COUNT + 1U)
    {
        trigger->sample_sequence = event_service_sample_sequence(block, 0U);
        trigger->sample_index = 0U;
        trigger->magnitude_sq = event_assembler.event.threshold_magnitude_sq;
        trigger->threshold_magnitude_sq =
            event_assembler.event.threshold_magnitude_sq;
        trigger->axis_mask = 0U;
        return trigger;
    }
#endif

    if (event_assembler.state != EVENT_ARMED)
    {
        return RT_NULL;
    }

    now_us = monotonic_clock_now_us();
    event_service_expire_cooldown_if_due_locked(now_us);
    if (event_natural_trigger_latched)
    {
        return RT_NULL;
    }
    if (!event_service_sink_is_ready())
    {
        event_service_reset_detector_counts_locked();
        return RT_NULL;
    }

    for (index = 0U; index < block->sample_count; index++)
    {
        if (event_natural_cooldown_deadline_us != 0U
            && now_us < event_natural_cooldown_deadline_us)
        {
            impact_hit = trigger_detector_feed(
                &event_impact_detector,
                &block->samples[index],
                event_service_sample_sequence(block, index),
                index,
                &impact_trigger);
            drop_hit = trigger_detector_feed(
                &event_drop_detector,
                &block->samples[index],
                event_service_sample_sequence(block, index),
                index,
                &drop_trigger);
            if (impact_hit || drop_hit)
            {
                event_service_record_cooldown_rejection_locked();
            }
            continue;
        }

        impact_hit = trigger_detector_feed(
            &event_impact_detector,
            &block->samples[index],
            event_service_sample_sequence(block, index),
            index,
            &impact_trigger);
        drop_hit = trigger_detector_feed(
            &event_drop_detector,
            &block->samples[index],
            event_service_sample_sequence(block, index),
            index,
            &drop_trigger);
        if (impact_hit)
        {
            *trigger = impact_trigger;
            event_service_accept_natural_trigger_locked();
            return trigger;
        }
        if (drop_hit)
        {
            *trigger = drop_trigger;
            event_service_accept_natural_trigger_locked();
            return trigger;
        }
    }

    return RT_NULL;
}

static rt_err_t event_service_process_block_locked(sample_block_t *block,
                                                   const trigger_fact_t *trigger)
{
    event_state_t state_before = event_assembler.state;
    rt_err_t result;

    if (state_before == EVENT_ARMED && event_sink_configured
        && event_sink.get_status != RT_NULL)
    {
        event_export_sink_status_t status;

        if (event_sink.get_status(event_sink.context, &status) == RT_EOK
            && status.next_event_id != 0U)
        {
            /*
             * Formatting/recovering the external log can change its next ID
             * while the event service remains running.  Synchronize before
             * the next trigger so the first post-format event is not lost.
             */
            event_assembler_set_next_event_id(&event_assembler,
                                              status.next_event_id);
        }
    }

    result = event_assembler_consume(&event_assembler,
                                     event_service_sample_pool(),
                                     block, trigger);
    if (result == RT_EOK && state_before == EVENT_ARMED && trigger != RT_NULL
        && event_assembler.state == EVENT_POST_TRIGGER_CAPTURING
        && event_health_service != RT_NULL)
    {
        health_snapshot_t snapshot;

        health_service_get_snapshot(event_health_service, &snapshot);
        event_assembler_set_health_snapshot(&event_assembler, &snapshot);
    }
    if (result == RT_EOK)
    {
        event_processed_block_count++;
    }
    else if (event_assembler.state == EVENT_ARMED)
    {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        event_service_cancel_queue_pressure_locked();
        if (event_fault_injection_case_pending
                == EVENT_QUALITY_FAULT_POOL_PRESSURE)
        {
            event_service_abandon_pool_pressure_locked();
        }
        event_fault_injection_case_pending = 0U;
#endif
        event_service_reset_detector_counts_locked();
        if (event_natural_trigger_latched)
        {
            event_service_rearm_after_event_locked();
        }
    }
    event_service_publish_power_state(state_before, event_assembler.state);

    return result;
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static void event_service_bound_corrupt_event_locked(void);
#endif

static rt_err_t event_service_clear_ready_locked(void)
{
    sample_block_pool_t *pool = event_service_sample_pool();
    event_state_t state_before;
    rt_err_t result;

    if (pool == RT_NULL)
    {
        return -RT_ERROR;
    }

    state_before = event_assembler.state;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    event_service_cancel_queue_pressure_locked();
    event_service_abandon_pool_pressure_locked();
    if (event_fault_injection_case_pending
            == EVENT_QUALITY_FAULT_POOL_PRESSURE)
    {
        event_fault_injection_case_pending = 0U;
        event_test_trigger_pending = RT_FALSE;
    }
    if (state_before == EVENT_READY_FOR_EXPORT)
    {
        event_fault_injection_case_pending = 0U;
    }
#endif
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    event_service_bound_corrupt_event_locked();
#endif
    result = event_assembler_clear(&event_assembler, pool);
    if (state_before == EVENT_READY_FOR_EXPORT
        && event_assembler.state == EVENT_ARMED)
    {
        event_service_rearm_after_event_locked();
    }
    event_service_publish_power_state(state_before, event_assembler.state);
    return result;
}

#ifndef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static rt_err_t event_service_reject_invalid_event_locked(void)
{
    event_export_error_count++;
    if (event_assembler.state == EVENT_READY_FOR_EXPORT)
    {
        (void)event_service_clear_ready_locked();
    }
    rt_kprintf("event log rejected; expected %u blocks x %u samples (%lu total)\n",
               (unsigned int)(EVENT_PRETRIGGER_BLOCK_COUNT
                              + EVENT_STANDARD_POST_BLOCK_COUNT),
               (unsigned int)IMU_SAMPLE_BATCH_SIZE,
               (unsigned long)EVENT_FIXED_SAMPLE_COUNT);
    return -RT_ERROR;
}
#endif

static void event_service_recover_log_after_write_failure_locked(void)
{
    event_export_sink_status_t status;

    if (event_assembler.state == EVENT_READY_FOR_EXPORT)
    {
        (void)event_service_clear_ready_locked();
    }
    if (event_sink.abort != RT_NULL && event_sink.get_status != RT_NULL
        && event_sink.abort(event_sink.context) == RT_EOK
        && event_sink.get_status(event_sink.context, &status) == RT_EOK
        && status.state == EVENT_EXPORT_SINK_READY)
    {
        event_assembler_set_next_event_id(&event_assembler,
                                          status.next_event_id);
        rt_kprintf("event log write failed; log recovered\n");
    }
    else
    {
        rt_kprintf("event log write failed; log stopped\n");
    }
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static void event_service_record_capture_failure_locked(void)
{
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    event_service_cancel_queue_pressure_locked();
#endif
    memset(&event_last_quality, 0, sizeof(event_last_quality));
    event_last_quality.serialization = EVENT_QUALITY_CAPTURE_FAILURE;
    event_last_quality.verdict = EVENT_QUALITY_NO_VERDICT;
    event_last_quality_valid = RT_TRUE;
    if (event_export_error_count != UINT32_MAX)
    {
        event_export_error_count++;
    }
}

static void event_service_bound_corrupt_event_locked(void)
{
    if (event_assembler.event.block_count > EVENT_MAX_BLOCK_COUNT)
    {
        event_assembler.event.block_count = EVENT_MAX_BLOCK_COUNT;
    }
}

static rt_err_t event_service_retain_event_locked(sample_block_pool_t *pool,
                                                  const event_record_t *event,
                                                  event_record_t *snapshot)
{
    uint8_t block_index;
    const uint8_t max_block_count =
        (uint8_t)(sizeof(event->blocks) / sizeof(event->blocks[0]));

    if (pool == RT_NULL || event == RT_NULL || snapshot == RT_NULL
        || event->block_count == 0U || event->block_count > max_block_count)
    {
        return -RT_ERROR;
    }
    *snapshot = *event;
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        if (event->blocks[block_index] == RT_NULL
            || sample_block_pool_retain(pool, event->blocks[block_index])
            != RT_EOK)
        {
            while (block_index != 0U)
            {
                block_index--;
                (void)sample_block_pool_release(pool,
                                                 event->blocks[block_index]);
            }
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}

static void event_service_release_event_locked(sample_block_pool_t *pool,
                                               const event_record_t *event)
{
    uint8_t block_index;
    const uint8_t max_block_count =
        (uint8_t)(sizeof(event->blocks) / sizeof(event->blocks[0]));
    uint8_t block_count;

    if (pool == RT_NULL || event == RT_NULL)
    {
        return;
    }
    block_count = event->block_count > max_block_count
                      ? max_block_count
                      : event->block_count;
    for (block_index = 0U; block_index < block_count; block_index++)
    {
        if (event->blocks[block_index] != RT_NULL)
        {
            (void)sample_block_pool_release(pool, event->blocks[block_index]);
        }
    }
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static rt_err_t event_service_apply_fault_injection_locked(void)
{
    event_record_t *event = &event_assembler.event;
    sample_block_pool_t *pool = event_service_sample_pool();
    uint32_t event_case = event_fault_injection_case_pending;
    uint8_t index;

    if (event_case == 0U)
    {
        return RT_EOK;
    }
    event_fault_injection_case_pending = 0U;
    event_test_trigger_pending = RT_FALSE;
    event_test_trigger_deadline_us = 0U;
    if (pool == RT_NULL || event_assembler.state != EVENT_READY_FOR_EXPORT)
    {
        return -RT_ERROR;
    }

    if (event_case == EVENT_QUALITY_FAULT_PRETRIGGER_SHORT)
    {
        sample_block_t *removed;

        if (event->pretrigger_block_count != EVENT_PRETRIGGER_BLOCK_COUNT
            || event->posttrigger_block_count
                   != EVENT_STANDARD_POST_BLOCK_COUNT
            || event->block_count
                   != EVENT_PRETRIGGER_BLOCK_COUNT
                          + EVENT_STANDARD_POST_BLOCK_COUNT
            || event->blocks[0] == RT_NULL)
        {
            return -RT_ERROR;
        }
        removed = event->blocks[0];
        if (sample_block_pool_release(pool, removed) != RT_EOK)
        {
            return -RT_ERROR;
        }
        for (index = 1U; index < event->block_count; index++)
        {
            event->blocks[index - 1U] = event->blocks[index];
        }
        event->block_count--;
        event->pretrigger_block_count--;
        event->blocks[event->block_count] = RT_NULL;
        event->flags |= EVENT_FLAG_PRETRIGGER_SHORT;
        return RT_EOK;
    }

    if (event_case == EVENT_QUALITY_FAULT_SEQUENCE_GAP)
    {
        uint8_t gap_index = (uint8_t)(event->pretrigger_block_count + 1U);
        uint64_t shift_us;

        if (gap_index >= event->block_count
            || event->blocks[gap_index] == RT_NULL
            || event->blocks[gap_index]->sample_period_ns < 1000U)
        {
            return -RT_ERROR;
        }
        shift_us = event->blocks[gap_index]->sample_period_ns / 1000U;
        for (index = gap_index; index < event->block_count; index++)
        {
            if (event->blocks[index] == RT_NULL
                || event->blocks[index]->sequence == UINT32_MAX
                || event->blocks[index]->first_monotonic_us
                       > UINT64_MAX - shift_us)
            {
                return -RT_ERROR;
            }
        }
        for (index = gap_index; index < event->block_count; index++)
        {
            event->blocks[index]->sequence++;
            event->blocks[index]->first_monotonic_us += shift_us;
        }
        event->flags |= EVENT_FLAG_DATA_LOSS;
        return RT_EOK;
    }

    if (event_case == EVENT_QUALITY_FAULT_DURATION_CAP)
    {
        if (event->posttrigger_block_count != EVENT_MAX_POST_BLOCK_COUNT
            || event->subtrigger_count == 0U
            || (event->flags & EVENT_FLAG_DURATION_CAPPED) == 0U)
        {
            return -RT_ERROR;
        }
        return RT_EOK;
    }

    return -RT_ERROR;
}
#endif

static void event_service_record_storage_failure_locked(void)
{
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    event_service_cancel_queue_pressure_locked();
#endif
    event_last_storage_outcome = EVENT_SERVICE_STORAGE_FAILURE;
    if (event_export_error_count != UINT32_MAX)
    {
        event_export_error_count++;
    }
}

static rt_err_t event_service_export_ready_locked(void)
{
    event_state_t state_before = event_assembler.state;
    event_quality_result_t live_result;
    event_quality_result_t final_result;
    event_quality_facts_t readback_facts;
    event_record_t event_snapshot;
    const event_record_t *event;
    sample_block_pool_t *pool;
    uint8_t header[EVENT_EXPORT_HEADER_SIZE];
    uint32_t ev01_length;
    uint32_t read_length;

    event_last_quality_valid = RT_FALSE;
    event_last_storage_outcome = EVENT_SERVICE_STORAGE_NONE;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (event_service_apply_fault_injection_locked() != RT_EOK)
    {
        event_service_record_capture_failure_locked();
        (void)event_service_clear_ready_locked();
        rt_kprintf("event fault injection failed serialization safety gate\n");
        return -RT_ERROR;
    }
#endif
    event = event_assembler_event(&event_assembler);
    pool = event_service_sample_pool();
    if (event == RT_NULL || pool == RT_NULL)
    {
        event_service_record_storage_failure_locked();
        rt_kprintf("event log is unavailable; run log status or log format --confirm\n");
        return -RT_ERROR;
    }

    event_snapshot = *event;
    if (event_service_retain_event_locked(pool, event, &event_snapshot)
        != RT_EOK)
    {
        event_service_record_capture_failure_locked();
        (void)event_service_clear_ready_locked();
        rt_kprintf("event capture failed serialization safety gate\n");
        return -RT_ERROR;
    }

    (void)event_quality_evaluate(pool, &event_snapshot, &live_result);
    if (live_result.serialization != EVENT_QUALITY_SERIALIZABLE)
    {
        event_last_quality = live_result;
        event_last_quality_valid = RT_TRUE;
        event_service_release_event_locked(pool, &event_snapshot);
        if (event_export_error_count != UINT32_MAX)
        {
            event_export_error_count++;
        }
        (void)event_service_clear_ready_locked();
        rt_kprintf("event capture failed serialization safety gate\n");
        return -RT_ERROR;
    }
    if (!event_sink_configured || !event_service_sink_is_ready()
        || event_sink.begin == RT_NULL || event_sink.write == RT_NULL
        || event_sink.verify == RT_NULL || event_sink.read == RT_NULL)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        (void)event_service_clear_ready_locked();
        rt_kprintf("event log is unavailable; run log status or log format --confirm\n");
        return -RT_ERROR;
    }
    if (event_service_ev01_length(&ev01_length) != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        (void)event_service_clear_ready_locked();
        return -RT_ERROR;
    }

    if (event_sink.begin(event_sink.context, event->event_id, ev01_length)
        != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        event_service_recover_log_after_write_failure_locked();
        return -RT_ERROR;
    }
    if (event_assembler_begin_export(&event_assembler) != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        event_service_recover_log_after_write_failure_locked();
        return -RT_ERROR;
    }

    rt_kprintf("event log write begins\n");
    if (event_export_debug_write(&event_assembler, pool, event_sink.write,
                                 event_sink.context) != RT_EOK)
    {
        event_service_publish_power_state(state_before, event_assembler.state);
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        event_service_recover_log_after_write_failure_locked();
        return -RT_ERROR;
    }
    event_service_publish_power_state(state_before, event_assembler.state);
    if (event_assembler.state == EVENT_ARMED)
    {
        event_service_rearm_after_event_locked();
    }
    live_result.committed = RT_TRUE;

    if (event_sink.verify(event_sink.context, event_snapshot.event_id)
        != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        return -RT_ERROR;
    }
    read_length = 0U;
    if (event_sink.read(event_sink.context, event_snapshot.event_id, 0U,
                       header, sizeof(header), &read_length) != RT_EOK
        || read_length != sizeof(header)
        || event_export_debug_decode_header(header, sizeof(header),
                                            ev01_length,
                                            &readback_facts) != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        return -RT_ERROR;
    }
    if (event_quality_check_round_trip(&live_result, &readback_facts,
                                       &final_result) != RT_EOK)
    {
        event_service_release_event_locked(pool, &event_snapshot);
        event_service_record_storage_failure_locked();
        return -RT_ERROR;
    }
    final_result.committed = RT_TRUE;
    final_result.readback_verified = RT_TRUE;
    final_result.ai_eligible = event_quality_ai_eligible(&final_result);
    event_last_quality = final_result;
    event_last_quality_valid = RT_TRUE;
    event_last_storage_outcome = EVENT_SERVICE_STORAGE_COMMITTED;

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    if (event_quality_ai_eligible(&final_result))
    {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        rt_err_t ai_submit_result = ai_inference_service_submit_event(
            pool, &event_snapshot);

        if (event_queue_pressure_active
            && (ai_submit_result != RT_EOK
                || ++event_queue_pressure_submitted_count
                       >= EVENT_QUEUE_PRESSURE_TARGET_EVENTS))
        {
            event_service_cancel_queue_pressure_locked();
        }
#else
        (void)ai_inference_service_submit_event(pool, &event_snapshot);
#endif
    }
#endif
    event_service_release_event_locked(pool, &event_snapshot);
    rt_kprintf("event log write complete\n");
    return RT_EOK;
}
#else
static rt_err_t event_service_export_ready_locked(void)
{
    event_state_t state_before = event_assembler.state;
    rt_err_t result;
    const event_record_t *event;
    sample_block_pool_t *pool;
    uint32_t ev01_length;
    event_export_sink_status_t status;

    event = event_assembler_event(&event_assembler);
    pool = event_service_sample_pool();
    if (event == RT_NULL || pool == RT_NULL)
    {
        event_export_error_count++;
        rt_kprintf("event log is unavailable; run log status or log format --confirm\n");
        return -RT_ERROR;
    }
    if (!event_service_event_has_fixed_sample_count_locked())
    {
        return event_service_reject_invalid_event_locked();
    }
    if (!event_sink_configured || !event_service_sink_is_ready()
        || event_sink.begin == RT_NULL || event_sink.write == RT_NULL)
    {
        event_export_error_count++;
        rt_kprintf("event log is unavailable; run log status or log format --confirm\n");
        return -RT_ERROR;
    }
    if (event_service_ev01_length(&ev01_length) != RT_EOK)
    {
        return event_service_reject_invalid_event_locked();
    }
    if (event_sink.begin(event_sink.context, event->event_id, ev01_length) != RT_EOK)
    {
        event_export_error_count++;
        if (event_sink.get_status == RT_NULL
            || event_sink.get_status(event_sink.context, &status) != RT_EOK
            || status.state == EVENT_EXPORT_SINK_ERROR)
        {
            event_service_recover_log_after_write_failure_locked();
        }
        else
        {
            if (event_assembler.state == EVENT_READY_FOR_EXPORT)
            {
                (void)event_service_clear_ready_locked();
            }
            if (status.next_event_id != 0U)
            {
                event_assembler_set_next_event_id(&event_assembler,
                                                  status.next_event_id);
            }
            rt_kprintf("event log is unavailable; run log status or log format --confirm\n");
        }
        return -RT_ERROR;
    }

    if (event_assembler_begin_export(&event_assembler) != RT_EOK)
    {
        event_export_error_count++;
        event_service_recover_log_after_write_failure_locked();
        return -RT_ERROR;
    }

    rt_kprintf("event log write begins\n");
    result = event_export_debug_write(&event_assembler, pool,
                                      event_sink.write, event_sink.context);
    event_service_publish_power_state(state_before, event_assembler.state);
    if (event_assembler.state == EVENT_ARMED)
    {
        event_service_rearm_after_event_locked();
    }
    if (result != RT_EOK)
    {
        event_export_error_count++;
        event_service_recover_log_after_write_failure_locked();
        return result;
    }

    rt_kprintf("event log write complete\n");
    return RT_EOK;
}
#endif

static void event_service_entry(void *parameter)
{
    sample_block_t *block;
    trigger_fact_t trigger;
    rt_err_t result;

    (void)parameter;
    while (RT_TRUE)
    {
        block = imu_acquisition_take_ready_block(RT_WAITING_FOREVER);
        if (block == RT_NULL)
        {
            continue;
        }
        if (event_service_lock() != RT_EOK)
        {
            (void)sample_block_pool_release(event_service_sample_pool(), block);
            continue;
        }

        result = event_service_process_block_locked(block,
                                                    event_service_find_trigger(block,
                                                                               &trigger));
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
        if (result == RT_EOK
            && event_assembler.state == EVENT_READY_FOR_EXPORT)
        {
            (void)event_service_export_ready_locked();
        }
#else
        if (result == RT_EOK && event_assembler.state == EVENT_READY_FOR_EXPORT
            && event_service_sink_is_ready())
        {
            if (event_service_event_has_fixed_sample_count_locked())
            {
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
                (void)ai_inference_service_submit_event(
                    event_service_sample_pool(),
                    event_assembler_event(&event_assembler));
#endif
                (void)event_service_export_ready_locked();
            }
            else
            {
                (void)event_service_reject_invalid_event_locked();
            }
        }
        else if (result == RT_EOK && event_assembler.state == EVENT_READY_FOR_EXPORT)
        {
            if (!event_service_event_has_fixed_sample_count_locked())
            {
                (void)event_service_reject_invalid_event_locked();
            }
            else
            {
                (void)event_service_clear_ready_locked();
                event_export_error_count++;
                rt_kprintf("event log is not ready; event discarded\n");
            }
        }
#endif
        event_service_unlock();
    }
}

rt_err_t event_service_start(const event_export_sink_t *sink)
{
    const trigger_detector_config_t impact_detector_config = {
        .threshold_magnitude_sq = EVENT_IMPACT_TRIGGER_THRESHOLD_MAGNITUDE_SQ,
        .consecutive_samples = EVENT_IMPACT_TRIGGER_CONSECUTIVE_SAMPLES,
        .comparison = TRIGGER_COMPARISON_ABOVE,
    };
    const trigger_detector_config_t drop_detector_config = {
        .threshold_magnitude_sq = EVENT_DROP_TRIGGER_THRESHOLD_MAGNITUDE_SQ,
        .consecutive_samples = EVENT_DROP_TRIGGER_CONSECUTIVE_SAMPLES,
        .comparison = TRIGGER_COMPARISON_BELOW,
    };
    rt_thread_t thread;
    event_export_sink_status_t status;

    if (event_service_started)
    {
        return RT_EOK;
    }
    if (sink == RT_NULL || sink->is_ready == RT_NULL || sink->begin == RT_NULL
        || sink->write == RT_NULL || sink->abort == RT_NULL
        || sink->get_status == RT_NULL
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
        || sink->verify == RT_NULL || sink->read == RT_NULL
#endif
    )
    {
        return -RT_ERROR;
    }
    if (!event_service_mutex_initialized)
    {
        if (rt_mutex_init(&event_service_mutex, "event", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            return -RT_ERROR;
        }
        event_service_mutex_initialized = RT_TRUE;
    }

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    event_service_cancel_queue_pressure_locked();
    event_service_abandon_pool_pressure_locked();
#endif
    event_assembler_init(&event_assembler);
    event_service_reset_policy_on_restart_locked();
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    memset(&event_last_quality, 0, sizeof(event_last_quality));
    event_last_quality_valid = RT_FALSE;
    event_last_storage_outcome = EVENT_SERVICE_STORAGE_NONE;
#endif
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    event_fault_injection_case_pending = 0U;
    event_queue_pressure_active = RT_FALSE;
    event_queue_pressure_submitted_count = 0U;
#endif
    event_test_trigger_pending = RT_FALSE;
    event_test_trigger_deadline_us = 0U;
    event_sink = *sink;
    event_sink_configured = RT_TRUE;
    if (event_sink.get_status(event_sink.context, &status) == RT_EOK
        && status.state == EVENT_EXPORT_SINK_READY)
    {
        event_assembler_set_next_event_id(&event_assembler,
                                          status.next_event_id);
    }
    trigger_detector_init(&event_impact_detector, &impact_detector_config);
    trigger_detector_init(&event_drop_detector, &drop_detector_config);
    thread = rt_thread_create("event", event_service_entry, RT_NULL,
                              EVENT_SERVICE_THREAD_STACK_SIZE,
                              EVENT_SERVICE_THREAD_PRIORITY,
                              EVENT_SERVICE_THREAD_TICK);
    if (thread == RT_NULL)
    {
        return -RT_ERROR;
    }

    event_service_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

void event_service_set_health_service(const health_service_t *service)
{
    event_health_service = service;
}

void event_service_get_stats(event_service_stats_t *stats)
{
    uint64_t now_us;

    if (stats == RT_NULL)
    {
        return;
    }

    *stats = (event_service_stats_t){0};
    if (event_service_lock() != RT_EOK)
    {
        return;
    }

    stats->state = event_assembler.state;
    stats->pretrigger_block_count = event_assembler.pretrigger.count;
    stats->busy_trigger_count = event_assembler.busy_trigger_count;
    stats->processed_block_count = event_processed_block_count;
    stats->export_error_count = event_export_error_count;
    stats->resource_reject_count = event_assembler.resource_reject_count;
    stats->natural_trigger_count = event_natural_accepted_count;
    stats->cooldown_rejection_count = event_cooldown_rejection_count;
    now_us = monotonic_clock_now_us();
    event_service_expire_cooldown_if_due_locked(now_us);
    stats->cooldown_remaining_ms =
        event_service_cooldown_remaining_ms_locked(now_us);
    event_service_unlock();
}

rt_err_t event_service_request_test_trigger(void)
{
    rt_err_t result = -RT_ERROR;

    if (event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (event_service_started && event_assembler.state == EVENT_ARMED
        && event_service_sink_is_ready()
        && !event_test_trigger_pending
        && event_test_trigger_deadline_us == 0U)
    {
        event_test_trigger_pending = RT_TRUE;
        result = RT_EOK;
    }
    event_service_unlock();
    return result;
}

rt_err_t event_service_request_test_trigger_after(uint32_t delay_ms)
{
    rt_err_t result = -RT_ERROR;
    uint64_t now_us;
    uint64_t delay_us;

    if (delay_ms == 0U || delay_ms > 60000U
        || event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (event_service_started && event_assembler.state == EVENT_ARMED
        && event_service_sink_is_ready()
        && !event_test_trigger_pending
        && event_test_trigger_deadline_us == 0U)
    {
        now_us = monotonic_clock_now_us();
        delay_us = (uint64_t)delay_ms * 1000U;
        if (now_us <= UINT64_MAX - delay_us)
        {
            event_test_trigger_deadline_us = now_us + delay_us;
            rt_kprintf("event trigger scheduled delay_ms=%lu\n",
                       (unsigned long)delay_ms);
            result = RT_EOK;
        }
    }
    event_service_unlock();
    return result;
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
rt_err_t event_service_inject_queue_pressure(void)
{
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    rt_err_t result = -RT_ERROR;

    if (event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!event_service_queue_pressure_is_active_locked()
        && event_service_started && event_assembler.state == EVENT_ARMED
        && event_service_sink_is_ready()
        && event_fault_injection_case_pending == 0U
        && !event_test_trigger_pending
        && event_test_trigger_deadline_us == 0U
        && ai_inference_service_arm_queue_pressure() == RT_EOK)
    {
        event_queue_pressure_active = RT_TRUE;
        event_queue_pressure_submitted_count = 0U;
        result = RT_EOK;
    }
    event_service_unlock();
    return result;
#else
    return -RT_ERROR;
#endif
}

rt_err_t event_service_inject_quality_case(uint32_t event_case)
{
    rt_err_t result = -RT_ERROR;

    if (event_case == EVENT_QUALITY_FAULT_QUEUE_PRESSURE)
    {
        return event_service_inject_queue_pressure();
    }
    if (event_case != EVENT_QUALITY_FAULT_SEQUENCE_GAP
        && event_case != EVENT_QUALITY_FAULT_PRETRIGGER_SHORT
        && event_case != EVENT_QUALITY_FAULT_DURATION_CAP
        && event_case != EVENT_QUALITY_FAULT_POOL_PRESSURE)
    {
        return -RT_ERROR;
    }
    if (event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (event_service_started && event_assembler.state == EVENT_ARMED
        && event_service_sink_is_ready()
        && event_fault_injection_case_pending == 0U
        && !event_test_trigger_pending
        && event_test_trigger_deadline_us == 0U)
    {
        event_fault_injection_case_pending = event_case;
        event_test_trigger_pending = RT_TRUE;
        result = RT_EOK;
    }
    else if (event_fault_injection_case_pending
                 == EVENT_QUALITY_FAULT_POOL_PRESSURE
             && (!event_service_started
                 || event_assembler.state != EVENT_ARMED
                 || !event_service_sink_is_ready()))
    {
        event_fault_injection_case_pending = 0U;
        event_test_trigger_pending = RT_FALSE;
        event_service_abandon_pool_pressure_locked();
    }
    event_service_unlock();
    return result;
}
#endif

rt_err_t event_service_export_ready(void)
{
    rt_err_t result;

    if (event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    result = event_service_export_ready_locked();
    event_service_unlock();
    return result;
}

rt_err_t event_service_clear_ready(void)
{
    rt_err_t result;

    if (event_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    result = event_service_clear_ready_locked();
    event_service_unlock();
    return result;
}

static int event(int argc, char **argv)
{
    event_service_stats_t stats;
    char *end;
    unsigned long delay_ms;

    if (argc < 2 || argc > 3)
    {
        rt_kprintf("usage: event status|arm|trigger_test|trigger_delay <ms>|export|clear\n");
        return -RT_ERROR;
    }

    if (rt_strcmp(argv[1], "status") == 0)
    {
        event_service_get_stats(&stats);
        rt_kprintf("event state=%u pretrigger_blocks=%u busy=%lu export_err=%lu resource_err=%lu natural=%lu cooldown_reject=%lu cooldown_ms=%lu\n",
                   (unsigned int)stats.state,
                   (unsigned int)stats.pretrigger_block_count,
                   (unsigned long)stats.busy_trigger_count,
                   (unsigned long)stats.export_error_count,
                   (unsigned long)stats.resource_reject_count,
                   (unsigned long)stats.natural_trigger_count,
                   (unsigned long)stats.cooldown_rejection_count,
                   (unsigned long)stats.cooldown_remaining_ms);
        return RT_EOK;
    }
    if (rt_strcmp(argv[1], "arm") == 0)
    {
        event_service_get_stats(&stats);
        return (stats.state == EVENT_ARMED) ? RT_EOK : -RT_ERROR;
    }
    if (rt_strcmp(argv[1], "trigger_test") == 0)
    {
        return event_service_request_test_trigger();
    }
    if (argc == 3 && rt_strcmp(argv[1], "trigger_delay") == 0)
    {
        delay_ms = strtoul(argv[2], &end, 0);
        if (argv[2] == end || *end != '\0' || delay_ms > UINT32_MAX)
        {
            return -RT_ERROR;
        }
        return event_service_request_test_trigger_after((uint32_t)delay_ms);
    }
    if (rt_strcmp(argv[1], "export") == 0)
    {
        return event_service_export_ready();
    }
    if (rt_strcmp(argv[1], "clear") == 0)
    {
        return event_service_clear_ready();
    }

    return -RT_ERROR;
}
MSH_CMD_EXPORT(event, event capture control: status arm trigger_test trigger_delay export clear);
