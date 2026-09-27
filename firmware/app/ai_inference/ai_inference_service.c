#include "ai_inference_service.h"

#include <string.h>

#include "ai_features.h"
#include "power_runtime.h"

#define AI_INFERENCE_THREAD_STACK_SIZE 2560U
#define AI_INFERENCE_THREAD_PRIORITY 15U
#define AI_INFERENCE_THREAD_TICK 10U
#define AI_RESULT_PERSIST_RETRY_DELAY_MS 100U
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
#define AI_QUEUE_PRESSURE_TIMEOUT_TICKS (30U * RT_TICK_PER_SECOND)
#endif

typedef struct
{
    uint32_t event_id;
    sample_block_pool_t *pool;
    event_record_t event;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    rt_bool_t has_precomputed_features;
    ai_feature_vector_t precomputed_features;
    uint32_t precomputed_sample_count;
    uint16_t precomputed_event_flags;
#endif
} ai_inference_item_t;

static ai_inference_item_t ai_queue[AI_INFERENCE_QUEUE_CAPACITY];
static uint8_t ai_queue_head;
static uint8_t ai_queue_count;
static rt_bool_t ai_worker_event_in_flight;
static ai_result_t ai_result_queue[AI_RESULT_PERSIST_QUEUE_CAPACITY];
static uint8_t ai_result_queue_head;
static uint8_t ai_result_queue_count;
static rt_bool_t ai_result_persist_in_flight;
static rt_bool_t ai_power_blocker_held;
static struct rt_mutex ai_mutex;
static rt_bool_t ai_mutex_initialized;
static struct rt_mutex ai_result_store_mutex;
static rt_bool_t ai_result_store_mutex_initialized;
static rt_bool_t ai_service_started;
static rt_bool_t ai_worker_prefer_event;
static rt_bool_t ai_model_transition_prepared;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static rt_bool_t ai_queue_pressure_active;
static rt_tick_t ai_queue_pressure_started_tick;
#endif
/* Set after an ambiguous persistent activation; only process restart/startup
 * may clear it, so a caller cannot silently re-enable an untrusted model. */
static rt_bool_t ai_model_quarantined;
static const ai_model_t *ai_model;
static ai_inference_stats_t ai_stats;
static ai_result_sidecar_t *ai_result_sidecar;
static ai_result_t ai_latest_result;
static rt_bool_t ai_latest_result_valid;
static uint32_t ai_next_result_sequence;

static rt_err_t ai_inference_lock(void)
{
    if (!ai_mutex_initialized
        || rt_mutex_take(&ai_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t ai_result_store_lock(void)
{
    if (!ai_result_store_mutex_initialized
        || rt_mutex_take(&ai_result_store_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static void ai_result_store_unlock(void)
{
    if (ai_result_store_mutex_initialized)
    {
        (void)rt_mutex_release(&ai_result_store_mutex);
    }
}

static void ai_inference_unlock(void)
{
    if (ai_mutex_initialized)
    {
        (void)rt_mutex_release(&ai_mutex);
    }
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static void ai_inference_queue_pressure_expire_locked(void)
{
    if (ai_queue_pressure_active
        && (rt_tick_t)(rt_tick_get() - ai_queue_pressure_started_tick)
               >= AI_QUEUE_PRESSURE_TIMEOUT_TICKS)
    {
        ai_queue_pressure_active = RT_FALSE;
        ai_queue_pressure_started_tick = 0U;
    }
}

static void ai_inference_queue_pressure_cancel_locked(void)
{
    ai_queue_pressure_active = RT_FALSE;
    ai_queue_pressure_started_tick = 0U;
}

static rt_bool_t ai_inference_queue_pressure_snapshot(void)
{
    rt_bool_t active = RT_FALSE;

    if (ai_inference_lock() != RT_EOK)
    {
        return RT_FALSE;
    }
    ai_inference_queue_pressure_expire_locked();
    active = ai_queue_pressure_active;
    ai_inference_unlock();
    return active;
}
#endif

static rt_bool_t ai_inference_event_dequeue_allowed_locked(void)
{
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_inference_queue_pressure_expire_locked();
    return ai_queue_pressure_active == RT_FALSE;
#else
    return RT_TRUE;
#endif
}

/* The AI blocker follows service work ownership, not only the worker's
 * compute interval.  This function is called with ai_mutex held. */
static void ai_inference_reconcile_power_blocker_locked(void)
{
    const rt_bool_t work_active = ai_queue_count != 0U
                                  || ai_result_queue_count != 0U
                                  || ai_worker_event_in_flight
                                  || ai_result_persist_in_flight;

    if (work_active && !ai_power_blocker_held)
    {
        if (power_runtime_acquire_blocker(POWER_BLOCKER_AI) == RT_EOK)
        {
            ai_power_blocker_held = RT_TRUE;
        }
    }
    else if (!work_active && ai_power_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_AI);
        ai_power_blocker_held = RT_FALSE;
    }
}

/* A model transition gate is kept separate from ai_mutex so QSPI commit
 * latency does not hold the general inference/result lock. */
static rt_err_t ai_inference_model_lock(void)
{
    for (;;)
    {
        if (ai_inference_lock() != RT_EOK)
        {
            return -RT_ERROR;
        }
        if (ai_model_quarantined)
        {
            ai_inference_unlock();
            return -RT_ERROR;
        }
        if (!ai_model_transition_prepared)
        {
            return RT_EOK;
        }
        ai_inference_unlock();
        rt_thread_mdelay(1U);
    }
}

static uint32_t ai_inference_event_sample_count(const event_record_t *event)
{
    uint8_t block_index;
    uint32_t count = 0U;

    if (event == RT_NULL)
    {
        return 0U;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        if (event->blocks[block_index] == RT_NULL)
        {
            return 0U;
        }
        count += event->blocks[block_index]->sample_count;
    }
    return count;
}

static uint8_t ai_inference_quality_flags(const event_record_t *event,
                                          uint32_t sample_count)
{
    uint8_t quality_flags = AI_RESULT_QUALITY_EVENT_FLAGS_VALID;

    if (sample_count != 0U)
    {
        quality_flags |= AI_RESULT_QUALITY_SAMPLE_COUNT_VALID;
    }
    if (event == RT_NULL)
    {
        return quality_flags;
    }
    if ((event->flags & EVENT_FLAG_DATA_LOSS) != 0U)
    {
        quality_flags |= AI_RESULT_QUALITY_DATA_LOSS;
    }
    if ((event->flags & EVENT_FLAG_PRETRIGGER_SHORT) != 0U)
    {
        quality_flags |= AI_RESULT_QUALITY_PRETRIGGER_SHORT;
    }
    if ((event->flags & EVENT_FLAG_DURATION_CAPPED) != 0U)
    {
        quality_flags |= AI_RESULT_QUALITY_DURATION_CAPPED;
    }
    if ((event->flags & EVENT_FLAG_RESOURCE_LIMIT) != 0U)
    {
        quality_flags |= AI_RESULT_QUALITY_RESOURCE_LIMIT;
    }
    return quality_flags;
}

static void ai_inference_build_result(const ai_model_t *model,
                                      const event_record_t *event,
                                      ai_result_status_t status,
                                      ai_result_failure_t failure,
                                      const ai_prediction_t *prediction,
                                      ai_result_t *result)
{
    uint32_t sample_count = ai_inference_event_sample_count(event);

    memset(result, 0, sizeof(*result));
    result->event_id = event == RT_NULL ? 0U : event->event_id;
    result->model_version = model == RT_NULL ? 0U : model->version;
    result->status = (uint8_t)status;
    result->quality_flags = ai_inference_quality_flags(event, sample_count);
    result->event_flags = event == RT_NULL ? 0U : event->flags;
    result->sample_count = sample_count;
    result->model_crc32 = model == RT_NULL ? 0U : model->model_crc32;
    result->failure_reason = (uint16_t)failure;
    if (failure == AI_RESULT_FAILURE_RESOURCE_LIMIT)
    {
        result->quality_flags |= AI_RESULT_QUALITY_RESOURCE_LIMIT;
    }
    if (prediction != RT_NULL && status == AI_RESULT_STATUS_PREDICTION)
    {
        result->class_index = prediction->class_index;
        result->class_count = model == RT_NULL ? 0U : model->class_count;
        result->confidence = prediction->confidence;
        memcpy(result->logits, prediction->logits, sizeof(result->logits));
    }
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static void ai_inference_build_compact_result(
    const ai_model_t *model,
    uint32_t event_id,
    uint16_t event_flags,
    uint32_t sample_count,
    ai_result_status_t status,
    ai_result_failure_t failure,
    const ai_prediction_t *prediction,
    ai_result_t *result)
{
    event_record_t event = {0};

    event.event_id = event_id;
    event.flags = event_flags;
    memset(result, 0, sizeof(*result));
    result->event_id = event_id;
    result->model_version = model == RT_NULL ? 0U : model->version;
    result->status = (uint8_t)status;
    result->quality_flags = ai_inference_quality_flags(&event, sample_count);
    result->event_flags = event_flags;
    result->sample_count = sample_count;
    result->model_crc32 = model == RT_NULL ? 0U : model->model_crc32;
    result->failure_reason = (uint16_t)failure;
    if (failure == AI_RESULT_FAILURE_RESOURCE_LIMIT)
    {
        result->quality_flags |= AI_RESULT_QUALITY_RESOURCE_LIMIT;
    }
    if (prediction != RT_NULL && status == AI_RESULT_STATUS_PREDICTION)
    {
        result->class_index = prediction->class_index;
        result->class_count = model == RT_NULL ? 0U : model->class_count;
        result->confidence = prediction->confidence;
        memcpy(result->logits, prediction->logits, sizeof(result->logits));
    }
}
#endif

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static void ai_inference_build_item_result(
    const ai_model_t *model,
    const ai_inference_item_t *item,
    ai_result_status_t status,
    ai_result_failure_t failure,
    const ai_prediction_t *prediction,
    ai_result_t *result)
{
    if (item == RT_NULL)
    {
        ai_inference_build_result(model, RT_NULL, status, failure,
                                  prediction, result);
        return;
    }
    if (item->has_precomputed_features)
    {
        ai_inference_build_compact_result(
            model, item->event_id, item->precomputed_event_flags,
            item->precomputed_sample_count, status, failure, prediction,
            result);
        return;
    }
    ai_inference_build_result(model, &item->event, status, failure,
                              prediction, result);
}
#endif

static void ai_inference_publish_result_locked(ai_result_t *result)
{
    if (result == RT_NULL || result->event_id == 0U
        || ai_next_result_sequence == 0U)
    {
        return;
    }
    if (result->result_sequence == 0U)
    {
        result->result_sequence = ai_next_result_sequence;
    }
    ai_latest_result = *result;
    ai_latest_result_valid = RT_TRUE;
    if (result->result_sequence == UINT32_MAX)
    {
        ai_next_result_sequence = 0U;
    }
    else if (result->result_sequence >= ai_next_result_sequence)
    {
        ai_next_result_sequence = result->result_sequence + 1U;
    }
}

static int ai_inference_persist_result(const ai_result_t *result)
{
    ai_result_sidecar_t *sidecar;
    int store_result;

    if (result == RT_NULL)
    {
        return AI_RESULT_SIDECAR_INVALID_ARGUMENT;
    }
    if (ai_inference_lock() != RT_EOK)
    {
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    sidecar = ai_result_sidecar;
    ai_inference_unlock();
    if (sidecar == RT_NULL)
    {
        if (ai_inference_lock() == RT_EOK)
        {
            ai_stats.result_store_error_count++;
            ai_inference_unlock();
        }
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    if (ai_result_store_lock() != RT_EOK)
    {
        return AI_RESULT_SIDECAR_STORAGE_ERROR;
    }
    store_result = ai_result_sidecar_append(sidecar, result);
    ai_result_store_unlock();
    if (store_result != AI_RESULT_SIDECAR_OK
        && ai_inference_lock() == RT_EOK)
    {
        ai_stats.result_store_error_count++;
        ai_inference_unlock();
    }
    return store_result;
}

static void ai_inference_finish_persisted_result(const ai_result_t *result,
                                                 int store_result)
{
    if (result == RT_NULL || ai_inference_lock() != RT_EOK)
    {
        return;
    }
    if (ai_result_persist_in_flight && store_result == AI_RESULT_SIDECAR_OK
        && ai_result_queue_count != 0U
        && ai_result_queue[ai_result_queue_head].event_id == result->event_id
        && ai_result_queue[ai_result_queue_head].result_sequence
           == result->result_sequence)
    {
        ai_result_queue_head = (uint8_t)(
            (ai_result_queue_head + 1U) % AI_RESULT_PERSIST_QUEUE_CAPACITY);
        ai_result_queue_count--;
        ai_stats.result_queue_depth = ai_result_queue_count;
    }
    ai_result_persist_in_flight = RT_FALSE;
    ai_inference_reconcile_power_blocker_locked();
    ai_inference_unlock();
}

static rt_bool_t ai_inference_wait_for_result_queue_slot(void)
{
    for (;;)
    {
        ai_result_t pending;
        rt_bool_t have_pending = RT_FALSE;

        if (ai_inference_lock() != RT_EOK)
        {
            return RT_FALSE;
        }
        if (ai_result_queue_count < AI_RESULT_PERSIST_QUEUE_CAPACITY)
        {
            ai_inference_reconcile_power_blocker_locked();
            ai_inference_unlock();
            return RT_TRUE;
        }
        if (!ai_result_persist_in_flight)
        {
            pending = ai_result_queue[ai_result_queue_head];
            ai_result_persist_in_flight = RT_TRUE;
            ai_inference_reconcile_power_blocker_locked();
            have_pending = RT_TRUE;
        }
        ai_inference_unlock();
        if (have_pending)
        {
            int store_result = ai_inference_persist_result(&pending);

            ai_inference_finish_persisted_result(&pending, store_result);
            if (store_result != AI_RESULT_SIDECAR_OK)
            {
                rt_thread_mdelay(AI_RESULT_PERSIST_RETRY_DELAY_MS);
            }
        }
        else
        {
            rt_thread_mdelay(AI_RESULT_PERSIST_RETRY_DELAY_MS);
        }
    }
}

static rt_err_t ai_inference_queue_result_locked(const ai_result_t *result)
{
    uint8_t slot;

    if (result == RT_NULL || result->event_id == 0U
        || ai_result_queue_count >= AI_RESULT_PERSIST_QUEUE_CAPACITY)
    {
        return -RT_ERROR;
    }
    slot = (uint8_t)((ai_result_queue_head + ai_result_queue_count)
                     % AI_RESULT_PERSIST_QUEUE_CAPACITY);
    ai_result_queue[slot] = *result;
    ai_result_queue_count++;
    ai_stats.result_queue_depth = ai_result_queue_count;
    if (ai_result_queue_count > ai_stats.result_queue_high_watermark)
    {
        ai_stats.result_queue_high_watermark = ai_result_queue_count;
    }
    ai_inference_reconcile_power_blocker_locked();
    return RT_EOK;
}

static rt_bool_t ai_inference_find_queued_result_locked(uint32_t event_id,
                                                        ai_result_t *result)
{
    uint8_t offset;
    rt_bool_t found = RT_FALSE;

    if (event_id == 0U || result == RT_NULL)
    {
        return RT_FALSE;
    }
    for (offset = 0U; offset < ai_result_queue_count; offset++)
    {
        uint8_t slot = (uint8_t)((ai_result_queue_head + offset)
                                 % AI_RESULT_PERSIST_QUEUE_CAPACITY);

        if (ai_result_queue[slot].event_id == event_id
            && (!found || ai_result_queue[slot].result_sequence
                              > result->result_sequence))
        {
            *result = ai_result_queue[slot];
            found = RT_TRUE;
        }
    }
    return found;
}

static void ai_inference_record_result(const ai_result_t *result)
{
    ai_result_t local;

    if (result == RT_NULL || result->event_id == 0U)
    {
        if (ai_inference_lock() == RT_EOK)
        {
            ai_worker_event_in_flight = RT_FALSE;
            ai_inference_reconcile_power_blocker_locked();
            ai_inference_unlock();
        }
        return;
    }
    local = *result;
    if (ai_inference_wait_for_result_queue_slot() != RT_TRUE)
    {
        if (ai_inference_lock() == RT_EOK)
        {
            ai_worker_event_in_flight = RT_FALSE;
            ai_inference_reconcile_power_blocker_locked();
            ai_inference_unlock();
        }
        return;
    }
    if (ai_inference_lock() != RT_EOK)
    {
        return;
    }
    ai_inference_publish_result_locked(&local);
    if (ai_inference_queue_result_locked(&local) != RT_EOK)
    {
        /* The failure admission limit reserves enough slots for the current
         * worker result and every queued event. Reaching this branch means
         * that invariant changed without its capacity proof being updated. */
        ai_stats.result_queue_drop_count++;
        ai_stats.result_queue_backpressure_count++;
    }
    ai_worker_event_in_flight = RT_FALSE;
    ai_inference_reconcile_power_blocker_locked();
    ai_inference_unlock();
}

static rt_err_t ai_inference_retain_event(sample_block_pool_t *pool,
                                          const event_record_t *event)
{
    uint8_t block_index;

    if (pool == RT_NULL || event == RT_NULL
        || ai_inference_event_sample_count(event) != AI_EVENT_SAMPLE_COUNT)
    {
        return -RT_ERROR;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        if (sample_block_pool_retain(pool, event->blocks[block_index]) != RT_EOK)
        {
            while (block_index != 0U)
            {
                block_index--;
                (void)sample_block_pool_release(pool, event->blocks[block_index]);
            }
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
static rt_bool_t ai_inference_prepare_compact_item(
    const event_record_t *event,
    ai_feature_vector_t *features)
{
    return event != RT_NULL && features != RT_NULL
               && ai_features_extract_event(event, features) == RT_EOK;
}
#endif

static void ai_inference_release_event(sample_block_pool_t *pool,
                                       const event_record_t *event)
{
    uint8_t block_index;

    if (pool == RT_NULL || event == RT_NULL)
    {
        return;
    }
    for (block_index = 0U; block_index < event->block_count; block_index++)
    {
        if (event->blocks[block_index] != RT_NULL)
        {
            (void)sample_block_pool_release(pool, event->blocks[block_index]);
        }
    }
}

static void ai_inference_worker_step(void)
{
    ai_inference_item_t item;
    ai_result_t persisted_result;

    /* Keep iteration-local worker state scoped as it was inside the loop. */
    {
        rt_bool_t have_item = RT_FALSE;
        rt_bool_t have_persisted_result = RT_FALSE;

        if (ai_inference_lock() == RT_EOK)
        {
            rt_bool_t event_dequeue_allowed =
                ai_inference_event_dequeue_allowed_locked();

            if (event_dequeue_allowed && ai_queue_count != 0U
                && (ai_result_queue_count == 0U || ai_worker_prefer_event))
            {
                item = ai_queue[ai_queue_head];
                ai_queue_head = (uint8_t)((ai_queue_head + 1U)
                                           % AI_INFERENCE_QUEUE_CAPACITY);
                ai_queue_count--;
                ai_worker_event_in_flight = RT_TRUE;
                have_item = RT_TRUE;
                ai_worker_prefer_event = RT_FALSE;
            }
            else if (ai_result_queue_count != 0U)
            {
                if (!ai_result_persist_in_flight)
                {
                    persisted_result = ai_result_queue[ai_result_queue_head];
                    ai_result_persist_in_flight = RT_TRUE;
                    have_persisted_result = RT_TRUE;
                    ai_worker_prefer_event = RT_TRUE;
                }
            }
            else if (event_dequeue_allowed && ai_queue_count != 0U)
            {
                item = ai_queue[ai_queue_head];
                ai_queue_head = (uint8_t)((ai_queue_head + 1U)
                                           % AI_INFERENCE_QUEUE_CAPACITY);
                ai_queue_count--;
                ai_worker_event_in_flight = RT_TRUE;
                have_item = RT_TRUE;
                ai_worker_prefer_event = RT_FALSE;
            }
            ai_inference_reconcile_power_blocker_locked();
            ai_inference_unlock();
        }
        if (have_persisted_result)
        {
            int store_result = ai_inference_persist_result(&persisted_result);

            ai_inference_finish_persisted_result(&persisted_result,
                                                 store_result);
            if (store_result != AI_RESULT_SIDECAR_OK)
            {
                rt_thread_mdelay(AI_RESULT_PERSIST_RETRY_DELAY_MS);
            }
        }
        else if (have_item)
        {
            ai_feature_vector_t features;
            ai_prediction_t prediction = {0};
            rt_err_t feature_result;

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
            if (item.has_precomputed_features)
            {
                features = item.precomputed_features;
                feature_result = RT_EOK;
            }
            else
#endif
            {
                feature_result = ai_features_extract_event(&item.event,
                                                             &features);
            }
            if (feature_result != RT_EOK)
            {
                ai_result_t result;

                if (ai_inference_model_lock() == RT_EOK)
                {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
                    ai_inference_build_item_result(
                        ai_model, &item, AI_RESULT_STATUS_FEATURE_ERROR,
                        AI_RESULT_FAILURE_FEATURE_EXTRACTION, RT_NULL, &result);
#else
                    ai_inference_build_result(
                        ai_model, &item.event, AI_RESULT_STATUS_FEATURE_ERROR,
                        AI_RESULT_FAILURE_FEATURE_EXTRACTION, RT_NULL, &result);
#endif
                    ai_stats.feature_error_count++;
                    ai_inference_unlock();
                }
                else
                {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
                    ai_inference_build_item_result(
                        RT_NULL, &item, AI_RESULT_STATUS_FEATURE_ERROR,
                        AI_RESULT_FAILURE_FEATURE_EXTRACTION, RT_NULL, &result);
#else
                    ai_inference_build_result(
                        RT_NULL, &item.event, AI_RESULT_STATUS_FEATURE_ERROR,
                        AI_RESULT_FAILURE_FEATURE_EXTRACTION, RT_NULL, &result);
#endif
                }
                ai_inference_record_result(&result);
            }
            else
            {
                ai_result_t result;

                if (ai_inference_model_lock() == RT_EOK)
                {
                    const ai_model_t *model = ai_model;

                    if (model != RT_NULL
                        && ai_runtime_infer(model, features.values,
                                            &prediction) == RT_EOK)
                    {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
                        ai_inference_build_item_result(
                            model, &item, AI_RESULT_STATUS_PREDICTION,
                            AI_RESULT_FAILURE_NONE, &prediction, &result);
#else
                        ai_inference_build_result(
                            model, &item.event, AI_RESULT_STATUS_PREDICTION,
                            AI_RESULT_FAILURE_NONE, &prediction, &result);
#endif
                        ai_stats.processed_count++;
                        ai_stats.last_event_id = item.event_id;
                        ai_stats.last_class_index = prediction.class_index;
                        ai_stats.last_confidence = prediction.confidence;
                    }
                    else
                    {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
                        ai_inference_build_item_result(
                            model, &item, AI_RESULT_STATUS_RUNTIME_ERROR,
                            AI_RESULT_FAILURE_RUNTIME, RT_NULL, &result);
#else
                        ai_inference_build_result(
                            model, &item.event, AI_RESULT_STATUS_RUNTIME_ERROR,
                            AI_RESULT_FAILURE_RUNTIME, RT_NULL, &result);
#endif
                        ai_stats.runtime_error_count++;
                    }
                    ai_inference_unlock();
                }
                else
                {
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
                    ai_inference_build_item_result(
                        RT_NULL, &item, AI_RESULT_STATUS_RUNTIME_ERROR,
                        AI_RESULT_FAILURE_RUNTIME, RT_NULL, &result);
#else
                    ai_inference_build_result(
                        RT_NULL, &item.event, AI_RESULT_STATUS_RUNTIME_ERROR,
                        AI_RESULT_FAILURE_RUNTIME, RT_NULL, &result);
#endif
                }
                ai_inference_record_result(&result);
            }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
            if (!item.has_precomputed_features)
#endif
            {
                ai_inference_release_event(item.pool, &item.event);
            }
        }
        else
        {
            rt_thread_mdelay(10U);
        }
    }
}

static void ai_inference_worker(void *parameter)
{
    (void)parameter;
    while (RT_TRUE)
    {
        ai_inference_worker_step();
    }
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
rt_err_t ai_inference_service_test_worker_step(void)
{
    if (!ai_service_started)
    {
        return -RT_ERROR;
    }
    ai_inference_worker_step();
    return RT_EOK;
}
#endif

rt_err_t ai_inference_service_start(const ai_model_t *model)
{
    rt_thread_t thread;

    if (ai_service_started)
    {
        rt_bool_t model_ready;

        if (ai_inference_lock() != RT_EOK)
        {
            return -RT_ERROR;
        }
        if (ai_model_quarantined)
        {
            ai_inference_unlock();
            return -RT_ERROR;
        }
        model_ready = ai_stats.model_ready;
        ai_inference_unlock();
        if (!model_ready && model != RT_NULL)
        {
            return ai_inference_service_set_model(model);
        }
        return RT_EOK;
    }
    if (!ai_mutex_initialized)
    {
        if (rt_mutex_init(&ai_mutex, "ai", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            return -RT_ERROR;
        }
        ai_mutex_initialized = RT_TRUE;
    }
    if (!ai_result_store_mutex_initialized)
    {
        if (rt_mutex_init(&ai_result_store_mutex, "airs", RT_IPC_FLAG_PRIO)
            != RT_EOK)
        {
            return -RT_ERROR;
        }
        ai_result_store_mutex_initialized = RT_TRUE;
    }
    memset(ai_queue, 0, sizeof(ai_queue));
    memset(ai_result_queue, 0, sizeof(ai_result_queue));
    memset(&ai_stats, 0, sizeof(ai_stats));
    memset(&ai_latest_result, 0, sizeof(ai_latest_result));
    ai_latest_result_valid = RT_FALSE;
    ai_next_result_sequence = 1U;
    ai_queue_head = 0U;
    ai_queue_count = 0U;
    ai_worker_event_in_flight = RT_FALSE;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_queue_pressure_active = RT_FALSE;
    ai_queue_pressure_started_tick = 0U;
#endif
    ai_result_queue_head = 0U;
    ai_result_queue_count = 0U;
    ai_result_persist_in_flight = RT_FALSE;
    ai_power_blocker_held = RT_FALSE;
    ai_worker_prefer_event = RT_TRUE;
    ai_model_transition_prepared = RT_FALSE;
    ai_model_quarantined = RT_FALSE;
    ai_model = model;
    ai_stats.model_ready = model != RT_NULL
                           && ai_runtime_validate_model(model) == RT_EOK;
    thread = rt_thread_create("ai", ai_inference_worker, RT_NULL,
                              AI_INFERENCE_THREAD_STACK_SIZE,
                              AI_INFERENCE_THREAD_PRIORITY,
                              AI_INFERENCE_THREAD_TICK);
    if (thread == RT_NULL)
    {
        ai_service_started = RT_FALSE;
        return -RT_ERROR;
    }
    ai_service_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

rt_err_t ai_inference_service_prepare_model(const ai_model_t *model)
{
    if (!ai_service_started || model == RT_NULL
        || ai_runtime_validate_model(model) != RT_EOK
        || ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (ai_model_quarantined || ai_model_transition_prepared)
    {
        ai_inference_unlock();
        return -RT_ERROR;
    }
    ai_model_transition_prepared = RT_TRUE;
    ai_inference_unlock();
    return RT_EOK;
}

rt_err_t ai_inference_service_publish_model(const ai_model_t *model)
{
    if (ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (ai_model_quarantined || !ai_model_transition_prepared)
    {
        ai_inference_unlock();
        return -RT_ERROR;
    }
    if (model == RT_NULL)
    {
        ai_model_transition_prepared = RT_FALSE;
        ai_inference_unlock();
        return -RT_ERROR;
    }
    ai_model = model;
    ai_stats.model_ready = RT_TRUE;
    ai_model_transition_prepared = RT_FALSE;
    ai_inference_unlock();
    return RT_EOK;
}

void ai_inference_service_abort_model(void)
{
    if (ai_inference_lock() != RT_EOK)
    {
        return;
    }
    if (!ai_model_transition_prepared)
    {
        ai_inference_unlock();
        return;
    }
    ai_model_transition_prepared = RT_FALSE;
    ai_inference_unlock();
}

void ai_inference_service_quarantine_model(void)
{
    if (ai_inference_lock() != RT_EOK)
    {
        return;
    }
    ai_model_quarantined = RT_TRUE;
    ai_model = RT_NULL;
    ai_stats.model_ready = RT_FALSE;
    ai_model_transition_prepared = RT_FALSE;
    ai_inference_unlock();
}

rt_err_t ai_inference_service_set_model(const ai_model_t *model)
{
    if (ai_inference_service_prepare_model(model) != RT_EOK)
    {
        return -RT_ERROR;
    }
    return ai_inference_service_publish_model(model);
}

rt_err_t ai_inference_service_set_result_sidecar(ai_result_sidecar_t *sidecar)
{
    ai_result_t latest;
    int latest_status;

    if (sidecar == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (ai_result_store_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    latest_status = ai_result_sidecar_get_latest(sidecar, &latest);
    ai_result_store_unlock();
    if (ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    ai_result_sidecar = sidecar;
    if (latest_status == AI_RESULT_SIDECAR_OK)
    {
        ai_latest_result = latest;
        ai_latest_result_valid = RT_TRUE;
        if (latest.result_sequence != UINT32_MAX)
        {
            ai_next_result_sequence = latest.result_sequence + 1U;
        }
    }
    ai_inference_unlock();
    return RT_EOK;
}

rt_err_t ai_inference_service_get_result_store_status(void)
{
    ai_result_sidecar_t *sidecar;

    if (ai_inference_lock() != RT_EOK)
    {
        return AI_INFERENCE_RESULT_STORAGE_ERROR;
    }
    sidecar = ai_result_sidecar;
    ai_inference_unlock();
    return sidecar != RT_NULL ? RT_EOK : AI_INFERENCE_RESULT_STORAGE_ERROR;
}

static rt_err_t ai_inference_publish_failure_locked(const event_record_t *event,
                                                     ai_result_status_t status,
                                                     ai_result_failure_t failure)
{
    ai_result_t result;

    if (event == RT_NULL || event->event_id == 0U)
    {
        return -RT_ERROR;
    }
    if (ai_result_queue_count >= AI_RESULT_PERSIST_FAILURE_CAPACITY)
    {
        ai_stats.result_queue_drop_count++;
        ai_stats.result_queue_backpressure_count++;
        return -RT_ERROR;
    }
    ai_inference_build_result(ai_model, event, status, failure, RT_NULL,
                              &result);
    ai_inference_publish_result_locked(&result);
    if (ai_inference_queue_result_locked(&result) != RT_EOK)
    {
        ai_stats.result_queue_drop_count++;
        ai_stats.result_queue_backpressure_count++;
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t ai_inference_service_submit_event(sample_block_pool_t *pool,
                                           const event_record_t *event)
{
    uint8_t slot;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    rt_bool_t compact_item = RT_FALSE;
    ai_feature_vector_t compact_features = {0};
#endif

    if (event == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (!ai_service_started
        && ai_inference_service_start(ai_model) != RT_EOK)
    {
        return -RT_ERROR;
    }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (ai_inference_queue_pressure_snapshot())
    {
        compact_item = ai_inference_prepare_compact_item(event,
                                                          &compact_features);
    }
#endif
    if (ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_inference_queue_pressure_expire_locked();
#endif
    if (!ai_stats.model_ready)
    {
        ai_inference_publish_failure_locked(event,
                                             AI_RESULT_STATUS_MODEL_UNAVAILABLE,
                                             AI_RESULT_FAILURE_NO_MODEL);
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        ai_inference_queue_pressure_cancel_locked();
#endif
        ai_inference_unlock();
        return -RT_ERROR;
    }
    if (pool == RT_NULL)
    {
        ai_inference_publish_failure_locked(event,
                                             AI_RESULT_STATUS_RESOURCE_LIMIT,
                                             AI_RESULT_FAILURE_RESOURCE_LIMIT);
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        ai_inference_queue_pressure_cancel_locked();
#endif
        ai_inference_unlock();
        return -RT_ERROR;
    }
    if (ai_queue_count >= AI_INFERENCE_QUEUE_CAPACITY)
    {
        ai_stats.queue_drop_count++;
        ai_inference_publish_failure_locked(event,
                                             AI_RESULT_STATUS_RESOURCE_LIMIT,
                                             AI_RESULT_FAILURE_RESOURCE_LIMIT);
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        ai_inference_queue_pressure_cancel_locked();
#endif
        ai_inference_unlock();
        return -RT_ERROR;
    }
    if (ai_result_queue_count + ai_queue_count
        + (ai_worker_event_in_flight ? 1U : 0U)
        >= AI_RESULT_PERSIST_QUEUE_CAPACITY)
    {
        ai_stats.queue_drop_count++;
        ai_inference_publish_failure_locked(event,
                                             AI_RESULT_STATUS_RESOURCE_LIMIT,
                                             AI_RESULT_FAILURE_RESOURCE_LIMIT);
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        ai_inference_queue_pressure_cancel_locked();
#endif
        ai_inference_unlock();
        return -RT_ERROR;
    }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (!ai_queue_pressure_active)
    {
        compact_item = RT_FALSE;
    }
#endif
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    if (!compact_item && ai_inference_retain_event(pool, event) != RT_EOK)
#else
    if (ai_inference_retain_event(pool, event) != RT_EOK)
#endif
    {
        ai_inference_publish_failure_locked(event,
                                             AI_RESULT_STATUS_RESOURCE_LIMIT,
                                             AI_RESULT_FAILURE_RESOURCE_LIMIT);
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
        ai_inference_queue_pressure_cancel_locked();
#endif
        ai_inference_unlock();
        return -RT_ERROR;
    }
    slot = (uint8_t)((ai_queue_head + ai_queue_count)
                     % AI_INFERENCE_QUEUE_CAPACITY);
    ai_queue[slot].event_id = event->event_id;
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_queue[slot].pool = compact_item ? RT_NULL : pool;
#else
    ai_queue[slot].pool = pool;
#endif
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_queue[slot].has_precomputed_features = compact_item;
    if (compact_item)
    {
        memset(&ai_queue[slot].event, 0, sizeof(ai_queue[slot].event));
        ai_queue[slot].precomputed_features = compact_features;
        ai_queue[slot].precomputed_sample_count = AI_EVENT_SAMPLE_COUNT;
        ai_queue[slot].precomputed_event_flags = event->flags;
        ai_queue[slot].event.event_id = event->event_id;
        ai_queue[slot].event.flags = event->flags;
    }
    else
    {
        ai_queue[slot].event = *event;
    }
#else
    ai_queue[slot].event = *event;
#endif
    ai_queue_count++;
    ai_stats.submitted_count++;
    ai_inference_reconcile_power_blocker_locked();
    ai_inference_unlock();
    return RT_EOK;
}

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
rt_err_t ai_inference_service_arm_queue_pressure(void)
{
    if (!ai_service_started || ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    ai_inference_queue_pressure_expire_locked();
    if (!ai_stats.model_ready || ai_queue_pressure_active
        || ai_queue_count != 0U || ai_worker_event_in_flight
        || ai_result_queue_count != 0U || ai_result_persist_in_flight)
    {
        ai_inference_unlock();
        return -RT_EBUSY;
    }
    ai_queue_pressure_active = RT_TRUE;
    ai_queue_pressure_started_tick = rt_tick_get();
    ai_inference_reconcile_power_blocker_locked();
    ai_inference_unlock();
    return RT_EOK;
}

void ai_inference_service_cancel_queue_pressure(void)
{
    if (ai_inference_lock() != RT_EOK)
    {
        return;
    }
    ai_inference_queue_pressure_cancel_locked();
    ai_inference_reconcile_power_blocker_locked();
    ai_inference_unlock();
}

rt_bool_t ai_inference_service_queue_pressure_active(void)
{
    rt_bool_t active = RT_FALSE;

    if (ai_inference_lock() != RT_EOK)
    {
        return RT_FALSE;
    }
    ai_inference_queue_pressure_expire_locked();
    active = ai_queue_pressure_active;
    ai_inference_unlock();
    return active;
}
#endif

void ai_inference_service_get_stats(ai_inference_stats_t *stats)
{
    if (stats == RT_NULL)
    {
        return;
    }
    if (ai_inference_lock() != RT_EOK)
    {
        *stats = ai_stats;
        return;
    }
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    ai_inference_queue_pressure_expire_locked();
#endif
    *stats = ai_stats;
    ai_inference_unlock();
}

rt_err_t ai_inference_service_get_result(uint32_t event_id,
                                         ai_result_t *result)
{
    ai_result_t stored;
    ai_result_t latest;
    ai_result_t queued;
    ai_result_sidecar_t *sidecar;
    int sidecar_status;
    rt_bool_t latest_available;

    if (result == RT_NULL || ai_inference_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    latest_available = ai_latest_result_valid
                      && (event_id == 0U || ai_latest_result.event_id == event_id);
    if (latest_available)
    {
        latest = ai_latest_result;
    }
    else if (event_id != 0U
             && ai_inference_find_queued_result_locked(event_id, &queued))
    {
        latest = queued;
        latest_available = RT_TRUE;
    }
    sidecar = ai_result_sidecar;
    ai_inference_unlock();
    if (latest_available)
    {
        *result = latest;
        return RT_EOK;
    }
    if (sidecar != RT_NULL && ai_result_store_lock() == RT_EOK)
    {
        sidecar_status = event_id == 0U
                         ? ai_result_sidecar_get_latest(sidecar, &stored)
                         : ai_result_sidecar_get_event(sidecar, event_id, &stored);
        ai_result_store_unlock();
        if (sidecar_status == AI_RESULT_SIDECAR_OK)
        {
            *result = stored;
            return RT_EOK;
        }
        if (sidecar_status == AI_RESULT_SIDECAR_STORAGE_ERROR)
        {
            return AI_INFERENCE_RESULT_STORAGE_ERROR;
        }
    }
    return AI_INFERENCE_RESULT_NOT_FOUND;
}
