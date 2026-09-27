#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "event_assembler.h"
#include "event_export_debug.h"
#include "event_quality.h"
#include "power_policy.h"
#include "sample_block_pool.h"

#undef MSH_CMD_EXPORT
#define MSH_CMD_EXPORT(function, description)                                \
    static int (*const msh_export_##function)(int, char **)                  \
        __attribute__((unused)) = function

static power_mode_t test_power_mode;
static uint32_t test_power_blockers;
static uint32_t test_power_event_active_calls;
static sample_block_pool_t test_pool;
static rt_bool_t test_sink_ready;
static rt_err_t test_sink_begin_result;
static rt_err_t test_sink_write_result;
static rt_err_t test_sink_verify_result;
static rt_err_t test_sink_read_result;
static rt_err_t test_ai_submit_result;
static uint32_t test_sink_begin_calls;
static uint32_t test_sink_write_calls;
static uint32_t test_sink_verify_calls;
static uint32_t test_sink_read_calls;
static uint32_t test_sink_expected_length;
static uint32_t test_sink_fail_write_call;
static uint32_t test_ai_submit_calls;
static uint32_t test_ai_queue_pressure_accepted;
static rt_bool_t test_ai_queue_pressure_active;
static rt_bool_t test_ai_observed_readback;
static uint8_t test_header[EVENT_EXPORT_HEADER_SIZE];
static uint32_t test_count;

#define TEST_STANDARD_EVENT_WRITE_CALLS 151U

typedef enum
{
    TEST_READ_MUTATION_NONE = 0,
    TEST_READ_MUTATION_SHORT,
    TEST_READ_MUTATION_BAD_MAGIC,
    TEST_READ_MUTATION_BAD_VERSION,
    TEST_READ_MUTATION_DECODABLE_EVENT_ID,
    TEST_READ_MUTATION_DECODABLE_PRETRIGGER,
} test_read_mutation_t;

static test_read_mutation_t test_read_mutation;

#include "ai_inference_service.h"

#include "../../app/event/event_service.c"

void power_runtime_set_blocker(power_blocker_t blocker, rt_bool_t active)
{
    if (active)
    {
        test_power_blockers |= blocker;
    }
    else
    {
        test_power_blockers &= ~((uint32_t)blocker);
    }
}

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    test_power_blockers |= blocker;
    return RT_EOK;
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    test_power_blockers &= ~((uint32_t)blocker);
}

rt_err_t power_runtime_set_mode(power_mode_t mode)
{
    test_power_mode = mode;
    return RT_EOK;
}

rt_err_t power_runtime_event_active(rt_bool_t active)
{
    test_power_event_active_calls++;
    if (active)
    {
        test_power_blockers |= POWER_BLOCKER_EVENT;
        test_power_mode = POWER_MODE_EVENT_ACTIVE;
    }
    else
    {
        test_power_blockers &= ~((uint32_t)POWER_BLOCKER_EVENT);
        test_power_mode = POWER_MODE_MONITOR;
    }
    return RT_EOK;
}

void power_runtime_get_snapshot(power_policy_snapshot_t *snapshot)
{
    if (snapshot == RT_NULL)
    {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->mode = test_power_mode;
    snapshot->blockers = test_power_blockers;
}

uint64_t monotonic_clock_now_us(void)
{
    return 0U;
}

sample_block_pool_t *imu_acquisition_sample_pool(void)
{
    return &test_pool;
}

sample_block_t *imu_acquisition_take_ready_block(rt_int32_t timeout)
{
    (void)timeout;
    return RT_NULL;
}

void trigger_detector_init(trigger_detector_t *detector,
                            const trigger_detector_config_t *config)
{
    if (detector == RT_NULL)
    {
        return;
    }
    memset(detector, 0, sizeof(*detector));
    if (config != RT_NULL)
    {
        detector->config = *config;
    }
}

bool trigger_detector_feed(trigger_detector_t *detector,
                           const icm45686_fifo_sample_t *sample,
                           uint32_t sequence,
                           uint16_t sample_index,
                           trigger_fact_t *trigger)
{
    (void)detector;
    (void)sample;
    (void)sequence;
    (void)sample_index;
    (void)trigger;
    return false;
}

void health_service_get_snapshot(const health_service_t *service,
                                 health_snapshot_t *snapshot)
{
    (void)service;
    if (snapshot != RT_NULL)
    {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

rt_err_t ai_inference_service_submit_event(sample_block_pool_t *pool,
                                           const event_record_t *event)
{
    (void)pool;
    (void)event;
    test_ai_submit_calls++;
    if (test_sink_verify_calls != 0U && test_sink_read_calls != 0U)
    {
        test_ai_observed_readback = RT_TRUE;
    }
    if (test_ai_queue_pressure_active)
    {
        if (test_ai_submit_calls <= AI_INFERENCE_QUEUE_CAPACITY)
        {
            test_ai_queue_pressure_accepted++;
            return RT_EOK;
        }
        return -RT_ERROR;
    }
    return test_ai_submit_result;
}

rt_err_t ai_inference_service_arm_queue_pressure(void)
{
    if (test_ai_queue_pressure_active)
    {
        return -RT_EBUSY;
    }
    test_ai_queue_pressure_active = RT_TRUE;
    return RT_EOK;
}

void ai_inference_service_cancel_queue_pressure(void)
{
    test_ai_queue_pressure_active = RT_FALSE;
}

rt_bool_t ai_inference_service_queue_pressure_active(void)
{
    return test_ai_queue_pressure_active;
}

static int expect(int condition, const char *message)
{
    test_count++;
    if (!condition)
    {
        fprintf(stderr, "event quality service: %s\n", message);
        return 0;
    }
    return 1;
}

static rt_bool_t test_sink_is_ready(void *context)
{
    (void)context;
    return test_sink_ready;
}

static rt_err_t test_sink_begin(void *context,
                                uint32_t event_id,
                                uint32_t ev01_length)
{
    (void)context;
    (void)event_id;
    test_sink_begin_calls++;
    test_sink_expected_length = ev01_length;
    return test_sink_begin_result;
}

static rt_err_t test_sink_write(const uint8_t *data,
                                rt_size_t length,
                                void *context)
{
    (void)context;
    test_sink_write_calls++;
    if (test_sink_fail_write_call != 0U
        && test_sink_write_calls == test_sink_fail_write_call)
    {
        return -RT_ERROR;
    }
    if (data != RT_NULL && length == EVENT_EXPORT_HEADER_SIZE)
    {
        memcpy(test_header, data, sizeof(test_header));
    }
    return test_sink_write_result;
}

static rt_err_t test_sink_abort(void *context)
{
    (void)context;
    return test_ai_submit_result;
}

static rt_err_t test_sink_get_status(void *context,
                                     event_export_sink_status_t *status)
{
    (void)context;
    if (status == RT_NULL)
    {
        return -RT_ERROR;
    }
    status->state = EVENT_EXPORT_SINK_READY;
    status->next_event_id = 1U;
    return RT_EOK;
}

static rt_err_t test_sink_verify(void *context, uint32_t event_id)
{
    (void)context;
    (void)event_id;
    test_sink_verify_calls++;
    return test_sink_verify_result;
}

static rt_err_t test_sink_read(void *context,
                               uint32_t event_id,
                               uint32_t ev01_offset,
                               uint8_t *data,
                               uint32_t length,
                               uint32_t *read_length)
{
    (void)context;
    (void)event_id;
    test_sink_read_calls++;
    if (test_sink_read_result != RT_EOK || data == RT_NULL
        || read_length == RT_NULL || ev01_offset != 0U
        || length != EVENT_EXPORT_HEADER_SIZE)
    {
        return test_sink_read_result;
    }
    memcpy(data, test_header, EVENT_EXPORT_HEADER_SIZE);
    if (test_read_mutation == TEST_READ_MUTATION_BAD_MAGIC)
    {
        data[0] = 'X';
    }
    else if (test_read_mutation == TEST_READ_MUTATION_BAD_VERSION)
    {
        data[4]++;
    }
    else if (test_read_mutation == TEST_READ_MUTATION_DECODABLE_EVENT_ID)
    {
        data[8]++;
    }
    else if (test_read_mutation == TEST_READ_MUTATION_DECODABLE_PRETRIGGER)
    {
        data[40] = 0U;
        data[41] = 3U;
        data[42] = 0U;
        data[43] = 0U;
        data[44] = 0x60U;
        data[45] = 0x06U;
        data[46] = 0U;
        data[47] = 0U;
    }
    *read_length = test_read_mutation == TEST_READ_MUTATION_SHORT
                       ? length - 1U
                       : length;
    return RT_EOK;
}

static void set_ready_trigger_facts(void)
{
    const sample_block_t *block =
        event_assembler.event.blocks[event_assembler.event.pretrigger_block_count];
    uint64_t offset_us = ((uint64_t)event_assembler.event.trigger_sample_index
                          * block->sample_period_ns) / 1000U;

    event_assembler.event.trigger_sequence =
        block->sequence + event_assembler.event.trigger_sample_index;
    event_assembler.event.trigger_monotonic_us = block->first_monotonic_us >
                                                 UINT64_MAX - offset_us
                                                     ? UINT64_MAX
                                                     : block->first_monotonic_us
                                                       + offset_us;
}

static void reset_fixture(void)
{
    sample_block_pool_init(&test_pool);
    event_assembler_init(&event_assembler);
    (void)rt_mutex_init(&event_service_mutex, "event", RT_IPC_FLAG_PRIO);
    event_service_mutex_initialized = RT_TRUE;
    event_sink = (event_export_sink_t){
        .is_ready = test_sink_is_ready,
        .begin = test_sink_begin,
        .write = test_sink_write,
        .abort = test_sink_abort,
        .get_status = test_sink_get_status,
        .verify = test_sink_verify,
        .read = test_sink_read,
        .context = RT_NULL,
    };
    event_sink_configured = RT_TRUE;
    event_service_started = RT_TRUE;
    event_health_service = RT_NULL;
    event_export_error_count = 0U;
    event_power_activation_pending = RT_FALSE;
    event_power_active_owned = RT_FALSE;
    test_power_mode = POWER_MODE_MONITOR;
    test_power_blockers = 0U;
    test_power_event_active_calls = 0U;
    test_sink_ready = RT_TRUE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    test_sink_verify_result = RT_EOK;
    test_sink_read_result = RT_EOK;
    test_sink_begin_calls = 0U;
    test_sink_write_calls = 0U;
    test_sink_verify_calls = 0U;
    test_sink_read_calls = 0U;
    test_sink_expected_length = 0U;
    test_sink_fail_write_call = 0U;
    test_ai_submit_calls = 0U;
    test_ai_queue_pressure_accepted = 0U;
    test_ai_queue_pressure_active = RT_FALSE;
    test_ai_submit_result = RT_EOK;
    test_ai_observed_readback = RT_FALSE;
    test_read_mutation = TEST_READ_MUTATION_NONE;
    memset(test_header, 0, sizeof(test_header));
}

static int prepare_ready_event(uint8_t pretrigger_blocks,
                               uint8_t posttrigger_blocks,
                               uint16_t flags)
{
    uint8_t index;
    uint8_t block_count = (uint8_t)(pretrigger_blocks
                                    + posttrigger_blocks);

    event_assembler.event.event_id = 1U;
    event_assembler.event.trigger_sample_index = 0U;
    event_assembler.event.pretrigger_block_count = pretrigger_blocks;
    event_assembler.event.posttrigger_block_count = posttrigger_blocks;
    event_assembler.event.flags = flags;
    event_assembler.event.block_count = 0U;
    for (index = 0U; index < block_count; index++)
    {
        sample_block_t *block = sample_block_pool_acquire(&test_pool);

        if (block == RT_NULL)
        {
            return 0;
        }
        block->sequence = (uint32_t)index * 32U;
        block->first_monotonic_us = (uint64_t)index * 20000U;
        block->sample_period_ns = 625000U;
        block->sample_count = 32U;
        if (sample_block_pool_publish(&test_pool, block) != RT_EOK)
        {
            return 0;
        }
        block = sample_block_pool_take_ready(&test_pool);
        if (block == RT_NULL)
        {
            return 0;
        }
        event_assembler.event.blocks[index] = block;
        event_assembler.event.block_count++;
    }
    set_ready_trigger_facts();
    event_assembler.state = EVENT_READY_FOR_EXPORT;
    return 1;
}

static sample_block_t *prepare_stream_block(uint32_t index)
{
    sample_block_t *block = sample_block_pool_acquire(&test_pool);

    if (block == RT_NULL)
    {
        return RT_NULL;
    }
    block->sequence = index * 32U;
    block->first_monotonic_us = (uint64_t)index * 20000U;
    block->sample_period_ns = 625000U;
    block->sample_count = 32U;
    memset(block->samples, 0, sizeof(block->samples));
    if (sample_block_pool_publish(&test_pool, block) != RT_EOK)
    {
        (void)sample_block_pool_abandon(&test_pool, block);
        return RT_NULL;
    }
    return sample_block_pool_take_ready(&test_pool);
}

static sample_block_t *prepare_ready_backlog_block(uint32_t index)
{
    sample_block_t *block = sample_block_pool_acquire(&test_pool);

    if (block == RT_NULL)
    {
        return RT_NULL;
    }
    block->sequence = index * 32U;
    block->first_monotonic_us = (uint64_t)index * 20000U;
    block->sample_period_ns = 625000U;
    block->sample_count = 32U;
    memset(block->samples, 0, sizeof(block->samples));
    if (sample_block_pool_publish(&test_pool, block) != RT_EOK)
    {
        (void)sample_block_pool_abandon(&test_pool, block);
        return RT_NULL;
    }
    return block;
}

static rt_err_t consume_stream_block_result(uint32_t index)
{
    sample_block_t *block = prepare_stream_block(index);
    trigger_fact_t trigger;
    const trigger_fact_t *trigger_or_null;

    if (block == RT_NULL)
    {
        return -RT_ERROR;
    }
    trigger_or_null = event_service_find_trigger(block, &trigger);
    return event_service_process_block_locked(block, trigger_or_null);
}

static int consume_stream_block(uint32_t index)
{
    return consume_stream_block_result(index) == RT_EOK;
}

static void add_gap_without_data_loss_flag(void)
{
    uint8_t index;

    event_assembler.event.blocks[30]->sequence++;
    for (index = 31U; index < event_assembler.event.block_count; index++)
    {
        event_assembler.event.blocks[index]->sequence =
            event_assembler.event.blocks[index - 1U]->sequence
            + event_assembler.event.blocks[index - 1U]->sample_count;
    }
    event_assembler.event.flags &= (uint16_t)~EVENT_FLAG_DATA_LOSS;
}

static int test_pass_is_submitted_after_readback(void)
{
    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "PASS event must be prepared")
        || !expect(event_service_export_ready() == RT_EOK,
                   "PASS event must be exported")
        || !expect(test_sink_begin_calls == 1U
                       && test_sink_write_calls != 0U
                       && test_sink_verify_calls == 1U
                       && test_sink_read_calls == 1U
                       && test_ai_submit_calls == 1U
                       && test_ai_observed_readback,
                   "AI must follow committed readback")
        || !expect(sample_block_pool_free_count(&test_pool)
                       == SAMPLE_BLOCK_POOL_SIZE,
                   "PASS path must restore the block pool"))
    {
        return 0;
    }
    return 1;
}

static int test_non_pass_and_capture_failure_skip_ai(void)
{
    reset_fixture();
    if (!expect(prepare_ready_event(24U, 50U,
                                    EVENT_FLAG_PRETRIGGER_SHORT),
                "DEGRADED event must be prepared")
        || !expect(event_service_export_ready() == RT_EOK,
                   "DEGRADED event must be persisted")
        || !expect(test_sink_begin_calls == 1U && test_ai_submit_calls == 0U,
                   "DEGRADED event must not reach AI")
        || !expect(sample_block_pool_free_count(&test_pool)
                       == SAMPLE_BLOCK_POOL_SIZE,
                    "DEGRADED path must restore the block pool"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_DATA_LOSS),
                "flag-only DATA_LOSS event must be prepared"))
    {
        return 0;
    }
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome == EVENT_SERVICE_STORAGE_NONE
                    && event_last_quality_valid
                    && event_last_quality.serialization
                           == EVENT_QUALITY_CAPTURE_FAILURE
                    && test_sink_begin_calls == 0U
                    && test_sink_write_calls == 0U
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "flag-only DATA_LOSS must fail before storage and AI"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "summary-only event must be prepared"))
    {
        return 0;
    }
    add_gap_without_data_loss_flag();
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome == EVENT_SERVICE_STORAGE_NONE
                    && event_last_quality_valid
                    && event_last_quality.serialization
                           == EVENT_QUALITY_CAPTURE_FAILURE
                    && test_sink_begin_calls == 0U
                    && test_sink_write_calls == 0U
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "summary-only gap must fail before storage and AI"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "capture failure event must be prepared"))
    {
        return 0;
    }
    event_assembler.event.blocks[0]->sample_count =
        SAMPLE_BLOCK_SAMPLE_CAPACITY + 1U;
    return expect(event_service_export_ready() == -RT_ERROR
                       && test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && test_ai_submit_calls == 0U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "capture failure must not touch storage or AI");
}

static int test_pretrigger_short_injection_persists_degraded_event(void)
{
    event_quality_facts_t facts;
    event_quality_result_t quality;

    reset_fixture();
    if (!expect(event_service_inject_quality_case(
                    EVENT_QUALITY_FAULT_PRETRIGGER_SHORT) == RT_EOK,
                "pretrigger-short injection must arm a persisted event")
        || !expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                   "pretrigger-short injection needs a standard event")
        || !expect(event_service_export_ready() == RT_EOK,
                   "pretrigger-short event must be exported")
        || !expect(event_export_debug_decode_header(
                       test_header, sizeof(test_header),
                       test_sink_expected_length, &facts) == RT_EOK,
                   "pretrigger-short header must decode")
        || !expect(event_quality_assess_facts(&facts, &quality) == RT_EOK,
                   "pretrigger-short facts must assess"))
    {
        return 0;
    }
    return expect(quality.verdict == EVENT_QUALITY_DEGRADED
                      && (quality.reason_flags
                          & EVENT_QUALITY_REASON_PRETRIGGER_SHORT) != 0U
                      && facts.pretrigger_block_count == 24U
                      && test_ai_submit_calls == 0U
                      && sample_block_pool_free_count(&test_pool)
                             == SAMPLE_BLOCK_POOL_SIZE,
                  "pretrigger-short injection must persist DEGRADED and skip AI");
}

static int test_sequence_gap_injection_persists_invalid_event(void)
{
    event_quality_facts_t facts;
    event_quality_result_t quality;

    reset_fixture();
    if (!expect(event_service_inject_quality_case(
                    EVENT_QUALITY_FAULT_SEQUENCE_GAP) == RT_EOK,
                "sequence-gap injection must arm a persisted event")
        || !expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                   "sequence-gap injection needs a standard event")
        || !expect(event_service_export_ready() == RT_EOK,
                   "sequence-gap event must be exported")
        || !expect(event_export_debug_decode_header(
                       test_header, sizeof(test_header),
                       test_sink_expected_length, &facts) == RT_EOK,
                   "sequence-gap header must decode")
        || !expect(event_quality_assess_facts(&facts, &quality) == RT_EOK,
                   "sequence-gap facts must assess"))
    {
        return 0;
    }
    return expect(quality.verdict == EVENT_QUALITY_INVALID
                      && (quality.reason_flags
                          & (EVENT_QUALITY_REASON_DATA_LOSS
                             | EVENT_QUALITY_REASON_SEQUENCE_GAP))
                             == (EVENT_QUALITY_REASON_DATA_LOSS
                                 | EVENT_QUALITY_REASON_SEQUENCE_GAP)
                      && facts.loss.lost_sample_count == 1U
                      && test_ai_submit_calls == 0U,
                  "sequence-gap injection must persist INVALID and skip AI");
}

static int test_duration_cap_injection_consumes_stream_and_persists_degraded(void)
{
    event_quality_facts_t facts;
    event_quality_result_t quality;
    uint32_t index;

    reset_fixture();
    for (index = 0U; index < EVENT_PRETRIGGER_BLOCK_COUNT; index++)
    {
        if (!consume_stream_block(index))
        {
            return expect(0, "duration-cap pretrigger stream must be consumed");
        }
    }
    if (!expect(event_assembler.pretrigger.count == EVENT_PRETRIGGER_BLOCK_COUNT,
                "duration-cap injection must retain a full pretrigger")
        || !expect(event_service_inject_quality_case(
                       EVENT_QUALITY_FAULT_DURATION_CAP) == RT_EOK,
                   "duration-cap injection must arm a streamed event"))
    {
        return 0;
    }

    for (index = EVENT_PRETRIGGER_BLOCK_COUNT;
         index < EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT;
         index++)
    {
        if (!consume_stream_block(index))
        {
            return expect(0, "duration-cap posttrigger stream must be consumed");
        }
        if (event_assembler.state == EVENT_READY_FOR_EXPORT)
        {
            break;
        }
    }
    if (!expect(event_assembler.state == EVENT_READY_FOR_EXPORT
                   && event_assembler.event.posttrigger_block_count
                          == EVENT_MAX_POST_BLOCK_COUNT
                   && event_assembler.event.block_count == EVENT_MAX_BLOCK_COUNT
                   && event_assembler.event.subtrigger_count != 0U
                   && (event_assembler.event.flags & EVENT_FLAG_DURATION_CAPPED)
                          != 0U,
                "duration-cap must be set by a streamed subtrigger")
        || !expect(event_service_export_ready() == RT_EOK,
                   "duration-cap event must be exported")
        || !expect(event_export_debug_decode_header(
                       test_header, sizeof(test_header),
                       test_sink_expected_length, &facts) == RT_EOK,
                   "duration-cap header must decode")
        || !expect(event_quality_assess_facts(&facts, &quality) == RT_EOK,
                   "duration-cap facts must assess"))
    {
        return 0;
    }
    return expect(quality.verdict == EVENT_QUALITY_DEGRADED
                      && (quality.reason_flags
                          & EVENT_QUALITY_REASON_DURATION_CAPPED) != 0U
                      && event_last_quality_valid
                      && event_last_storage_outcome
                             == EVENT_SERVICE_STORAGE_COMMITTED
                      && event_last_quality.verdict == EVENT_QUALITY_DEGRADED
                      && (event_last_quality.reason_flags
                          & EVENT_QUALITY_REASON_DURATION_CAPPED) != 0U
                      && event_last_quality.committed
                      && event_last_quality.readback_verified
                      && !event_last_quality.ai_eligible
                      && event_quality_facts_equal(
                             &event_last_quality.live_facts,
                             &event_last_quality.readback_facts)
                      && test_ai_submit_calls == 0U
                      && sample_block_pool_free_count(&test_pool)
                             == SAMPLE_BLOCK_POOL_SIZE,
                  "duration-cap must persist DEGRADED, skip AI, and restore blocks");
}

static int test_duration_cap_clear_cancels_pending_injection(void)
{
    uint32_t index;
    int passed = 1;

    reset_fixture();
    for (index = 0U; index < EVENT_PRETRIGGER_BLOCK_COUNT; index++)
    {
        if (!consume_stream_block(index))
        {
            return expect(0, "clear test pretrigger stream must be consumed");
        }
    }
    if (!expect(event_service_inject_quality_case(
                    EVENT_QUALITY_FAULT_DURATION_CAP) == RT_EOK,
                "clear test duration-cap injection must arm")
        || !expect(event_assembler.state == EVENT_ARMED,
                   "clear test injection must start armed"))
    {
        return 0;
    }
    for (index = EVENT_PRETRIGGER_BLOCK_COUNT;
         index < EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT;
         index++)
    {
        if (!consume_stream_block(index))
        {
            return expect(0, "clear test duration-cap stream must be consumed");
        }
        if (event_assembler.state == EVENT_READY_FOR_EXPORT)
        {
            break;
        }
    }
    if (!expect(event_assembler.state == EVENT_READY_FOR_EXPORT,
                "clear test duration-cap event must become ready")
        || !expect(event_service_clear_ready() == RT_EOK,
                   "clear test must clear the ready event"))
    {
        return 0;
    }
    passed &= expect(event_fault_injection_case_pending == 0U,
                     "clear-ready must cancel duration-cap pending injection");

    for (index = EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT;
         index < EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT
                       + EVENT_PRETRIGGER_BLOCK_COUNT;
         index++)
    {
        if (!consume_stream_block(index))
        {
            passed &= expect(0, "next event pretrigger stream must be consumed");
            break;
        }
    }
    passed &= expect(event_service_request_test_trigger() == RT_EOK,
                     "next normal event must arm a test trigger");
    for (;
         index < EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT
                       + EVENT_PRETRIGGER_BLOCK_COUNT + EVENT_MAX_POST_BLOCK_COUNT;
         index++)
    {
        if (!consume_stream_block(index))
        {
            passed &= expect(0, "next normal event stream must be consumed");
            break;
        }
        if (event_assembler.state == EVENT_READY_FOR_EXPORT)
        {
            break;
        }
    }
    passed &= expect(event_assembler.state == EVENT_READY_FOR_EXPORT
                         && event_assembler.event.posttrigger_block_count
                                == EVENT_STANDARD_POST_BLOCK_COUNT
                         && (event_assembler.event.flags
                             & EVENT_FLAG_DURATION_CAPPED) == 0U,
                     "next normal event must not inherit duration cap");
    if (event_assembler.state == EVENT_READY_FOR_EXPORT)
    {
        passed &= expect(event_service_export_ready() == RT_EOK,
                         "next normal event must be exported");
        passed &= expect(sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                          "next normal event must restore the block pool");
    }
    return passed;
}

static int test_pool_pressure_rejects_without_event_and_recovers(void)
{
    const uint32_t pressure_stream_index = 0U;
    const uint32_t next_event_pretrigger_start = pressure_stream_index + 1U;
    const uint32_t next_event_trigger_index =
        next_event_pretrigger_start + EVENT_PRETRIGGER_BLOCK_COUNT;
    uint32_t resource_reject_count;
    uint32_t next_event_id;
    uint32_t free_before;
    uint32_t index;

    reset_fixture();
    free_before = sample_block_pool_free_count(&test_pool);
    resource_reject_count = event_assembler.resource_reject_count;
    next_event_id = event_assembler.next_event_id;
    if (!expect(free_before > SAMPLE_BLOCK_HANDOFF_COUNT,
                "pool-pressure test must have reserve capacity")
        || !expect(event_service_inject_quality_case(
                       EVENT_QUALITY_FAULT_POOL_PRESSURE) == RT_EOK,
                   "pool-pressure injection must arm the real service")
        || !expect(event_assembler.state == EVENT_ARMED
                       && sample_block_pool_free_count(&test_pool)
                              == free_before,
                   "pool-pressure arm must wait for a producer block"))
    {
        return 0;
    }

    if (!expect(consume_stream_block_result(pressure_stream_index) == -RT_ERROR,
                "pool-pressure producer block must hit assembler rejection")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_assembler.resource_reject_count
                              == resource_reject_count + 1U
                       && event_assembler.next_event_id == next_event_id
                       && event_assembler.event.event_id == 0U,
                   "pool-pressure rejection must not create an event")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && test_sink_verify_calls == 0U
                       && test_sink_read_calls == 0U
                       && test_ai_submit_calls == 0U,
                   "pool-pressure rejection must not touch sink or AI")
        || !expect(sample_block_pool_free_count(&test_pool) == free_before,
                   "pool-pressure rejection must abandon all reserved blocks"))
    {
        return 0;
    }

    for (index = next_event_pretrigger_start;
         index < next_event_trigger_index;
         index++)
    {
        if (!expect(consume_stream_block_result(index) == RT_EOK,
                    "next normal event pretrigger block must be consumed"))
        {
            return 0;
        }
    }
    if (!expect(event_service_request_test_trigger() == RT_EOK,
                "next normal event must still arm a test trigger")
        || !expect(consume_stream_block_result(next_event_trigger_index)
                       == RT_EOK
                       && event_assembler.state
                              == EVENT_POST_TRIGGER_CAPTURING,
                   "next normal event must enter post-trigger capture"))
    {
        return 0;
    }
    for (index = next_event_trigger_index + 1U;
         index < next_event_trigger_index + EVENT_STANDARD_POST_BLOCK_COUNT;
         index++)
    {
        if (!expect(consume_stream_block_result(index) == RT_EOK,
                    "next normal event posttrigger block must be consumed"))
        {
            return 0;
        }
    }
    if (!expect(event_assembler.state == EVENT_READY_FOR_EXPORT
                   && event_assembler.event.event_id == next_event_id + 1U
                   && event_assembler.event.posttrigger_block_count
                          == EVENT_STANDARD_POST_BLOCK_COUNT,
                "next normal event must complete after rejection")
        || !expect(test_sink_begin_calls == 0U && test_ai_submit_calls == 0U,
                   "rejected injection must remain unpersisted before next export")
        || !expect(event_service_export_ready() == RT_EOK,
                   "next normal event must export")
        || !expect(test_sink_begin_calls == 1U && test_ai_submit_calls == 1U,
                   "only the next normal event may reach sink and AI")
        || !expect(sample_block_pool_free_count(&test_pool)
                       == SAMPLE_BLOCK_POOL_SIZE,
                   "next normal event must restore the block pool"))
    {
        return 0;
    }
    return 1;
}

static int test_pool_pressure_binds_rejection_to_ready_backlog_block(void)
{
    sample_block_t *backlog;
    sample_block_t *consuming;
    trigger_fact_t trigger;
    const trigger_fact_t *trigger_or_null;
    uint32_t free_before;
    uint32_t resource_reject_count;
    uint32_t next_event_id;

    reset_fixture();
    backlog = prepare_ready_backlog_block(0U);
    free_before = sample_block_pool_free_count(&test_pool);
    resource_reject_count = event_assembler.resource_reject_count;
    next_event_id = event_assembler.next_event_id;
    if (!expect(backlog != RT_NULL,
                "pool-pressure backlog block must be prepared")
        || !expect(event_service_inject_quality_case(
                       EVENT_QUALITY_FAULT_POOL_PRESSURE) == RT_EOK,
                   "pool-pressure must arm with a READY backlog")
        || !expect(sample_block_pool_free_count(&test_pool) == free_before,
                   "pool-pressure arm must not reserve ahead of a block")
        || !expect((consuming = sample_block_pool_take_ready(&test_pool))
                       == backlog,
                   "pool-pressure must process the injected backlog block"))
    {
        return 0;
    }

    trigger_or_null = event_service_find_trigger(consuming, &trigger);
    if (!expect(trigger_or_null != RT_NULL,
                "pool-pressure backlog must receive the test trigger")
        || !expect(event_service_process_block_locked(consuming,
                                                      trigger_or_null)
                       == -RT_ERROR,
                   "pool-pressure backlog must hit assembler rejection")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_assembler.resource_reject_count
                              == resource_reject_count + 1U
                       && event_assembler.next_event_id == next_event_id
                       && event_assembler.event.event_id == 0U,
                   "backlog rejection must not create an event")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && test_sink_verify_calls == 0U
                       && test_sink_read_calls == 0U
                       && test_ai_submit_calls == 0U,
                   "backlog rejection must not touch sink or AI")
        || !expect(sample_block_pool_free_count(&test_pool)
                       == SAMPLE_BLOCK_POOL_SIZE,
                   "backlog rejection must clean up reserved blocks"))
    {
        return 0;
    }
    return 1;
}

static int test_queue_pressure_streams_three_pass_events(void)
{
    uint32_t index = 0U;
    uint32_t event_round;
    uint32_t block_index;
    int passed = 1;

    reset_fixture();
    passed &= expect(event_service_inject_queue_pressure() == RT_EOK,
                     "queue-pressure must arm real event orchestration");
    for (event_round = 0U; event_round < AI_INFERENCE_QUEUE_CAPACITY + 1U;
         event_round++)
    {
        for (block_index = 0U;
             block_index < EVENT_PRETRIGGER_BLOCK_COUNT;
             block_index++)
        {
            passed &= expect(consume_stream_block(index++) == 1,
                             "queue-pressure pretrigger block must be consumed");
        }
        passed &= expect(event_assembler.state == EVENT_ARMED
                             && event_assembler.pretrigger.count
                                    == EVENT_PRETRIGGER_BLOCK_COUNT,
                         "queue-pressure must wait for a full pretrigger ring");
        for (block_index = 0U;
             block_index < EVENT_STANDARD_POST_BLOCK_COUNT;
             block_index++)
        {
            passed &= expect(consume_stream_block(index++) == 1,
                             "queue-pressure event block must be consumed");
        }
        passed &= expect(event_assembler.state == EVENT_READY_FOR_EXPORT
                             && event_assembler.event.pretrigger_block_count
                                    == EVENT_PRETRIGGER_BLOCK_COUNT,
                         "queue-pressure event must retain 25 pretrigger blocks");
        passed &= expect(event_service_export_ready() == RT_EOK,
                         "queue-pressure event must commit and read back");
        passed &= expect(event_last_quality_valid
                             && event_last_quality.verdict == EVENT_QUALITY_PASS
                             && event_last_quality.committed
                             && event_last_quality.readback_verified,
                         "queue-pressure event quality must remain PASS");
    }
    return passed && expect(test_sink_begin_calls == 3U
                                && test_sink_verify_calls == 3U
                                && test_sink_read_calls == 3U
                                && test_ai_submit_calls == 3U
                                && test_ai_queue_pressure_accepted == 2U
                                && !test_ai_queue_pressure_active
                                && event_assembler.state == EVENT_ARMED
                                && sample_block_pool_free_count(&test_pool)
                                       == SAMPLE_BLOCK_POOL_SIZE,
                            "queue-pressure must stop after the third rejection");
}

static int test_storage_failures_skip_ai(void)
{
    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "begin failure event must be prepared"))
    {
        return 0;
    }
    test_sink_begin_result = -RT_ERROR;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome
                           == EVENT_SERVICE_STORAGE_FAILURE
                    && !event_last_quality_valid
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "begin failure must skip AI and restore blocks"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "write failure event must be prepared"))
    {
        return 0;
    }
    test_sink_write_result = -RT_ERROR;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome
                           == EVENT_SERVICE_STORAGE_FAILURE
                    && !event_last_quality_valid
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "write failure must skip AI and restore blocks"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "verify failure event must be prepared"))
    {
        return 0;
    }
    test_sink_verify_result = -RT_ERROR;
    return expect(event_service_export_ready() == -RT_ERROR
                       && event_last_storage_outcome
                              == EVENT_SERVICE_STORAGE_FAILURE
                       && !event_last_quality_valid
                       && test_sink_verify_calls == 1U
                       && test_sink_read_calls == 0U
                       && test_ai_submit_calls == 0U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "verify failure must skip AI and restore blocks");
}

static int test_write_failure_at_call(uint32_t fail_call)
{
    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "write-failure event must be prepared"))
    {
        return 0;
    }
    test_sink_fail_write_call = fail_call;
    return expect(event_service_export_ready() == -RT_ERROR
                       && event_last_storage_outcome
                              == EVENT_SERVICE_STORAGE_FAILURE
                       && !event_last_quality_valid
                       && test_sink_write_calls == fail_call
                       && test_ai_submit_calls == 0U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "write failure must restore references and skip AI");
}

static int test_write_failures_cover_payload_and_final(void)
{
    return test_write_failure_at_call(2U)
           && test_write_failure_at_call(75U)
           && test_write_failure_at_call(TEST_STANDARD_EVENT_WRITE_CALLS);
}

static int test_corrupt_event_gate_is_bounded(void)
{
    sample_block_t *original_block;

    reset_fixture();
    if (!expect(prepare_ready_event(25U, EVENT_MAX_POST_BLOCK_COUNT,
                                    EVENT_FLAG_DURATION_CAPPED),
                "oversized event must be prepared")
        || !expect(event_assembler.event.block_count
                       == EVENT_MAX_BLOCK_COUNT,
                   "oversized test must start with a full event"))
    {
        return 0;
    }
    event_assembler.event.block_count = EVENT_MAX_BLOCK_COUNT + 1U;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && test_sink_begin_calls == 0U
                    && test_sink_write_calls == 0U
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "oversized block count must not overrun cleanup"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "wild-pointer event must be prepared"))
    {
        return 0;
    }
    original_block = event_assembler.event.blocks[0];
    event_assembler.event.blocks[0] =
        (sample_block_t *)(uintptr_t)0x1234U;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && test_sink_begin_calls == 0U
                    && test_sink_write_calls == 0U
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE - 1U,
                "wild block pointer must not be dereferenced"))
    {
        return 0;
    }
    return expect(sample_block_pool_release(&test_pool, original_block)
                       == RT_EOK
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "overwritten block remains recoverable by its owner");
}

static int test_loss_time_regression_fails_before_storage(void)
{
    uint8_t index;

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "loss-time event must be prepared"))
    {
        return 0;
    }
    event_assembler.event.blocks[30]->sequence++;
    for (index = 31U; index < 40U; index++)
    {
        event_assembler.event.blocks[index]->sequence =
            event_assembler.event.blocks[index - 1U]->sequence
            + event_assembler.event.blocks[index - 1U]->sample_count;
    }
    event_assembler.event.blocks[40]->sequence += 2U;
    for (index = 41U; index < event_assembler.event.block_count; index++)
    {
        event_assembler.event.blocks[index]->sequence =
            event_assembler.event.blocks[index - 1U]->sequence
            + event_assembler.event.blocks[index - 1U]->sample_count;
    }
    event_assembler.event.blocks[39]->first_monotonic_us = 0U;
    return expect(event_service_export_ready() == -RT_ERROR
                       && test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && test_ai_submit_calls == 0U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "loss-time regression must fail before storage");
}

static int test_ai_failure_does_not_leak_snapshot(void)
{
    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "AI-failure event must be prepared"))
    {
        return 0;
    }
    test_ai_submit_result = -RT_ERROR;
    return expect(event_service_export_ready() == RT_EOK
                       && event_last_storage_outcome
                              == EVENT_SERVICE_STORAGE_COMMITTED
                       && event_last_quality_valid
                       && event_last_quality.verdict == EVENT_QUALITY_PASS
                       && test_ai_submit_calls == 1U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "AI queue failure must not change quality or leak blocks");
}

static int test_repeated_exports_restore_pool(void)
{
    uint8_t index;

    reset_fixture();
    for (index = 0U; index < 6U; index++)
    {
        const rt_bool_t degraded = (index % 2U) != 0U;
        const uint8_t pretrigger_blocks = degraded ? 24U : 25U;
        const uint16_t flags = degraded ? EVENT_FLAG_PRETRIGGER_SHORT
                                        : EVENT_FLAG_NONE;

        if (!expect(prepare_ready_event(pretrigger_blocks, 50U, flags),
                    "repeated event must be prepared")
            || !expect(event_service_export_ready() == RT_EOK,
                       "repeated event must be exported")
            || !expect(sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                       "repeated event must restore the block pool"))
        {
            return 0;
        }
    }
    return expect(test_ai_submit_calls == 3U,
                  "only repeated PASS events must reach AI");
}

static int test_readback_failures_and_mismatch_skip_ai(void)
{
    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "short read event must be prepared"))
    {
        return 0;
    }
    test_read_mutation = TEST_READ_MUTATION_SHORT;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome
                           == EVENT_SERVICE_STORAGE_FAILURE
                    && !event_last_quality_valid
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "short read must skip AI and restore blocks"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "corrupt header event must be prepared"))
    {
        return 0;
    }
    test_read_mutation = TEST_READ_MUTATION_BAD_MAGIC;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome
                           == EVENT_SERVICE_STORAGE_FAILURE
                    && !event_last_quality_valid
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "corrupt header must skip AI and restore blocks"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "bad version event must be prepared"))
    {
        return 0;
    }
    test_read_mutation = TEST_READ_MUTATION_BAD_VERSION;
    if (!expect(event_service_export_ready() == -RT_ERROR
                    && event_last_storage_outcome
                           == EVENT_SERVICE_STORAGE_FAILURE
                    && !event_last_quality_valid
                    && test_ai_submit_calls == 0U
                    && sample_block_pool_free_count(&test_pool)
                           == SAMPLE_BLOCK_POOL_SIZE,
                "bad version must be storage failure"))
    {
        return 0;
    }

    reset_fixture();
    if (!expect(prepare_ready_event(25U, 50U, EVENT_FLAG_NONE),
                "round-trip mismatch event must be prepared"))
    {
        return 0;
    }
    test_read_mutation = TEST_READ_MUTATION_DECODABLE_PRETRIGGER;
    return expect(event_service_export_ready() == RT_EOK
                       && event_last_storage_outcome
                              == EVENT_SERVICE_STORAGE_COMMITTED
                       && event_last_quality_valid
                       && event_last_quality.verdict == EVENT_QUALITY_INVALID
                       && (event_last_quality.reason_flags
                           & EVENT_QUALITY_REASON_EVIDENCE_ROUND_TRIP_MISMATCH)
                              != 0U
                       && test_ai_submit_calls == 0U
                       && sample_block_pool_free_count(&test_pool)
                              == SAMPLE_BLOCK_POOL_SIZE,
                   "round-trip mismatch must skip AI and restore blocks");
}

int main(void)
{
    if (!test_pass_is_submitted_after_readback()
        || !test_non_pass_and_capture_failure_skip_ai()
        || !test_pretrigger_short_injection_persists_degraded_event()
        || !test_sequence_gap_injection_persists_invalid_event()
        || !test_duration_cap_injection_consumes_stream_and_persists_degraded()
        || !test_duration_cap_clear_cancels_pending_injection()
        || !test_pool_pressure_rejects_without_event_and_recovers()
        || !test_pool_pressure_binds_rejection_to_ready_backlog_block()
        || !test_queue_pressure_streams_three_pass_events()
        || !test_storage_failures_skip_ai()
        || !test_write_failures_cover_payload_and_final()
        || !test_corrupt_event_gate_is_bounded()
        || !test_loss_time_regression_fails_before_storage()
        || !test_ai_failure_does_not_leak_snapshot()
        || !test_repeated_exports_restore_pool()
        || !test_readback_failures_and_mismatch_skip_ai())
    {
        return 1;
    }
    printf("event quality service: PASS (%u checks)\n", test_count);
    return 0;
}
