#include "event_quality.h"

#include <limits.h>
#include <string.h>

#include "event_export_format.h"
#include "imu_sample_batcher.h"

#define EVENT_QUALITY_MAX_SEQUENCE_GAP \
    (EVENT_MAX_BLOCK_COUNT * SAMPLE_BLOCK_SAMPLE_CAPACITY)

static uint32_t event_quality_reason_for_facts(
    const event_quality_facts_t *facts)
{
    uint32_t reasons = EVENT_QUALITY_REASON_NONE;

    if (facts->pretrigger_short)
    {
        reasons |= EVENT_QUALITY_REASON_PRETRIGGER_SHORT;
    }
    if (facts->duration_capped)
    {
        reasons |= EVENT_QUALITY_REASON_DURATION_CAPPED;
    }
    if ((facts->flags & EVENT_FLAG_DATA_LOSS) != 0U
        || facts->loss.lost_sample_count != 0U)
    {
        reasons |= EVENT_QUALITY_REASON_DATA_LOSS;
    }
    if (facts->loss.lost_sample_count != 0U)
    {
        reasons |= EVENT_QUALITY_REASON_SEQUENCE_GAP;
    }
    if (!facts->fixed_shape)
    {
        reasons |= EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION;
    }
    return reasons;
}

static event_quality_verdict_t event_quality_verdict_for_reasons(
    uint32_t reasons)
{
    if ((reasons & (EVENT_QUALITY_REASON_DATA_LOSS
                    | EVENT_QUALITY_REASON_SEQUENCE_GAP
                    | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION
                    | EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH))
        != 0U)
    {
        return EVENT_QUALITY_INVALID;
    }
    if ((reasons & (EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                    | EVENT_QUALITY_REASON_DURATION_CAPPED)) != 0U)
    {
        return EVENT_QUALITY_DEGRADED;
    }
    return EVENT_QUALITY_PASS;
}

rt_err_t event_quality_calculate_loss_summary(
    const event_record_t *event,
    event_quality_loss_summary_t *summary)
{
    uint8_t index;

    if (event == RT_NULL || summary == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (event->block_count > (sizeof(event->blocks)
                             / sizeof(event->blocks[0])))
    {
        return -RT_ERROR;
    }
    memset(summary, 0, sizeof(*summary));

    for (index = 1U; index < event->block_count; index++)
    {
        const sample_block_t *previous = event->blocks[index - 1U];
        const sample_block_t *current = event->blocks[index];
        uint32_t expected_sequence;
        uint32_t gap;
        uint64_t previous_span_us;
        uint64_t loss_start_us;
        uint64_t loss_duration_us;
        uint64_t loss_end_us;

        if (previous == RT_NULL || current == RT_NULL)
        {
            return -RT_ERROR;
        }
        expected_sequence = previous->sequence + previous->sample_count;
        gap = current->sequence - expected_sequence;
        if (gap == 0U)
        {
            continue;
        }
        if (gap > EVENT_QUALITY_MAX_SEQUENCE_GAP
            || summary->lost_sample_count > UINT32_MAX - gap
            || summary->loss_episode_count == UINT32_MAX)
        {
            return -RT_ERROR;
        }
        previous_span_us = ((uint64_t)previous->sample_count
                            * previous->sample_period_ns) / 1000U;
        if (previous->first_monotonic_us > UINT64_MAX - previous_span_us)
        {
            return -RT_ERROR;
        }
        loss_start_us = previous->first_monotonic_us + previous_span_us;
        loss_duration_us = ((uint64_t)(gap - 1U)
                            * previous->sample_period_ns) / 1000U;
        if (loss_start_us > UINT64_MAX - loss_duration_us)
        {
            return -RT_ERROR;
        }
        loss_end_us = loss_start_us + loss_duration_us;
        if (summary->loss_episode_count != 0U
            && (loss_start_us < summary->first_loss_monotonic_us
                || loss_start_us < summary->last_loss_monotonic_us
                || loss_end_us < summary->last_loss_monotonic_us))
        {
            return -RT_ERROR;
        }

        if (summary->loss_episode_count == 0U)
        {
            summary->first_lost_sequence = expected_sequence;
            summary->first_loss_monotonic_us = loss_start_us;
        }
        summary->lost_sample_count += gap;
        summary->last_lost_sequence = current->sequence - 1U;
        summary->loss_episode_count++;
        summary->last_loss_monotonic_us = loss_end_us;
    }

    return RT_EOK;
}

static rt_bool_t event_quality_fixed_shape(const event_record_t *event,
                                           uint32_t actual_sample_count)
{
    uint32_t expected_sample_period_ns = 0U;
    uint32_t expected_sample_count;
    rt_bool_t has_short_pretrigger;
    rt_bool_t has_capped_duration;
    uint8_t index;

    has_short_pretrigger =
        (event->flags & EVENT_FLAG_PRETRIGGER_SHORT) != 0U;
    has_capped_duration =
        (event->flags & EVENT_FLAG_DURATION_CAPPED) != 0U;
    if (event->block_count != (uint8_t)(event->pretrigger_block_count
                                        + event->posttrigger_block_count)
        || event->posttrigger_block_count > EVENT_MAX_POST_BLOCK_COUNT
        || (event->flags & EVENT_FLAG_RESOURCE_LIMIT) != 0U
        || (has_short_pretrigger
            ? event->pretrigger_block_count >= EVENT_PRETRIGGER_BLOCK_COUNT
            : event->pretrigger_block_count != EVENT_PRETRIGGER_BLOCK_COUNT)
        || (has_capped_duration
            ? event->posttrigger_block_count != EVENT_MAX_POST_BLOCK_COUNT
            : event->posttrigger_block_count
                  != EVENT_STANDARD_POST_BLOCK_COUNT))
    {
        return RT_FALSE;
    }

    for (index = 0U; index < event->block_count; index++)
    {
        const sample_block_t *block = event->blocks[index];

        if (block->sample_count != IMU_SAMPLE_BATCH_SIZE)
        {
            return RT_FALSE;
        }
        if (index == 0U)
        {
            expected_sample_period_ns = block->sample_period_ns;
        }
        else if (block->sample_period_ns != expected_sample_period_ns)
        {
            return RT_FALSE;
        }
    }

    expected_sample_count = (uint32_t)event->block_count
                            * IMU_SAMPLE_BATCH_SIZE;
    return actual_sample_count == expected_sample_count;
}

rt_err_t event_quality_collect_facts(const sample_block_pool_t *pool,
                                     const event_record_t *event,
                                     event_quality_facts_t *facts)
{
    uint64_t total_samples = 0U;
    uint64_t pretrigger_samples = 0U;
    uint8_t index;

    if (pool == RT_NULL || facts == RT_NULL)
    {
        return -RT_ERROR;
    }
    memset(facts, 0, sizeof(*facts));
    facts->serialization = EVENT_QUALITY_CAPTURE_FAILURE;
    if (event == RT_NULL || event->block_count == 0U
        || event->block_count > (sizeof(event->blocks)
                                 / sizeof(event->blocks[0]))
        || event->pretrigger_block_count >= event->block_count
        || event->posttrigger_block_count > EVENT_MAX_POST_BLOCK_COUNT
        || (event->flags
            & (uint16_t)~(EVENT_FLAG_PRETRIGGER_SHORT
                           | EVENT_FLAG_DURATION_CAPPED
                           | EVENT_FLAG_RESOURCE_LIMIT
                           | EVENT_FLAG_DATA_LOSS)) != 0U)
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < event->block_count; index++)
    {
        if (!sample_block_pool_is_valid_consuming(pool, event->blocks[index]))
        {
            return -RT_ERROR;
        }
    }

    for (index = 0U; index < event->block_count; index++)
    {
        const sample_block_t *block = event->blocks[index];

        if (block->sample_count == 0U
            || block->sample_count > SAMPLE_BLOCK_SAMPLE_CAPACITY
            || block->sample_count != IMU_SAMPLE_BATCH_SIZE
            || block->sample_period_ns == 0U
            || total_samples > UINT32_MAX - block->sample_count)
        {
            return -RT_ERROR;
        }
        total_samples += block->sample_count;
        if (index < event->pretrigger_block_count)
        {
            pretrigger_samples += block->sample_count;
        }
    }

    if (event->trigger_sample_index
        >= event->blocks[event->pretrigger_block_count]->sample_count
        || pretrigger_samples > UINT32_MAX - event->trigger_sample_index
        || pretrigger_samples + event->trigger_sample_index > total_samples)
    {
        return -RT_ERROR;
    }
    pretrigger_samples += event->trigger_sample_index;
    total_samples -= pretrigger_samples;
    if (total_samples > UINT32_MAX
        || (pretrigger_samples + total_samples)
               > UINT32_MAX / EVENT_EXPORT_SAMPLE_SIZE)
    {
        return -RT_ERROR;
    }
    if (event_quality_calculate_loss_summary(event, &facts->loss) != RT_EOK)
    {
        return -RT_ERROR;
    }
    if ((facts->loss.lost_sample_count == 0U
         && (event->flags & EVENT_FLAG_DATA_LOSS) != 0U)
        || (facts->loss.lost_sample_count != 0U
            && (event->flags & EVENT_FLAG_DATA_LOSS) == 0U))
    {
        return -RT_ERROR;
    }
    if (event->trigger_sequence
            != event->blocks[event->pretrigger_block_count]->sequence
                   + event->trigger_sample_index)
    {
        return -RT_ERROR;
    }
    {
        const sample_block_t *trigger_block =
            event->blocks[event->pretrigger_block_count];
        uint64_t trigger_offset_us =
            ((uint64_t)event->trigger_sample_index
             * trigger_block->sample_period_ns) / 1000U;
        uint64_t expected_trigger_time =
            trigger_block->first_monotonic_us > UINT64_MAX - trigger_offset_us
                ? UINT64_MAX
                : trigger_block->first_monotonic_us + trigger_offset_us;

        if (event->trigger_monotonic_us != expected_trigger_time)
        {
            return -RT_ERROR;
        }
    }

    facts->event_id = event->event_id;
    facts->trigger_sequence = event->trigger_sequence;
    facts->actual_sample_count = (uint32_t)(pretrigger_samples + total_samples);
    facts->pretrigger_samples = (uint32_t)pretrigger_samples;
    facts->posttrigger_samples = (uint32_t)total_samples;
    facts->trigger_sample_index = event->trigger_sample_index;
    facts->flags = event->flags;
    facts->block_count = event->block_count;
    facts->pretrigger_block_count = event->pretrigger_block_count;
    facts->posttrigger_block_count = event->posttrigger_block_count;
    facts->sequence_continuous = facts->loss.lost_sample_count == 0U;
    facts->fixed_shape = event_quality_fixed_shape(event,
                                                   facts->actual_sample_count);
    facts->pretrigger_short =
        ((facts->flags & EVENT_FLAG_PRETRIGGER_SHORT) != 0U
         || event->pretrigger_block_count < EVENT_PRETRIGGER_BLOCK_COUNT);
    facts->duration_capped =
        (facts->flags & EVENT_FLAG_DURATION_CAPPED) != 0U;
    facts->serialization = EVENT_QUALITY_SERIALIZABLE;
    return RT_EOK;
}

rt_err_t event_quality_assess_facts(const event_quality_facts_t *facts,
                                    event_quality_result_t *result)
{
    if (facts == RT_NULL || result == RT_NULL
        || facts->serialization != EVENT_QUALITY_SERIALIZABLE)
    {
        return -RT_ERROR;
    }

    memset(result, 0, sizeof(*result));
    result->serialization = EVENT_QUALITY_SERIALIZABLE;
    result->verdict = EVENT_QUALITY_PASS;
    result->live_facts = *facts;
    result->reason_flags = event_quality_reason_for_facts(facts);
    result->verdict = event_quality_verdict_for_reasons(result->reason_flags);
    return RT_EOK;
}

rt_err_t event_quality_evaluate(const sample_block_pool_t *pool,
                                const event_record_t *event,
                                event_quality_result_t *result)
{
    event_quality_facts_t facts;

    if (result == RT_NULL)
    {
        return -RT_ERROR;
    }
    memset(result, 0, sizeof(*result));
    result->serialization = EVENT_QUALITY_CAPTURE_FAILURE;
    result->verdict = EVENT_QUALITY_NO_VERDICT;
    if (event_quality_collect_facts(pool, event, &facts) != RT_EOK)
    {
        return RT_EOK;
    }
    return event_quality_assess_facts(&facts, result);
}

rt_bool_t event_quality_facts_equal(const event_quality_facts_t *left,
                                    const event_quality_facts_t *right)
{
    if (left == RT_NULL || right == RT_NULL)
    {
        return RT_FALSE;
    }
    return left->event_id == right->event_id
           && left->trigger_sequence == right->trigger_sequence
           && left->actual_sample_count == right->actual_sample_count
           && left->pretrigger_samples == right->pretrigger_samples
           && left->posttrigger_samples == right->posttrigger_samples
           && left->trigger_sample_index == right->trigger_sample_index
           && left->flags == right->flags
           && left->block_count == right->block_count
           && left->pretrigger_block_count == right->pretrigger_block_count
           && left->posttrigger_block_count == right->posttrigger_block_count
           && left->sequence_continuous == right->sequence_continuous
           && left->fixed_shape == right->fixed_shape
           && left->pretrigger_short == right->pretrigger_short
           && left->duration_capped == right->duration_capped
           && left->serialization == right->serialization
           && left->loss.lost_sample_count == right->loss.lost_sample_count
           && left->loss.first_lost_sequence
                  == right->loss.first_lost_sequence
           && left->loss.last_lost_sequence
                  == right->loss.last_lost_sequence
           && left->loss.loss_episode_count
                  == right->loss.loss_episode_count
           && left->loss.first_loss_monotonic_us
                  == right->loss.first_loss_monotonic_us
           && left->loss.last_loss_monotonic_us
                  == right->loss.last_loss_monotonic_us;
}

rt_bool_t event_quality_ai_eligible(const event_quality_result_t *result)
{
    if (result == RT_NULL
        || result->serialization != EVENT_QUALITY_SERIALIZABLE
        || result->verdict != EVENT_QUALITY_PASS
        || !result->committed || !result->readback_verified
        || (result->reason_flags
            & EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH) != 0U)
    {
        return RT_FALSE;
    }
    return event_quality_facts_equal(&result->live_facts,
                                     &result->readback_facts);
}

rt_err_t event_quality_check_round_trip(
    const event_quality_result_t *live_result,
    const event_quality_facts_t *readback_facts,
    event_quality_result_t *final_result)
{
    event_quality_result_t readback_result;

    if (live_result == RT_NULL || readback_facts == RT_NULL
        || final_result == RT_NULL
        || live_result->serialization != EVENT_QUALITY_SERIALIZABLE
        || event_quality_assess_facts(readback_facts, &readback_result)
               != RT_EOK)
    {
        return -RT_ERROR;
    }

    *final_result = *live_result;
    final_result->readback_facts = *readback_facts;
    final_result->reason_flags |= readback_result.reason_flags;
    if (!event_quality_facts_equal(&live_result->live_facts, readback_facts))
    {
        final_result->reason_flags |=
            EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH;
    }
    final_result->verdict =
        event_quality_verdict_for_reasons(final_result->reason_flags);
    final_result->ai_eligible = event_quality_ai_eligible(final_result);
    return RT_EOK;
}

rt_err_t event_quality_assess_fault_injection_case(
    uint32_t event_case,
    event_quality_result_t *result)
{
    event_quality_facts_t facts;

    if (result == RT_NULL)
    {
        return -RT_ERROR;
    }
    memset(&facts, 0, sizeof(facts));
    facts.serialization = EVENT_QUALITY_SERIALIZABLE;
    facts.fixed_shape = RT_TRUE;

    switch (event_case)
    {
    case EVENT_QUALITY_FAULT_SEQUENCE_GAP: /* sequence-gap */
        facts.flags = EVENT_FLAG_DATA_LOSS;
        facts.sequence_continuous = RT_FALSE;
        facts.loss.lost_sample_count = 1U;
        facts.loss.loss_episode_count = 1U;
        break;
    case EVENT_QUALITY_FAULT_PRETRIGGER_SHORT: /* pretrigger-short */
        facts.flags = EVENT_FLAG_PRETRIGGER_SHORT;
        facts.pretrigger_short = RT_TRUE;
        break;
    case EVENT_QUALITY_FAULT_DURATION_CAP: /* duration-cap */
        facts.flags = EVENT_FLAG_DURATION_CAPPED;
        facts.duration_capped = RT_TRUE;
        break;
    case EVENT_QUALITY_FAULT_POOL_PRESSURE: /* pool-pressure */
        facts.fixed_shape = RT_FALSE;
        break;
    case EVENT_QUALITY_FAULT_QUEUE_PRESSURE: /* queue-pressure */
        facts.fixed_shape = RT_FALSE;
        break;
    default:
        return -RT_ERROR;
    }
    return event_quality_assess_facts(&facts, result);
}
