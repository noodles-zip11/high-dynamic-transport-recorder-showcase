#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "event_export_debug.h"
#include "event_quality.h"

static sample_block_pool_t test_pool;
static unsigned int test_count;

typedef struct
{
    uint8_t header[EVENT_EXPORT_HEADER_SIZE];
    uint32_t write_calls;
} header_capture_t;

static int expect(int condition, const char *message)
{
    test_count++;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static void make_event(event_record_t *event,
                       uint8_t pretrigger_blocks,
                       uint8_t posttrigger_blocks)
{
    uint8_t index;
    uint8_t block_count = (uint8_t)(pretrigger_blocks
                                    + posttrigger_blocks);
    uint32_t sequence = UINT32_MAX - UINT32_C(800);

    sample_block_pool_init(&test_pool);
    memset(event, 0, sizeof(*event));
    event->event_id = 42U;
    event->trigger_sample_index = 7U;
    event->pretrigger_block_count = pretrigger_blocks;
    event->posttrigger_block_count = posttrigger_blocks;
    event->block_count = block_count;
    for (index = 0U; index < block_count; index++)
    {
        sample_block_t *block = sample_block_pool_acquire(&test_pool);

        block->sequence = sequence;
        block->first_monotonic_us = UINT64_C(1000000)
                                    + (uint64_t)index * 20000U;
        block->sample_period_ns = 625000U;
        block->sample_count = 32U;
        (void)sample_block_pool_publish(&test_pool, block);
        block = sample_block_pool_take_ready(&test_pool);
        event->blocks[index] = block;
        sequence += 32U;
    }
    event->trigger_sequence =
        event->blocks[pretrigger_blocks]->sequence
        + event->trigger_sample_index;
    event->trigger_monotonic_us =
        event->blocks[pretrigger_blocks]->first_monotonic_us + 4375U;
}

static void set_trigger_facts(event_record_t *event)
{
    const sample_block_t *block =
        event->blocks[event->pretrigger_block_count];
    uint64_t offset_us = ((uint64_t)event->trigger_sample_index
                          * block->sample_period_ns) / 1000U;

    event->trigger_sequence = block->sequence + event->trigger_sample_index;
    event->trigger_monotonic_us = block->first_monotonic_us >
                                  UINT64_MAX - offset_us
                                      ? UINT64_MAX
                                      : block->first_monotonic_us + offset_us;
}

static int expect_quality(const event_record_t *event,
                          event_quality_serialization_t serialization,
                          event_quality_verdict_t verdict,
                          uint32_t reasons,
                          const char *message)
{
    event_quality_result_t result;

    if (!expect(event_quality_evaluate(&test_pool, event, &result)
                    == RT_EOK,
                message))
    {
        return 0;
    }
    return expect(result.serialization == serialization
                      && result.verdict == verdict
                      && result.reason_flags == reasons,
                  message);
}

static int test_pass_and_degraded_cases(void)
{
    event_record_t event;

    make_event(&event, 25U, 50U);
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_PASS, EVENT_QUALITY_REASON_NONE,
                        "fixed event must be PASS"))
    {
        return 0;
    }

    make_event(&event, 24U, 50U);
    event.flags = EVENT_FLAG_PRETRIGGER_SHORT;
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_DEGRADED,
                        EVENT_QUALITY_REASON_PRETRIGGER_SHORT,
                        "short pretrigger must be DEGRADED"))
    {
        return 0;
    }

    make_event(&event, 25U, 75U);
    event.flags = EVENT_FLAG_DURATION_CAPPED;
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_DEGRADED,
                        EVENT_QUALITY_REASON_DURATION_CAPPED,
                        "capped duration must be DEGRADED"))
    {
        return 0;
    }

    make_event(&event, 24U, 75U);
    event.flags = EVENT_FLAG_PRETRIGGER_SHORT | EVENT_FLAG_DURATION_CAPPED;
    return expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                          EVENT_QUALITY_DEGRADED,
                          EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                          | EVENT_QUALITY_REASON_DURATION_CAPPED,
                          "short and capped event must preserve both reasons");
}

static int test_invalid_cases(void)
{
    event_record_t event;

    make_event(&event, 25U, 50U);
    event.flags = EVENT_FLAG_DATA_LOSS;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "flag-only DATA_LOSS must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.flags = EVENT_FLAG_DATA_LOSS;
    for (uint8_t index = 30U; index < event.block_count; index++)
    {
        event.blocks[index]->sequence++;
    }
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_DATA_LOSS
                        | EVENT_QUALITY_REASON_SEQUENCE_GAP,
                        "sequence gap must dominate degraded reasons"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    for (uint8_t index = 30U; index < event.block_count; index++)
    {
        event.blocks[index]->sequence++;
    }
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "gap without DATA_LOSS flag must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[0]->sample_count = 31U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "31-sample block must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[74]->sample_count = 33U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "33-sample block must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.pretrigger_block_count = 24U;
    set_trigger_facts(&event);
    event.flags = EVENT_FLAG_PRETRIGGER_SHORT;
    return expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                          EVENT_QUALITY_INVALID,
                          EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                          | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                          "shape violation must dominate degraded reason");
}

static int test_shape_flags_are_consistent(void)
{
    event_record_t event;

    make_event(&event, 25U, 50U);
    event.pretrigger_block_count = 24U;
    event.posttrigger_block_count = 51U;
    set_trigger_facts(&event);
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                        | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                        "24 pre and 51 post without flags must be INVALID"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.flags = EVENT_FLAG_PRETRIGGER_SHORT;
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                        | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                        "short flag on 25 pre blocks must be INVALID"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.flags = EVENT_FLAG_DURATION_CAPPED;
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_DURATION_CAPPED
                        | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                        "capped flag on 50 post blocks must be INVALID"))
    {
        return 0;
    }

    make_event(&event, 24U, 50U);
    set_trigger_facts(&event);
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_PRETRIGGER_SHORT
                        | EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                        "24 pre blocks without short flag must be INVALID"))
    {
        return 0;
    }

    make_event(&event, 25U, 75U);
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_INVALID,
                        EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION,
                        "75 post blocks without capped flag must be INVALID"))
    {
        return 0;
    }

    return 1;
}

static int test_ai_requires_committed_equivalent_readback(void)
{
    event_record_t event;
    event_quality_result_t live;
    event_quality_result_t final;
    event_quality_facts_t readback;

    make_event(&event, 25U, 50U);
    if (!expect(event_quality_evaluate(&test_pool, &event, &live) == RT_EOK,
                "eligibility test must evaluate live event"))
    {
        return 0;
    }
    readback = live.live_facts;
    if (!expect(event_quality_check_round_trip(&live, &readback, &final)
                    == RT_EOK,
                "equal facts must complete round-trip check")
        || !expect(!final.ai_eligible,
                   "uncommitted and unverified facts must not be AI eligible"))
    {
        return 0;
    }
    final.committed = RT_TRUE;
    if (!expect(!event_quality_ai_eligible(&final),
                "unverified facts must not be AI eligible"))
    {
        return 0;
    }
    final.readback_verified = RT_TRUE;
    return expect(event_quality_ai_eligible(&final),
                  "committed equivalent verified facts must be AI eligible");
}

static int test_capture_failures(void)
{
    event_record_t event;

    make_event(&event, 25U, 50U);
    if (!expect_quality(RT_NULL, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "null event must be capture failure"))
    {
        return 0;
    }

    memset(&event, 0, sizeof(event));
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "zero block event must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.block_count = EVENT_MAX_BLOCK_COUNT + 1U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "oversized block count must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[10] = RT_NULL;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "null block must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[10] = (sample_block_t *)(uintptr_t)0x1234U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "wild block must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[10]->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY + 1U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "oversized block must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.trigger_sample_index = event.blocks[25]->sample_count;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "invalid trigger boundary must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.trigger_sequence++;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "wrong trigger sequence must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.trigger_monotonic_us++;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "wrong trigger time must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[25]->first_monotonic_us = UINT64_MAX - 1000U;
    set_trigger_facts(&event);
    if (!expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                        EVENT_QUALITY_PASS, EVENT_QUALITY_REASON_NONE,
                        "saturated trigger time must remain PASS"))
    {
        return 0;
    }
    event.trigger_monotonic_us = UINT64_MAX - 1U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "wrong saturated trigger time must fail"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[20]->sequence +=
        EVENT_MAX_BLOCK_COUNT * SAMPLE_BLOCK_SAMPLE_CAPACITY + 1U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "loss summary overflow must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[30]->sequence++;
    for (uint8_t index = 31U; index < 40U; index++)
    {
        event.blocks[index]->sequence =
            event.blocks[index - 1U]->sequence
            + event.blocks[index - 1U]->sample_count;
    }
    event.blocks[40]->sequence += 2U;
    for (uint8_t index = 41U; index < event.block_count; index++)
    {
        event.blocks[index]->sequence =
            event.blocks[index - 1U]->sequence
            + event.blocks[index - 1U]->sample_count;
    }
    event.blocks[39]->first_monotonic_us = 0U;
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "loss time regression must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.flags = UINT16_C(1U << 15);
    if (!expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                        EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                        "undefined event flags must be capture failure"))
    {
        return 0;
    }

    make_event(&event, 25U, 50U);
    event.blocks[10]->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    return expect_quality(&event, EVENT_QUALITY_CAPTURE_FAILURE,
                          EVENT_QUALITY_NO_VERDICT, EVENT_QUALITY_REASON_NONE,
                          "64-sample block must be capture failure");
}

static int test_sequence_wrap(void)
{
    event_record_t event;

    make_event(&event, 25U, 50U);
    event.blocks[0]->sequence = UINT32_MAX - UINT32_C(16);
    for (uint8_t index = 1U; index < event.block_count; index++)
    {
        event.blocks[index]->sequence = event.blocks[index - 1U]->sequence
                                        + event.blocks[index - 1U]->sample_count;
    }
    set_trigger_facts(&event);
    return expect_quality(&event, EVENT_QUALITY_SERIALIZABLE,
                          EVENT_QUALITY_PASS, EVENT_QUALITY_REASON_NONE,
                          "sequence wrap must remain continuous");
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static rt_err_t capture_header_write(const uint8_t *data,
                                     rt_size_t length,
                                     void *context)
{
    header_capture_t *capture = context;

    if (capture == RT_NULL || data == RT_NULL || length == 0U)
    {
        return -RT_ERROR;
    }
    if (capture->write_calls == 0U)
    {
        if (length != EVENT_EXPORT_HEADER_SIZE)
        {
            return -RT_ERROR;
        }
        memcpy(capture->header, data, EVENT_EXPORT_HEADER_SIZE);
    }
    capture->write_calls++;
    return RT_EOK;
}

static int make_pool_event(sample_block_pool_t *pool,
                           event_assembler_t *assembler,
                           uint8_t pretrigger_blocks,
                           uint8_t posttrigger_blocks)
{
    uint8_t index;
    uint8_t block_count = (uint8_t)(pretrigger_blocks
                                    + posttrigger_blocks);
    uint32_t sequence = UINT32_MAX - UINT32_C(800);

    sample_block_pool_init(pool);
    event_assembler_init(assembler);
    assembler->state = EVENT_EXPORTING;
    assembler->event.event_id = 42U;
    assembler->event.trigger_sample_index = 7U;
    assembler->event.pretrigger_block_count = pretrigger_blocks;
    assembler->event.posttrigger_block_count = posttrigger_blocks;
    assembler->event.block_count = block_count;
    for (index = 0U; index < block_count; index++)
    {
        sample_block_t *block = sample_block_pool_acquire(pool);

        if (block == RT_NULL)
        {
            return 0;
        }
        block->sequence = sequence;
        block->first_monotonic_us = UINT64_C(1000000)
                                    + (uint64_t)index * 20000U;
        block->sample_period_ns = 625000U;
        block->sample_count = 32U;
        if (sample_block_pool_publish(pool, block) != RT_EOK)
        {
            return 0;
        }
        block = sample_block_pool_take_ready(pool);
        if (block == RT_NULL)
        {
            return 0;
        }
        assembler->event.blocks[index] = block;
        sequence += 32U;
    }
    set_trigger_facts(&assembler->event);
    return 1;
}

static int capture_event_header(event_assembler_t *assembler,
                                sample_block_pool_t *pool,
                                header_capture_t *capture)
{
    if (!expect(event_export_debug_write(assembler, pool,
                                         capture_header_write, capture)
                    == RT_EOK,
                "event export must produce an EV03 header")
        || !expect(capture->write_calls != 0U,
                   "event export must call the capture sink"))
    {
        return 0;
    }
    return 1;
}

static int test_ev03_round_trip_case(uint8_t pretrigger_blocks,
                                     uint8_t posttrigger_blocks,
                                     uint16_t flags,
                                     int loss,
                                     int fixed_shape_violation)
{
    sample_block_pool_t pool;
    event_assembler_t assembler;
    header_capture_t capture = {0};
    event_quality_result_t live;
    event_quality_result_t final;
    event_quality_facts_t readback;
    uint8_t index;
    uint32_t ev01_length;

    if (!expect(make_pool_event(&pool, &assembler, pretrigger_blocks,
                                posttrigger_blocks),
                "round-trip event must be allocated"))
    {
        return 0;
    }
    assembler.event.flags = flags;
    if (loss)
    {
        for (index = 30U; index < assembler.event.block_count; index++)
        {
            assembler.event.blocks[index]->sequence++;
        }
    }
    if (fixed_shape_violation)
    {
        assembler.event.pretrigger_block_count = 24U;
        assembler.event.posttrigger_block_count = 51U;
        set_trigger_facts(&assembler.event);
    }
    if (!expect(event_quality_evaluate(&pool, &assembler.event, &live)
                    == RT_EOK,
                "round-trip live facts must evaluate"))
    {
        return 0;
    }
    if (!expect(capture_event_header(&assembler, &pool, &capture),
                "round-trip event must export"))
    {
        return 0;
    }
    ev01_length = EVENT_EXPORT_HEADER_SIZE
                   + get_u32_le(&capture.header[60]);
    if (!expect(event_export_debug_decode_header(
                    capture.header, sizeof(capture.header), ev01_length,
                    &readback) == RT_EOK,
                "exported EV03 header must decode"))
    {
        return 0;
    }
    if (fixed_shape_violation)
    {
        if (!expect(event_quality_facts_equal(&live.live_facts, &readback),
                    "allowed fixed-shape violation must round-trip")
            || !expect(event_quality_check_round_trip(&live, &readback, &final)
                           == RT_EOK,
                       "fixed-shape facts must complete round-trip check")
            || !expect(final.verdict == EVENT_QUALITY_INVALID
                           && (final.reason_flags
                               & EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION)
                                  != 0U
                           && !final.ai_eligible,
                       "fixed-shape facts must be INVALID and ineligible"))
        {
            return 0;
        }
        return 1;
    }

    if (!expect(event_quality_facts_equal(&live.live_facts, &readback),
                "EV03 readback facts must equal live facts")
        || !expect(event_quality_check_round_trip(&live, &readback, &final)
                       == RT_EOK,
                   "equal facts must complete round-trip check")
        || !expect((final.reason_flags
                    & EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH) == 0U,
                    "equal facts must not report a mismatch"))
    {
        return 0;
    }
    final.committed = RT_TRUE;
    final.readback_verified = RT_TRUE;
    return expect(event_quality_ai_eligible(&final)
                      == (live.verdict == EVENT_QUALITY_PASS),
                  "only a PASS round-trip may be AI eligible");
}

static int test_ev03_header_round_trips(void)
{
    sample_block_pool_t pool;
    event_assembler_t assembler;
    header_capture_t capture = {0};
    uint8_t mutated[EVENT_EXPORT_HEADER_SIZE];
    event_quality_facts_t facts;
    uint32_t ev01_length;

    if (!test_ev03_round_trip_case(25U, 50U, EVENT_FLAG_NONE, 0, 0)
        || !test_ev03_round_trip_case(24U, 50U,
                                      EVENT_FLAG_PRETRIGGER_SHORT, 0, 0)
        || !test_ev03_round_trip_case(25U, 75U,
                                      EVENT_FLAG_DURATION_CAPPED, 0, 0)
        || !test_ev03_round_trip_case(25U, 50U, EVENT_FLAG_DATA_LOSS, 1, 0)
        || !test_ev03_round_trip_case(25U, 50U, EVENT_FLAG_NONE, 0, 1))
    {
        return 0;
    }

    if (!expect(make_pool_event(&pool, &assembler, 25U, 50U),
                "header mutation event must be allocated"))
    {
        return 0;
    }
    if (!expect(capture_event_header(&assembler, &pool, &capture),
                "header mutation event must export"))
    {
        return 0;
    }
    ev01_length = EVENT_EXPORT_HEADER_SIZE
                   + get_u32_le(&capture.header[60]);
    memcpy(mutated, capture.header, sizeof(mutated));
    mutated[0] = 'X';
    if (!expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                 ev01_length, &facts)
                    != RT_EOK,
                "bad EV03 magic must be rejected"))
    {
        return 0;
    }
    memcpy(mutated, capture.header, sizeof(mutated));
    put_u16_le(&mutated[4], EVENT_EXPORT_VERSION + 1U);
    if (!expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                 ev01_length, &facts)
                    != RT_EOK,
                "bad EV03 version must be rejected"))
    {
        return 0;
    }
    memcpy(mutated, capture.header, sizeof(mutated));
    put_u16_le(&mutated[6], EVENT_EXPORT_HEADER_SIZE - 1U);
    if (!expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                 ev01_length, &facts)
                    != RT_EOK,
                "bad EV03 header length must be rejected"))
    {
        return 0;
    }
    memcpy(mutated, capture.header, sizeof(mutated));
    put_u16_le(&mutated[50], EVENT_FLAG_DATA_LOSS);
    if (!expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                 ev01_length, &facts)
                    != RT_EOK,
                "flag-only DATA_LOSS must be rejected"))
    {
        return 0;
    }
    memcpy(mutated, capture.header, sizeof(mutated));
    put_u32_le(&mutated[60], UINT32_MAX);
    if (!expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                 ev01_length, &facts)
                    != RT_EOK,
                "EV03 payload arithmetic overflow must be rejected"))
    {
        return 0;
    }
    memcpy(mutated, capture.header, sizeof(mutated));
    put_u16_le(&mutated[50], UINT16_C(1U << 15));
    return expect(event_export_debug_decode_header(mutated, sizeof(mutated),
                                                   ev01_length, &facts)
                      != RT_EOK,
                  "undefined EV03 flags must be rejected");
}

static int test_fault_injection_event_quality_seam(void)
{
    event_quality_result_t result;

    if (!expect(event_quality_assess_fault_injection_case(
                    EVENT_QUALITY_FAULT_SEQUENCE_GAP, &result) == RT_EOK
                    && result.serialization == EVENT_QUALITY_SERIALIZABLE
                    && result.verdict == EVENT_QUALITY_INVALID
                    && (result.reason_flags
                        & (EVENT_QUALITY_REASON_DATA_LOSS
                           | EVENT_QUALITY_REASON_SEQUENCE_GAP))
                           == (EVENT_QUALITY_REASON_DATA_LOSS
                               | EVENT_QUALITY_REASON_SEQUENCE_GAP)
                    && !result.ai_eligible,
                "sequence-gap injection must reach quality seam"))
    {
        return 0;
    }
    if (!expect(event_quality_assess_fault_injection_case(
                    EVENT_QUALITY_FAULT_PRETRIGGER_SHORT, &result) == RT_EOK
                    && result.verdict == EVENT_QUALITY_DEGRADED
                    && (result.reason_flags
                        & EVENT_QUALITY_REASON_PRETRIGGER_SHORT) != 0U,
                "pretrigger-short injection must be DEGRADED"))
    {
        return 0;
    }
    if (!expect(event_quality_assess_fault_injection_case(
                    EVENT_QUALITY_FAULT_DURATION_CAP, &result) == RT_EOK
                    && result.verdict == EVENT_QUALITY_DEGRADED
                    && (result.reason_flags
                        & EVENT_QUALITY_REASON_DURATION_CAPPED) != 0U,
                "duration-cap injection must be DEGRADED"))
    {
        return 0;
    }
    if (!expect(event_quality_assess_fault_injection_case(
                    EVENT_QUALITY_FAULT_POOL_PRESSURE, &result) == RT_EOK
                    && result.verdict == EVENT_QUALITY_INVALID
                    && (result.reason_flags
                        & EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION) != 0U,
                "pool-pressure injection must be INVALID"))
    {
        return 0;
    }
    return expect(event_quality_assess_fault_injection_case(
                       EVENT_QUALITY_FAULT_QUEUE_PRESSURE, &result) == RT_EOK
                       && result.verdict == EVENT_QUALITY_INVALID
                       && (result.reason_flags
                           & EVENT_QUALITY_REASON_FIXED_SHAPE_VIOLATION) != 0U,
                   "queue-pressure injection must be INVALID");
}

int main(void)
{
    if (!test_pass_and_degraded_cases()
        || !test_invalid_cases()
        || !test_shape_flags_are_consistent()
        || !test_ai_requires_committed_equivalent_readback()
        || !test_capture_failures()
        || !test_sequence_wrap()
        || !test_ev03_header_round_trips()
        || !test_fault_injection_event_quality_seam())
    {
        return 1;
    }
    printf("event quality: PASS (%u checks)\n", test_count);
    return 0;
}
