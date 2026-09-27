#ifndef TRANSPORT_RECORDER_PRETRIGGER_RING_H
#define TRANSPORT_RECORDER_PRETRIGGER_RING_H

#include <stdint.h>

#include "sample_block_pool.h"

#define PRETRIGGER_RING_BLOCK_COUNT 25U

typedef struct
{
    sample_block_t *blocks[PRETRIGGER_RING_BLOCK_COUNT];
    uint8_t count;
    uint8_t head;
} pretrigger_ring_t;

void pretrigger_ring_init(pretrigger_ring_t *ring);

rt_err_t pretrigger_ring_push(sample_block_pool_t *pool,
                              pretrigger_ring_t *ring,
                              sample_block_t *block);

uint8_t pretrigger_ring_snapshot(sample_block_pool_t *pool,
                                 const pretrigger_ring_t *ring,
                                 sample_block_t **blocks,
                                 uint8_t capacity);

rt_err_t pretrigger_ring_reset(sample_block_pool_t *pool,
                               pretrigger_ring_t *ring);

#endif
