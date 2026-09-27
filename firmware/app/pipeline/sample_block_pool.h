#ifndef TRANSPORT_RECORDER_SAMPLE_BLOCK_POOL_H
#define TRANSPORT_RECORDER_SAMPLE_BLOCK_POOL_H

#include <stdint.h>

#include <rtthread.h>

#include "icm45686_fifo.h"

#define SAMPLE_BLOCK_SAMPLE_CAPACITY 64U
/* Acquisition must always retain this many blocks for the producer path. */
#define SAMPLE_BLOCK_HANDOFF_COUNT 8U
/*
 * The synchronous event exporter can hold a maximum event while acquisition
 * continues. Keep a bounded export headroom contract in the pool itself.
 */
#define SAMPLE_BLOCK_EXPORT_HEADROOM_COUNT 64U
#define SAMPLE_BLOCK_POOL_SIZE 192U

typedef enum
{
    SAMPLE_BLOCK_FREE = 0,
    SAMPLE_BLOCK_FILLING,
    SAMPLE_BLOCK_READY,
    SAMPLE_BLOCK_CONSUMING,
} sample_block_state_t;

typedef struct
{
    sample_block_state_t state;
    uint32_t sequence;
    uint64_t first_monotonic_us;
    uint32_t sample_period_ns;
    uint16_t sample_count;
    uint16_t flags;
    uint16_t ref_count;
    icm45686_fifo_sample_t samples[SAMPLE_BLOCK_SAMPLE_CAPACITY];
} sample_block_t;

typedef struct
{
    sample_block_t blocks[SAMPLE_BLOCK_POOL_SIZE];
    uint32_t backpressure_count;
    uint16_t min_free_count;
} sample_block_pool_t;

void sample_block_pool_init(sample_block_pool_t *pool);

sample_block_t *sample_block_pool_acquire(sample_block_pool_t *pool);

rt_err_t sample_block_pool_publish(sample_block_pool_t *pool,
                                   sample_block_t *block);

rt_err_t sample_block_pool_abandon(sample_block_pool_t *pool,
                                   sample_block_t *block);

sample_block_t *sample_block_pool_take_ready(sample_block_pool_t *pool);

rt_err_t sample_block_pool_retain(sample_block_pool_t *pool,
                                  sample_block_t *block);

rt_err_t sample_block_pool_release(sample_block_pool_t *pool,
                                   sample_block_t *block);

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
rt_bool_t sample_block_pool_is_valid_consuming(
    const sample_block_pool_t *pool,
    const sample_block_t *block);
#endif

uint16_t sample_block_pool_free_count(const sample_block_pool_t *pool);

uint16_t sample_block_pool_min_free_count(const sample_block_pool_t *pool);

#endif
