#include "pretrigger_ring.h"

#include <string.h>

static uint8_t pretrigger_ring_start_index(const pretrigger_ring_t *ring)
{
    return (uint8_t)((ring->head + PRETRIGGER_RING_BLOCK_COUNT - ring->count)
                     % PRETRIGGER_RING_BLOCK_COUNT);
}

void pretrigger_ring_init(pretrigger_ring_t *ring)
{
    if (ring != RT_NULL)
    {
        memset(ring, 0, sizeof(*ring));
    }
}

rt_err_t pretrigger_ring_push(sample_block_pool_t *pool,
                              pretrigger_ring_t *ring,
                              sample_block_t *block)
{
    sample_block_t *oldest;

    if (pool == RT_NULL || ring == RT_NULL || block == RT_NULL
        || sample_block_pool_retain(pool, block) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (ring->count == PRETRIGGER_RING_BLOCK_COUNT)
    {
        oldest = ring->blocks[ring->head];
        if (sample_block_pool_release(pool, oldest) != RT_EOK)
        {
            (void)sample_block_pool_release(pool, block);
            return -RT_ERROR;
        }
    }
    else
    {
        ring->count++;
    }

    ring->blocks[ring->head] = block;
    ring->head = (uint8_t)((ring->head + 1U) % PRETRIGGER_RING_BLOCK_COUNT);
    return RT_EOK;
}

uint8_t pretrigger_ring_snapshot(sample_block_pool_t *pool,
                                 const pretrigger_ring_t *ring,
                                 sample_block_t **blocks,
                                 uint8_t capacity)
{
    sample_block_t *retained[PRETRIGGER_RING_BLOCK_COUNT] = {0};
    uint8_t index;
    uint8_t start;

    if (pool == RT_NULL || ring == RT_NULL || blocks == RT_NULL
        || capacity < ring->count)
    {
        return 0U;
    }

    start = pretrigger_ring_start_index(ring);
    for (index = 0U; index < ring->count; index++)
    {
        retained[index] = ring->blocks[(start + index)
                                       % PRETRIGGER_RING_BLOCK_COUNT];
        if (retained[index] == RT_NULL
            || sample_block_pool_retain(pool, retained[index]) != RT_EOK)
        {
            while (index > 0U)
            {
                index--;
                (void)sample_block_pool_release(pool, retained[index]);
            }
            return 0U;
        }
    }

    for (index = 0U; index < ring->count; index++)
    {
        blocks[index] = retained[index];
    }

    return ring->count;
}

rt_err_t pretrigger_ring_reset(sample_block_pool_t *pool,
                               pretrigger_ring_t *ring)
{
    uint8_t index;
    rt_err_t result = RT_EOK;

    if (pool == RT_NULL || ring == RT_NULL)
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < ring->count; index++)
    {
        if (sample_block_pool_release(pool, ring->blocks[index]) != RT_EOK)
        {
            result = -RT_ERROR;
        }
    }

    memset(ring, 0, sizeof(*ring));
    return result;
}
