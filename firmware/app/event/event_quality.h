#ifndef TRANSPORT_RECORDER_EVENT_QUALITY_H
#define TRANSPORT_RECORDER_EVENT_QUALITY_H

#include <stdint.h>

#include "event_assembler.h"
#include "sample_block_pool.h"

typedef enum
{
    EVENT_QUALITY_CAPTURE_FAILURE = 0,
    EVENT_QUALITY_SERIALIZABLE = 1,
} event_quality_serialization_t;

typedef enum
{
    EVENT_QUALITY_NO_VERDICT = 0,
    EVENT_QUALITY_PASS,
    EVENT_QUALITY_DEGRADED,
    EVENT_QUALITY_INVALID,
} event_quality_verdict_t;

typedef enum
{
    EVENT_QUALITY_REASON_NONE = 0U,
    EVENT_QUALITY_REASON_PRETRIGGER_SHORT = 1U << 0,
    EVENT_QUALITY_REASON_DURATION_CAPPED = 1U << 1,
    EVENT_QUALITY_REASON_DATA_LOSS = 1U << 2,
    EVENT_QUALITY_REASON_SEQUENCE_GAP = 1U << 3,
    EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION = 1U << 4,
    EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH = 1U << 5,
} event_quality_reason_t;

typedef struct
{
    uint32_t lost_sample_count;
    uint32_t first_lost_sequence;
    uint32_t last_lost_sequence;
    uint32_t loss_episode_count;
    uint64_t first_loss_monotonic_us;
    uint64_t last_loss_monotonic_us;
} event_quality_loss_summary_t;

typedef struct
{
    uint32_t event_id;
    uint32_t trigger_sequence;
    uint32_t actual_sample_count;
    uint32_t pretrigger_samples;
    uint32_t posttrigger_samples;
    uint16_t trigger_sample_index;
    uint16_t flags;
    uint8_t block_count;
    uint8_t pretrigger_block_count;
    uint8_t posttrigger_block_count;
    uint8_t sequence_continuous;
    uint8_t fixed_shape;
    uint8_t pretrigger_short;
    uint8_t duration_capped;
    event_quality_serialization_t serialization;
    event_quality_loss_summary_t loss;
} event_quality_facts_t;

typedef struct
{
    event_quality_serialization_t serialization;
    event_quality_verdict_t verdict;
    uint32_t reason_flags;
    event_quality_facts_t live_facts;
    event_quality_facts_t readback_facts;
    rt_bool_t committed;
    rt_bool_t readback_verified;
    rt_bool_t ai_eligible;
} event_quality_result_t;

enum
{
    EVENT_QUALITY_FAULT_SEQUENCE_GAP = 1U,
    EVENT_QUALITY_FAULT_PRETRIGGER_SHORT = 2U,
    EVENT_QUALITY_FAULT_DURATION_CAP = 3U,
    EVENT_QUALITY_FAULT_POOL_PRESSURE = 4U,
    EVENT_QUALITY_FAULT_QUEUE_PRESSURE = 5U,
};

rt_err_t event_quality_calculate_loss_summary(
    const event_record_t *event,
    event_quality_loss_summary_t *summary);

rt_err_t event_quality_collect_facts(const sample_block_pool_t *pool,
                                     const event_record_t *event,
                                     event_quality_facts_t *facts);

rt_err_t event_quality_assess_facts(const event_quality_facts_t *facts,
                                    event_quality_result_t *result);

rt_err_t event_quality_evaluate(const sample_block_pool_t *pool,
                                const event_record_t *event,
                                event_quality_result_t *result);

rt_bool_t event_quality_facts_equal(const event_quality_facts_t *left,
                                    const event_quality_facts_t *right);

rt_bool_t event_quality_ai_eligible(const event_quality_result_t *result);

rt_err_t event_quality_check_round_trip(
    const event_quality_result_t *live_result,
    const event_quality_facts_t *readback_facts,
    event_quality_result_t *final_result);

rt_err_t event_quality_assess_fault_injection_case(
    uint32_t event_case,
    event_quality_result_t *result);

#endif
