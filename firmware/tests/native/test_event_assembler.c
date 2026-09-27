#include <stdint.h>
#include <stdio.h>

#include "event_assembler.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "event_assembler: %s\n", message);
        return 0;
    }

    return 1;
}

static int consume_block(sample_block_pool_t *pool,
                         event_assembler_t *assembler,
                         uint32_t sequence,
                         const trigger_fact_t *trigger)
{
    sample_block_t *block = sample_block_pool_acquire(pool);

    if (block == RT_NULL)
    {
        return 0;
    }

    block->sequence = sequence;
    block->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    if (sample_block_pool_publish(pool, block) != RT_EOK)
    {
        return 0;
    }

    block = sample_block_pool_take_ready(pool);
    return block != RT_NULL
           && event_assembler_consume(assembler, pool, block, trigger) == RT_EOK;
}

static int fill_pretrigger_history(sample_block_pool_t *pool,
                                   event_assembler_t *assembler,
                                   uint32_t *sequence)
{
    uint8_t index;

    for (index = 0U; index < EVENT_PRETRIGGER_BLOCK_COUNT; index++)
    {
        if (!consume_block(pool, assembler, (*sequence)++, RT_NULL))
        {
            return 0;
        }
    }

    return 1;
}

static trigger_fact_t make_trigger(uint32_t sequence, uint32_t magnitude)
{
    trigger_fact_t trigger = {
        .sample_sequence = sequence,
        .magnitude_sq = magnitude,
        .threshold_magnitude_sq = 100U,
        .axis_mask = TRIGGER_AXIS_X,
    };

    return trigger;
}

static int test_trigger_position_is_preserved(void)
{
    static const uint16_t trigger_indices[] = {0U, 17U, 63U};
    uint8_t case_index;

    for (case_index = 0U;
         case_index < sizeof(trigger_indices) / sizeof(trigger_indices[0]);
         case_index++)
    {
        sample_block_pool_t pool = {0};
        event_assembler_t assembler = {0};
        sample_block_t *block;
        uint16_t trigger_index = trigger_indices[case_index];
        trigger_fact_t trigger = make_trigger(1000U + trigger_index, 450U);

        trigger.sample_index = trigger_index;
        sample_block_pool_init(&pool);
        event_assembler_init(&assembler);
        block = sample_block_pool_acquire(&pool);
        if (!expect(block != RT_NULL,
                    "the trigger-position test must acquire one block"))
        {
            return 1;
        }
        block->sequence = 1000U;
        block->first_monotonic_us = UINT64_C(5000000);
        block->sample_period_ns = 625000U;
        block->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
        if (!expect(sample_block_pool_publish(&pool, block) == RT_EOK,
                    "the trigger-position block must publish"))
        {
            return 1;
        }
        block = sample_block_pool_take_ready(&pool);
        if (!expect(event_assembler_consume(&assembler, &pool, block, &trigger)
                    == RT_EOK,
                    "the trigger-position block must start an event")
            || !expect(assembler.event.trigger_sequence
                       == block->sequence + trigger_index,
                       "the event must identify the exact triggering sample")
            || !expect(assembler.event.trigger_sample_index == trigger_index,
                       "the event must retain the trigger index")
            || !expect(assembler.event.trigger_monotonic_us
                       == UINT64_C(5000000)
                          + ((uint64_t)trigger_index * 625000U) / 1000U,
                       "the event timestamp must identify the exact triggering sample"))
        {
            return 1;
        }
    }

    return 0;
}

static int test_trigger_timestamp_saturates_at_uint64_max(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    trigger_fact_t trigger = make_trigger(1000U, 450U);
    sample_block_t *block;

    trigger.sample_index = 63U;
    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    block = sample_block_pool_acquire(&pool);
    if (!expect(block != RT_NULL,
                "the saturation test must acquire one block"))
    {
        return 1;
    }
    block->sequence = 1000U;
    block->first_monotonic_us = UINT64_MAX - UINT64_C(10000);
    block->sample_period_ns = 625000U;
    block->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    if (!expect(sample_block_pool_publish(&pool, block) == RT_EOK,
                "the saturation block must publish"))
    {
        return 1;
    }
    block = sample_block_pool_take_ready(&pool);
    if (!expect(event_assembler_consume(&assembler, &pool, block, &trigger)
                == RT_EOK,
                "the saturation block must start an event")
        || !expect(assembler.event.trigger_monotonic_us == UINT64_MAX,
                   "the trigger timestamp must saturate at UINT64_MAX"))
    {
        return 1;
    }

    return 0;
}

static int test_standard_event_flows_to_export_and_releases_every_block(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    trigger_fact_t trigger = make_trigger(1000U, 450U);
    uint32_t sequence = 0U;
    uint8_t index;

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    event_assembler_set_next_event_id(&assembler, 42U);
    if (!expect(fill_pretrigger_history(&pool, &assembler, &sequence),
                "the standard test must fill one second of history")
        || !expect(consume_block(&pool, &assembler, sequence++, &trigger),
                   "the trigger block must be consumable")
        || !expect(assembler.state == EVENT_POST_TRIGGER_CAPTURING,
                   "a valid trigger must start post-trigger capture")
        || !expect(assembler.event.pretrigger_block_count
                   == EVENT_PRETRIGGER_BLOCK_COUNT,
                   "a complete pretrigger window must be fixed into the event")
        || !expect(assembler.event.posttrigger_block_count == 1U,
                   "the trigger block must be the first post-trigger block")
        || !expect(assembler.event.event_id == 42U,
                   "a restored event ID must be used by the next trigger"))
    {
        return 1;
    }

    for (index = 1U; index < EVENT_STANDARD_POST_BLOCK_COUNT; index++)
    {
        if (!expect(consume_block(&pool, &assembler, sequence++, RT_NULL),
                    "the standard post-trigger blocks must be consumable"))
        {
            return 1;
        }
    }

    if (!expect(assembler.state == EVENT_READY_FOR_EXPORT,
                "fifty post-trigger blocks must produce an exportable event")
        || !expect(event_assembler_begin_export(&assembler) == RT_EOK,
                   "a ready event must enter the exporting state")
        || !expect(assembler.state == EVENT_EXPORTING,
                   "begin_export must change state")
        || !expect(event_assembler_finish_export(&assembler, &pool, 0) == RT_EOK,
                   "an export failure must still release event ownership")
        || !expect(assembler.state == EVENT_ARMED,
                   "a failed export must re-arm capture")
        || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                   "failed export must return every event block"))
    {
        return 1;
    }

    return 0;
}

static int test_short_history_and_repeated_triggers_cap_at_four_seconds(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    trigger_fact_t first_trigger = make_trigger(2000U, 400U);
    trigger_fact_t repeated_trigger = make_trigger(3000U, 900U);
    uint32_t sequence = 0U;
    uint8_t index;

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    if (!expect(consume_block(&pool, &assembler, sequence++, RT_NULL)
                && consume_block(&pool, &assembler, sequence++, RT_NULL),
                "the short-history test must produce two pretrigger blocks")
        || !expect(consume_block(&pool, &assembler, sequence++, &first_trigger),
                   "the short-history trigger must start capture")
        || !expect((assembler.event.flags & EVENT_FLAG_PRETRIGGER_SHORT) != 0U,
                   "insufficient history must be explicitly flagged"))
    {
        return 1;
    }

    for (index = 1U; index < 49U; index++)
    {
        if (!expect(consume_block(&pool, &assembler, sequence++, RT_NULL),
                    "the short-history event must collect initial posttrigger data"))
        {
            return 1;
        }
    }

    if (!expect(consume_block(&pool, &assembler, sequence++, &repeated_trigger),
                "a repeated trigger must extend an active event")
        || !expect(assembler.event.peak_magnitude_sq == repeated_trigger.magnitude_sq,
                   "a repeated trigger must update the event peak")
        || !expect(assembler.event.subtrigger_count == 1U,
                   "a repeated trigger must be counted")
        || !expect(assembler.event.flags & EVENT_FLAG_DURATION_CAPPED,
                   "an extension beyond three post seconds must be capped"))
    {
        return 1;
    }

    for (index = assembler.event.posttrigger_block_count;
         index < EVENT_MAX_POST_BLOCK_COUNT;
         index++)
    {
        if (!expect(consume_block(&pool, &assembler, sequence++, RT_NULL),
                    "the capped event must continue until its maximum window"))
        {
            return 1;
        }
    }

    if (!expect(assembler.state == EVENT_READY_FOR_EXPORT,
                "a capped event must become exportable at the fixed maximum")
        || !expect(assembler.event.posttrigger_block_count
                   == EVENT_MAX_POST_BLOCK_COUNT,
                   "the event must never retain more than three post seconds")
        || !expect(event_assembler_begin_export(&assembler) == RT_EOK,
                   "the capped event must be exportable")
        || !expect(consume_block(&pool, &assembler, sequence++, &repeated_trigger),
                   "a block arriving during export must be safely consumed")
        || !expect(assembler.busy_trigger_count == 1U,
                   "a trigger during export must be counted as busy")
        || !expect(event_assembler_finish_export(&assembler, &pool, 1) == RT_EOK,
                   "a successful export must release event blocks")
        || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                   "the capped event must release every reference after export"))
    {
        return 1;
    }

    return 0;
}

static int test_one_hundred_events_return_the_pool_to_its_baseline(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    trigger_fact_t trigger = make_trigger(4000U, 500U);
    uint32_t sequence = 0U;
    uint16_t event_index;
    uint8_t post_index;

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    for (event_index = 0U; event_index < 100U; event_index++)
    {
        if (!expect(fill_pretrigger_history(&pool, &assembler, &sequence),
                    "each repeated event must refill pretrigger history")
            || !expect(consume_block(&pool, &assembler, sequence++, &trigger),
                       "each repeated event must accept its trigger block"))
        {
            return 1;
        }

        for (post_index = 1U; post_index < EVENT_STANDARD_POST_BLOCK_COUNT;
             post_index++)
        {
            if (!expect(consume_block(&pool, &assembler, sequence++, RT_NULL),
                        "each repeated event must collect its standard post window"))
            {
                return 1;
            }
        }

        if (!expect(assembler.state == EVENT_READY_FOR_EXPORT,
                    "each repeated event must become ready")
            || !expect(event_assembler_begin_export(&assembler) == RT_EOK,
                       "each repeated event must start export")
            || !expect(event_assembler_finish_export(&assembler, &pool, 1) == RT_EOK,
                       "each repeated event must release after export")
            || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                       "every completed event must restore the free-block baseline"))
        {
            return 1;
        }
    }

    return 0;
}

static int test_health_snapshot_is_copied_once_for_the_pending_event(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    trigger_fact_t trigger = make_trigger(5000U, 600U);
    health_snapshot_t snapshot = {
        .state = HEALTH_DEGRADED,
        .utc_valid = RT_TRUE,
        .utc_unix_seconds = INT64_C(1735689600),
        .temperature_centi_c = 2534,
        .transition_sequence = 7U,
    };

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    if (!expect(consume_block(&pool, &assembler, 0U, &trigger),
                "the snapshot test must accept one trigger")
        || !expect(assembler.state == EVENT_POST_TRIGGER_CAPTURING,
                   "an accepted trigger must own a pending event"))
    {
        return 1;
    }

    event_assembler_set_health_snapshot(&assembler, &snapshot);
    snapshot.state = HEALTH_HEALTHY;
    snapshot.utc_unix_seconds = 0;
    snapshot.temperature_centi_c = -1;
    snapshot.transition_sequence = 99U;
    if (!expect(assembler.event.health_snapshot.state == HEALTH_DEGRADED
                && assembler.event.health_snapshot.utc_unix_seconds == INT64_C(1735689600)
                && assembler.event.health_snapshot.temperature_centi_c == 2534
                && assembler.event.health_snapshot.transition_sequence == 7U,
                "mutating the provider snapshot must not alter the pending event"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_trigger_position_is_preserved() != 0
        || test_trigger_timestamp_saturates_at_uint64_max() != 0
        || test_standard_event_flows_to_export_and_releases_every_block() != 0
        || test_short_history_and_repeated_triggers_cap_at_four_seconds() != 0
        || test_one_hundred_events_return_the_pool_to_its_baseline() != 0
        || test_health_snapshot_is_copied_once_for_the_pending_event() != 0)
    {
        return 1;
    }

    puts("event assembler: PASS");
    return 0;
}
