#include "sample_block_pool.h"

#include <limits.h>
#include <string.h>

static sample_block_t *sample_block_pool_find(sample_block_pool_t *pool,
                                              sample_block_t *block)
{
    rt_size_t index;

    if (pool == RT_NULL || block == RT_NULL)
    {
        return RT_NULL;
    }

    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        if (&pool->blocks[index] == block)
        {
            return block;
        }
    }

    return RT_NULL;
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
rt_bool_t sample_block_pool_is_valid_consuming(
    const sample_block_pool_t *pool,
    const sample_block_t *block)
{
    rt_bool_t valid = RT_FALSE;
    rt_base_t level;
    sample_block_t *owned_block;

    if (pool == RT_NULL || block == RT_NULL)
    {
        return RT_FALSE;
    }

    level = rt_hw_interrupt_disable();
    owned_block = sample_block_pool_find((sample_block_pool_t *)pool,
                                         (sample_block_t *)block);
    if (owned_block != RT_NULL
        && owned_block->state == SAMPLE_BLOCK_CONSUMING
        && owned_block->ref_count != 0U)
    {
        valid = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);
    return valid;
}
#endif

static int sample_block_sequence_is_before(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) < 0;
}

static uint16_t sample_block_pool_count_free_locked(
    const sample_block_pool_t *pool)
{
    rt_size_t index;
    uint16_t free_count = 0U;

    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        if (pool->blocks[index].state == SAMPLE_BLOCK_FREE)
        {
            free_count++;
        }
    }

    return free_count;
}

static void sample_block_pool_update_min_free_locked(sample_block_pool_t *pool)
{
    uint16_t free_count = sample_block_pool_count_free_locked(pool);

    if (free_count < pool->min_free_count)
    {
        pool->min_free_count = free_count;
    }
}

//把全部块清零，初始状态都是 FREE
void sample_block_pool_init(sample_block_pool_t *pool)
{
    if (pool != RT_NULL)
    {
        memset(pool, 0, sizeof(*pool));
        pool->min_free_count = SAMPLE_BLOCK_POOL_SIZE;
    }
}
//找一个 FREE 块，改为 FILLING 后交给生产者填写
sample_block_t *sample_block_pool_acquire(sample_block_pool_t *pool)
{
    rt_size_t index;
    rt_base_t level;

    if (pool == RT_NULL)
    {
        return RT_NULL;
    }

    level = rt_hw_interrupt_disable();
    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        if (pool->blocks[index].state == SAMPLE_BLOCK_FREE)
        {
            pool->blocks[index].state = SAMPLE_BLOCK_FILLING;
            sample_block_pool_update_min_free_locked(pool);
            rt_hw_interrupt_enable(level);
            return &pool->blocks[index];
        }
    }

    pool->backpressure_count++;
    rt_hw_interrupt_enable(level);
    return RT_NULL;
}
//生产者填完数据后，把块从 FILLING 改成 READY
rt_err_t sample_block_pool_publish(sample_block_pool_t *pool,
                                   sample_block_t *block)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    block = sample_block_pool_find(pool, block);
    if (block == RT_NULL || block->state != SAMPLE_BLOCK_FILLING)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    block->state = SAMPLE_BLOCK_READY;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

rt_err_t sample_block_pool_abandon(sample_block_pool_t *pool,
                                   sample_block_t *block)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    block = sample_block_pool_find(pool, block);
    if (block == RT_NULL || block->state != SAMPLE_BLOCK_FILLING)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    memset(block, 0, sizeof(*block));
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

sample_block_t *sample_block_pool_take_ready(sample_block_pool_t *pool)
{
    rt_size_t index;
    sample_block_t *oldest = RT_NULL;
    rt_base_t level;

    if (pool == RT_NULL)
    {
        return RT_NULL;
    }

    level = rt_hw_interrupt_disable();    for (index = 0U; index < SAMPLE_BLOCK_POOL_SIZE; index++)
    {
        if (pool->blocks[index].state == SAMPLE_BLOCK_READY
            && (oldest == RT_NULL
                || sample_block_sequence_is_before(pool->blocks[index].sequence,
                                                   oldest->sequence)))
        {
            oldest = &pool->blocks[index];
        }
    }

    if (oldest != RT_NULL)
    {
        oldest->state = SAMPLE_BLOCK_CONSUMING;
        oldest->ref_count = 1U;
    }

    rt_hw_interrupt_enable(level);
    return oldest;
}

rt_err_t sample_block_pool_retain(sample_block_pool_t *pool,
                                  sample_block_t *block)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    block = sample_block_pool_find(pool, block);
    if (block == RT_NULL || block->state != SAMPLE_BLOCK_CONSUMING
        || block->ref_count == 0U || block->ref_count == UINT16_MAX)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    block->ref_count++;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

rt_err_t sample_block_pool_release(sample_block_pool_t *pool,
                                   sample_block_t *block)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    block = sample_block_pool_find(pool, block);
    if (block == RT_NULL || block->state != SAMPLE_BLOCK_CONSUMING
        || block->ref_count == 0U)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    block->ref_count--;
    if (block->ref_count == 0U)
    {
        memset(block, 0, sizeof(*block));
    }

    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

uint16_t sample_block_pool_free_count(const sample_block_pool_t *pool)
{
    uint16_t free_count;
    rt_base_t level;

    if (pool == RT_NULL)
    {
        return 0U;
    }

    level = rt_hw_interrupt_disable();
    free_count = sample_block_pool_count_free_locked(pool);
    rt_hw_interrupt_enable(level);
    return free_count;
}

uint16_t sample_block_pool_min_free_count(const sample_block_pool_t *pool)
{
    uint16_t min_free_count;
    rt_base_t level;

    if (pool == RT_NULL)
    {
        return 0U;
    }

    level = rt_hw_interrupt_disable();
    min_free_count = pool->min_free_count;
    rt_hw_interrupt_enable(level);
    return min_free_count;
}
