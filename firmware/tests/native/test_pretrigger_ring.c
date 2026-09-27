#include <stdint.h>
#include <stdio.h>

#include "pretrigger_ring.h"

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "pretrigger_ring: %s\n", message);
        return 0;
    }

    return 1;
}

static sample_block_t *publish_and_take(sample_block_pool_t *pool,
                                        uint32_t sequence)
{
    sample_block_t *block = sample_block_pool_acquire(pool);

    if (block == RT_NULL)
    {
        return RT_NULL;
    }

    block->sequence = sequence;
    block->sample_count = 1U;
    if (sample_block_pool_publish(pool, block) != RT_EOK)
    {
        return RT_NULL;
    }

    return sample_block_pool_take_ready(pool);
}

static int test_ring_keeps_only_the_latest_pretrigger_window(void)
{
    sample_block_pool_t pool = {0};
    pretrigger_ring_t ring = {0};
    sample_block_t *snapshot[PRETRIGGER_RING_BLOCK_COUNT] = {0};
    sample_block_t *block;
    uint8_t index;

    sample_block_pool_init(&pool);
    pretrigger_ring_init(&ring);
    for (index = 0U; index < PRETRIGGER_RING_BLOCK_COUNT + 1U; index++)
    {
        block = publish_and_take(&pool, index);
        if (!expect(block != RT_NULL, "the test must produce a consumer block")
            || !expect(pretrigger_ring_push(&pool, &ring, block) == RT_EOK,
                       "the ring must retain each completed block")
            || !expect(sample_block_pool_release(&pool, block) == RT_EOK,
                       "the temporary consumer reference must be releasable"))
        {
            return 1;
        }
    }

    if (!expect(ring.count == PRETRIGGER_RING_BLOCK_COUNT,
                "a full ring must have exactly the configured window size")
        || !expect(pretrigger_ring_snapshot(&pool, &ring, snapshot,
                                             PRETRIGGER_RING_BLOCK_COUNT)
                   == PRETRIGGER_RING_BLOCK_COUNT,
                   "an event snapshot must retain the whole pretrigger window"))
    {
        return 1;
    }

    for (index = 0U; index < PRETRIGGER_RING_BLOCK_COUNT; index++)
    {
        if (!expect(snapshot[index]->sequence == (uint32_t)index + 1U,
                    "a snapshot must be chronological and discard only the oldest block")
            || !expect(snapshot[index]->ref_count == 2U,
                       "the ring and event snapshot must hold independent references")
            || !expect(sample_block_pool_release(&pool, snapshot[index]) == RT_EOK,
                       "each event snapshot reference must be releasable"))
        {
            return 1;
        }
    }

    pretrigger_ring_reset(&pool, &ring);
    return !expect(ring.count == 0U,
                   "reset must leave the ring empty")
           || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                      "the final ring release must return every block to the pool");
}

static int test_snapshot_never_partially_retains_a_window(void)
{
    sample_block_pool_t pool = {0};
    pretrigger_ring_t ring = {0};
    sample_block_t *block;
    sample_block_t *too_small[PRETRIGGER_RING_BLOCK_COUNT - 1U] = {0};
    uint8_t index;

    sample_block_pool_init(&pool);
    pretrigger_ring_init(&ring);
    for (index = 0U; index < PRETRIGGER_RING_BLOCK_COUNT; index++)
    {
        block = publish_and_take(&pool, index);
        if (!expect(block != RT_NULL, "the capacity test must produce a block")
            || !expect(pretrigger_ring_push(&pool, &ring, block) == RT_EOK,
                       "the capacity test must fill the ring")
            || !expect(sample_block_pool_release(&pool, block) == RT_EOK,
                       "the capacity test must release the consumer reference"))
        {
            return 1;
        }
    }

    if (!expect(pretrigger_ring_snapshot(&pool, &ring, too_small,
                                         PRETRIGGER_RING_BLOCK_COUNT - 1U) == 0U,
                "an undersized snapshot destination must fail without partial output")
        || !expect(ring.blocks[0]->ref_count == 1U,
                   "a failed snapshot must not retain any event references"))
    {
        return 1;
    }

    pretrigger_ring_reset(&pool, &ring);
    return !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                   "reset after failed snapshot must release every ring reference");
}

int main(void)
{
    if (test_ring_keeps_only_the_latest_pretrigger_window() != 0
        || test_snapshot_never_partially_retains_a_window() != 0)
    {
        return 1;
    }

    puts("pretrigger ring: PASS");
    return 0;
}
