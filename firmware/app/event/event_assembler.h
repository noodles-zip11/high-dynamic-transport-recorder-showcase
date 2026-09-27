#ifndef TRANSPORT_RECORDER_EVENT_ASSEMBLER_H
#define TRANSPORT_RECORDER_EVENT_ASSEMBLER_H

#include <stdint.h>

#include "pretrigger_ring.h"
#include "trigger_detector.h"
#include "health_service.h"

#define EVENT_PRETRIGGER_BLOCK_COUNT PRETRIGGER_RING_BLOCK_COUNT
#define EVENT_STANDARD_POST_BLOCK_COUNT 50U
#define EVENT_MAX_POST_BLOCK_COUNT 75U
#define EVENT_MAX_BLOCK_COUNT (EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT)

typedef enum
{
    EVENT_ARMED = 0,
    EVENT_POST_TRIGGER_CAPTURING,
    EVENT_READY_FOR_EXPORT,
    EVENT_EXPORTING,
} event_state_t;

typedef enum
{
    EVENT_FLAG_NONE = 0U,
    EVENT_FLAG_PRETRIGGER_SHORT = 1U << 0,
    EVENT_FLAG_DURATION_CAPPED = 1U << 1,
    EVENT_FLAG_RESOURCE_LIMIT = 1U << 2,
    EVENT_FLAG_DATA_LOSS = 1U << 3,
} event_flag_t;

typedef struct
{
    uint32_t event_id;
    uint64_t trigger_monotonic_us;
    uint32_t trigger_sequence;
    uint32_t peak_magnitude_sq;
    uint32_t threshold_magnitude_sq;
    uint16_t flags;
    uint16_t subtrigger_count;
    uint16_t trigger_sample_index;
    uint8_t pretrigger_block_count;
    uint8_t posttrigger_block_count;
    uint8_t block_count;
    health_snapshot_t health_snapshot;
    sample_block_t *blocks[EVENT_MAX_BLOCK_COUNT];
} event_record_t;

typedef struct
{
    event_state_t state;
    pretrigger_ring_t pretrigger;
    event_record_t event;
    uint8_t posttrigger_target_blocks;
    uint32_t next_event_id;
    uint32_t busy_trigger_count;
    uint32_t resource_reject_count;
} event_assembler_t;

void event_assembler_init(event_assembler_t *assembler);
void event_assembler_set_next_event_id(event_assembler_t *assembler,
                                       uint32_t next_event_id);
void event_assembler_set_health_snapshot(event_assembler_t *assembler,
                                         const health_snapshot_t *snapshot);

rt_err_t event_assembler_consume(event_assembler_t *assembler,
                                 sample_block_pool_t *pool,
                                 sample_block_t *block,
                                 const trigger_fact_t *trigger_or_null);

const event_record_t *event_assembler_event(const event_assembler_t *assembler);

rt_err_t event_assembler_begin_export(event_assembler_t *assembler);

rt_err_t event_assembler_finish_export(event_assembler_t *assembler,
                                       sample_block_pool_t *pool,
                                       int succeeded);

rt_err_t event_assembler_clear(event_assembler_t *assembler,
                               sample_block_pool_t *pool);

#endif
