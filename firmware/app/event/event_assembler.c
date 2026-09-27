#include "event_assembler.h"

#include <string.h>

_Static_assert(SAMPLE_BLOCK_POOL_SIZE >= EVENT_MAX_BLOCK_COUNT
                   + SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT
                   + SAMPLE_BLOCK_HANDOFF_COUNT,
               "sample block pool must cover event, export, and handoff headroom");

static rt_err_t event_assembler_release_event(event_assembler_t *assembler,
                                              sample_block_pool_t *pool)
{
    uint8_t index;
    rt_err_t result = RT_EOK;

    for (index = 0U; index < assembler->event.block_count; index++)
    {
        if (assembler->event.blocks[index] != RT_NULL
            && sample_block_pool_release(pool, assembler->event.blocks[index]) != RT_EOK)
        {
            result = -RT_ERROR;
        }
    }

    memset(&assembler->event, 0, sizeof(assembler->event));
    assembler->posttrigger_target_blocks = 0U;
    return result;
}

static rt_err_t event_assembler_abort_for_resource_limit(event_assembler_t *assembler,
                                                          sample_block_pool_t *pool,
                                                          sample_block_t *block)
{
    (void)sample_block_pool_release(pool, block);
    (void)event_assembler_release_event(assembler, pool);
    assembler->state = EVENT_ARMED;
    assembler->resource_reject_count++;
    return -RT_ERROR;
}

static rt_err_t event_assembler_push_history(event_assembler_t *assembler,
                                             sample_block_pool_t *pool,
                                             sample_block_t *block)
{
    if (pretrigger_ring_push(pool, &assembler->pretrigger, block) != RT_EOK)
    {
        (void)sample_block_pool_release(pool, block);
        return -RT_ERROR;
    }

    return sample_block_pool_release(pool, block);
}

static rt_err_t event_assembler_start(event_assembler_t *assembler,
                                      sample_block_pool_t *pool,
                                      sample_block_t *block,
                                      const trigger_fact_t *trigger)
{
    uint8_t pretrigger_count;
    uint64_t trigger_offset_us;

    if (trigger->sample_index >= block->sample_count)
    {
        (void)sample_block_pool_release(pool, block);
        return -RT_ERROR;
    }

    if (sample_block_pool_free_count(pool) < SAMPLE_BLOCK_HANDOFF_COUNT)
    {
        assembler->resource_reject_count++;
        (void)sample_block_pool_release(pool, block);
        return -RT_ERROR;
    }

    memset(&assembler->event, 0, sizeof(assembler->event));
    pretrigger_count = pretrigger_ring_snapshot(pool, &assembler->pretrigger,
                                                assembler->event.blocks,
                                                EVENT_MAX_BLOCK_COUNT);
    if (pretrigger_count != assembler->pretrigger.count
        || pretrigger_ring_reset(pool, &assembler->pretrigger) != RT_EOK)
    {
        assembler->event.block_count = pretrigger_count;
        (void)event_assembler_release_event(assembler, pool);
        (void)sample_block_pool_release(pool, block);
        return -RT_ERROR;
    }

    assembler->event.event_id = ++assembler->next_event_id;
    trigger_offset_us = ((uint64_t)trigger->sample_index
                         * block->sample_period_ns) / 1000U;
    assembler->event.trigger_monotonic_us =
        block->first_monotonic_us > UINT64_MAX - trigger_offset_us
            ? UINT64_MAX
            : block->first_monotonic_us + trigger_offset_us;
    assembler->event.trigger_sequence = block->sequence + trigger->sample_index;
    assembler->event.trigger_sample_index = trigger->sample_index;
    assembler->event.peak_magnitude_sq = trigger->magnitude_sq;
    assembler->event.threshold_magnitude_sq = trigger->threshold_magnitude_sq;
    assembler->event.pretrigger_block_count = pretrigger_count;
    assembler->event.posttrigger_block_count = 1U;
    assembler->event.block_count = (uint8_t)(pretrigger_count + 1U);
    assembler->event.blocks[pretrigger_count] = block;
    if (pretrigger_count < EVENT_PRETRIGGER_BLOCK_COUNT)
    {
        assembler->event.flags |= EVENT_FLAG_PRETRIGGER_SHORT;
    }

    assembler->posttrigger_target_blocks = EVENT_STANDARD_POST_BLOCK_COUNT;
    assembler->state = EVENT_POST_TRIGGER_CAPTURING;
    return RT_EOK;
}

static void event_assembler_record_subtrigger(event_assembler_t *assembler,
                                              const trigger_fact_t *trigger)
{
    uint16_t requested_posttrigger_blocks;

    assembler->event.subtrigger_count++;
    if (trigger->magnitude_sq > assembler->event.peak_magnitude_sq)
    {
        assembler->event.peak_magnitude_sq = trigger->magnitude_sq;
    }

    requested_posttrigger_blocks = (uint16_t)assembler->event.posttrigger_block_count
                                   + EVENT_STANDARD_POST_BLOCK_COUNT;
    if (requested_posttrigger_blocks > EVENT_MAX_POST_BLOCK_COUNT)
    {
        assembler->posttrigger_target_blocks = EVENT_MAX_POST_BLOCK_COUNT;
        assembler->event.flags |= EVENT_FLAG_DURATION_CAPPED;
    }
    else if (requested_posttrigger_blocks > assembler->posttrigger_target_blocks)
    {
        assembler->posttrigger_target_blocks = (uint8_t)requested_posttrigger_blocks;
    }
}

static rt_err_t event_assembler_capture(event_assembler_t *assembler,
                                        sample_block_pool_t *pool,
                                        sample_block_t *block,
                                        const trigger_fact_t *trigger)
{
    if (sample_block_pool_free_count(pool) < SAMPLE_BLOCK_HANDOFF_COUNT
        || assembler->event.block_count >= EVENT_MAX_BLOCK_COUNT)
    {
        return event_assembler_abort_for_resource_limit(assembler, pool, block);
    }

    assembler->event.blocks[assembler->event.block_count++] = block;
    assembler->event.posttrigger_block_count++;
    if (trigger != RT_NULL)
    {
        event_assembler_record_subtrigger(assembler, trigger);
    }

    if (assembler->event.posttrigger_block_count
        >= assembler->posttrigger_target_blocks)
    {
        assembler->state = EVENT_READY_FOR_EXPORT;
    }

    return RT_EOK;
}

void event_assembler_init(event_assembler_t *assembler)
{
    if (assembler != RT_NULL)
    {
        memset(assembler, 0, sizeof(*assembler));
        pretrigger_ring_init(&assembler->pretrigger);
    }
}

void event_assembler_set_next_event_id(event_assembler_t *assembler,
                                       uint32_t next_event_id)
{
    if (assembler != RT_NULL && assembler->state == EVENT_ARMED
        && next_event_id != 0U)
    {
        assembler->next_event_id = next_event_id - 1U;
    }
}

void event_assembler_set_health_snapshot(event_assembler_t *assembler,
                                         const health_snapshot_t *snapshot)
{
    if (assembler != RT_NULL && snapshot != RT_NULL
        && (assembler->state == EVENT_POST_TRIGGER_CAPTURING
            || assembler->state == EVENT_READY_FOR_EXPORT
            || assembler->state == EVENT_EXPORTING))
    {
        assembler->event.health_snapshot = *snapshot;
    }
}

rt_err_t event_assembler_consume(event_assembler_t *assembler,
                                 sample_block_pool_t *pool,
                                 sample_block_t *block,
                                 const trigger_fact_t *trigger_or_null)
{
    if (assembler == RT_NULL || pool == RT_NULL || block == RT_NULL)
    {
        return -RT_ERROR;
    }

    switch (assembler->state)
    {
    case EVENT_ARMED:
        if (trigger_or_null == RT_NULL)
        {
            return event_assembler_push_history(assembler, pool, block);
        }
        return event_assembler_start(assembler, pool, block, trigger_or_null);

    case EVENT_POST_TRIGGER_CAPTURING:
        return event_assembler_capture(assembler, pool, block, trigger_or_null);

    case EVENT_EXPORTING:
        if (trigger_or_null != RT_NULL)
        {
            assembler->busy_trigger_count++;
        }
        return sample_block_pool_release(pool, block);

    case EVENT_READY_FOR_EXPORT:
    default:
        return sample_block_pool_release(pool, block);
    }
}

const event_record_t *event_assembler_event(const event_assembler_t *assembler)
{
    if (assembler == RT_NULL || (assembler->state != EVENT_READY_FOR_EXPORT
                                 && assembler->state != EVENT_EXPORTING))
    {
        return RT_NULL;
    }

    return &assembler->event;
}

rt_err_t event_assembler_begin_export(event_assembler_t *assembler)
{
    if (assembler == RT_NULL || assembler->state != EVENT_READY_FOR_EXPORT)
    {
        return -RT_ERROR;
    }

    assembler->state = EVENT_EXPORTING;
    return RT_EOK;
}

rt_err_t event_assembler_finish_export(event_assembler_t *assembler,
                                       sample_block_pool_t *pool,
                                       int succeeded)
{
    rt_err_t result;

    (void)succeeded;
    if (assembler == RT_NULL || pool == RT_NULL || assembler->state != EVENT_EXPORTING)
    {
        return -RT_ERROR;
    }

    result = event_assembler_release_event(assembler, pool);
    assembler->state = EVENT_ARMED;
    return result;
}

rt_err_t event_assembler_clear(event_assembler_t *assembler,
                               sample_block_pool_t *pool)
{
    rt_err_t result;

    if (assembler == RT_NULL || pool == RT_NULL
        || assembler->state != EVENT_READY_FOR_EXPORT)
    {
        return -RT_ERROR;
    }

    result = event_assembler_release_event(assembler, pool);
    assembler->state = EVENT_ARMED;
    return result;
}
