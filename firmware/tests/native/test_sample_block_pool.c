#include <stdint.h>
#include <stdio.h>

#include "event_assembler.h"
#include "sample_block_pool.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "sample_block_pool: %s\n", message);
        return 0;
    }

    return 1;
}

static int test_pool_reserves_capacity_for_events(void)
{
    return !expect(SAMPLE_BLOCK_HANDOFF_COUNT == 8U,
                   "the handoff pool must contain eight blocks")
           || !expect(SAMPLE_BLOCK_POOL_SIZE == 192U,
                      "the event pipeline must own 192 fixed blocks")
           || !expect(SAMPLE_BLOCK_POOL_SIZE
                          >= EVENT_MAX_BLOCK_COUNT
                             + SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT
                             + SAMPLE_BLOCK_HANDOFF_COUNT,
                      "the pool must cover event, export, and handoff headroom");
}

static int test_maximum_event_leaves_export_headroom(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *event_blocks[EVENT_MAX_BLOCK_COUNT] = {0};
    sample_block_t *producer_blocks[SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT] = {0};
    sample_block_t *taken;
    rt_size_t index;

    sample_block_pool_init(&pool);
    for (index = 0U; index < EVENT_MAX_BLOCK_COUNT; index++)
    {
        event_blocks[index] = sample_block_pool_acquire(&pool);
        if (!expect(event_blocks[index] != RT_NULL,
                    "the maximum event must retain every configured event block"))
        {
            return 1;
        }
        event_blocks[index]->sequence = (uint32_t)index;
        if (!expect(sample_block_pool_publish(&pool, event_blocks[index]) == RT_EOK,
                    "an event block must be publishable before retention")
            || !expect(sample_block_pool_take_ready(&pool) == event_blocks[index],
                       "the event must own the published block"))
        {
            return 1;
        }
    }

    for (index = 0U; index < SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT; index++)
    {
        producer_blocks[index] = sample_block_pool_acquire(&pool);
        if (!expect(producer_blocks[index] != RT_NULL,
                    "the producer must retain export headroom while an event is held"))
        {
            return 1;
        }
    }

    for (index = 0U; index < EVENT_MAX_BLOCK_COUNT; index++)
    {
        (void)sample_block_pool_release(&pool, event_blocks[index]);
    }
    for (index = 0U; index < SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT; index++)
    {
        (void)sample_block_pool_abandon(&pool, producer_blocks[index]);
    }

    taken = sample_block_pool_take_ready(&pool);
    return !expect(taken == RT_NULL && sample_block_pool_free_count(&pool)
                                      == SAMPLE_BLOCK_POOL_SIZE,
                   "all simulated event and producer blocks must be reusable");
}

static int test_block_moves_through_ownership_states(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *producer_block;
    sample_block_t *consumer_block;

    sample_block_pool_init(&pool);
    producer_block = sample_block_pool_acquire(&pool);
    if (!expect(producer_block != RT_NULL,
                "a new pool must provide a free block")
        || !expect(producer_block->state == SAMPLE_BLOCK_FILLING,
                   "the producer must own an acquired block") )
    {
        return 1;
    }

    producer_block->sequence = 7U;
    producer_block->sample_count = 1U;
    producer_block->samples[0].accel[0] = 123;
    if (!expect(sample_block_pool_publish(&pool, producer_block) == RT_EOK,
                "a filled block must be publishable")
        || !expect(producer_block->state == SAMPLE_BLOCK_READY,
                   "a published block must wait for a consumer"))
    {
        return 1;
    }

    consumer_block = sample_block_pool_take_ready(&pool);
    if (!expect(consumer_block == producer_block,
                "the consumer must receive the published block")
        || !expect(consumer_block->state == SAMPLE_BLOCK_CONSUMING,
                   "a taken block must have one consumer")
        || !expect(consumer_block->sequence == 7U
                   && consumer_block->sample_count == 1U
                   && consumer_block->samples[0].accel[0] == 123,
                   "published metadata and samples must remain intact")
        || !expect(consumer_block->ref_count == 1U,
                   "a taken block must start with one consumer reference")
        || !expect(sample_block_pool_retain(&pool, consumer_block) == RT_EOK,
                   "an event may retain a consumer block")
        || !expect(consumer_block->ref_count == 2U,
                   "a retained block must count both holders")
        || !expect(sample_block_pool_release(&pool, consumer_block) == RT_EOK,
                   "one holder may release a shared block")
        || !expect(consumer_block->state == SAMPLE_BLOCK_CONSUMING
                   && consumer_block->ref_count == 1U,
                   "a shared block must stay protected until last release")
        || !expect(sample_block_pool_release(&pool, consumer_block) == RT_EOK,
                   "the final holder must release the shared block")
        || !expect(consumer_block->state == SAMPLE_BLOCK_FREE,
                   "a released block must become reusable"))
    {
        return 1;
    }

    return 0;
}

static int test_pool_tracks_minimum_free_count(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *first;
    sample_block_t *second;

    sample_block_pool_init(&pool);
    first = sample_block_pool_acquire(&pool);
    second = sample_block_pool_acquire(&pool);
    if (!expect(first != RT_NULL && second != RT_NULL,
                "minimum-free tracking must observe acquired blocks")
        || !expect(sample_block_pool_min_free_count(&pool)
                       == SAMPLE_BLOCK_POOL_SIZE - 2U,
                   "minimum-free tracking must retain the lowest observed value"))
    {
        return 1;
    }

    (void)sample_block_pool_abandon(&pool, first);
    (void)sample_block_pool_abandon(&pool, second);
    return !expect(sample_block_pool_min_free_count(&pool)
                       == SAMPLE_BLOCK_POOL_SIZE - 2U,
                   "releasing blocks must not erase the observed minimum");
}

static int test_full_pool_reports_backpressure_without_overwriting(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *blocks[SAMPLE_BLOCK_POOL_SIZE] = {0};
    rt_size_t index;

    sample_block_pool_init(&pool);
    if (!expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                "a new pool must report every block free"))
    {
        return 1;
    }
    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        blocks[index] = sample_block_pool_acquire(&pool);
        if (!expect(blocks[index] != RT_NULL,
                    "each configured pool block must be acquirable once"))
        {
            return 1;
        }
    }

    if (!expect(sample_block_pool_acquire(&pool) == RT_NULL,
                "a full pool must reject a new producer block")
        || !expect(pool.backpressure_count == 1U,
                   "a full-pool rejection must be counted")
        || !expect(sample_block_pool_free_count(&pool) == 0U,
                   "a full pool must report no free blocks")
        || !expect(blocks[0]->state == SAMPLE_BLOCK_FILLING,
                   "a full-pool rejection must not overwrite an owned block"))
    {
        return 1;
    }

    return 0;
}

static int test_consumer_takes_the_oldest_ready_sequence_first(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *first_published;
    sample_block_t *second_published;
    sample_block_t *taken;

    sample_block_pool_init(&pool);
    first_published = sample_block_pool_acquire(&pool);
    second_published = sample_block_pool_acquire(&pool);
    if (!expect(first_published != RT_NULL && second_published != RT_NULL,
                "the ordering test must acquire two blocks"))
    {
        return 1;
    }

    first_published->sequence = 10U;
    second_published->sequence = 9U;
    if (!expect(sample_block_pool_publish(&pool, first_published) == RT_EOK
                && sample_block_pool_publish(&pool, second_published) == RT_EOK,
                "the ordering test must publish both blocks"))
    {
        return 1;
    }

    taken = sample_block_pool_take_ready(&pool);
    if (!expect(taken == second_published,
                "the consumer must take the oldest ready sequence first")
        || !expect(sample_block_pool_release(&pool, taken) == RT_EOK,
                   "the first consumed block must be releasable"))
    {
        return 1;
    }

    taken = sample_block_pool_take_ready(&pool);
    if (!expect(taken == first_published,
                "the next consumer take must preserve chronological order")
        || !expect(sample_block_pool_release(&pool, taken) == RT_EOK,
                   "the second consumed block must be releasable"))
    {
        return 1;
    }

    return 0;
}

static int test_unpublished_block_can_be_abandoned(void)
{
    sample_block_pool_t pool = {0};
    sample_block_t *block;

    sample_block_pool_init(&pool);
    block = sample_block_pool_acquire(&pool);
    if (!expect(block != RT_NULL,
                "an abandon test must acquire a producer block")
        || !expect(sample_block_pool_abandon(&pool, block) == RT_EOK,
                   "an unpublished block must be returnable to the pool")
        || !expect(block->state == SAMPLE_BLOCK_FREE,
                   "an abandoned block must become reusable"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_pool_reserves_capacity_for_events() != 0
        || test_maximum_event_leaves_export_headroom() != 0
        || test_block_moves_through_ownership_states() != 0
        || test_pool_tracks_minimum_free_count() != 0
        || test_full_pool_reports_backpressure_without_overwriting() != 0
        || test_consumer_takes_the_oldest_ready_sequence_first() != 0
        || test_unpublished_block_can_be_abandoned() != 0)
    {
        return 1;
    }

    puts("sample block pool: PASS");
    return 0;
}
