#include <stdio.h>
#include <string.h>

#include "event_assembler.h"
#include "power_policy.h"
#include "sample_block_pool.h"

#undef MSH_CMD_EXPORT
#define MSH_CMD_EXPORT(function, description)                                \
    static int (*const msh_export_##function)(int, char **)                  \
        __attribute__((unused)) = function
static power_mode_t test_power_mode;
static uint32_t test_power_blockers;
static rt_err_t test_power_event_active_result;
static uint32_t test_power_event_active_calls;
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
    if (test_power_event_active_result != RT_EOK)
    {
        return test_power_event_active_result;
    }
    if (active)
    {
        test_power_blockers |= POWER_BLOCKER_EVENT;
        test_power_mode = POWER_MODE_EVENT_ACTIVE;
    }
    else
    {
        test_power_blockers &= ~((uint32_t)POWER_BLOCKER_EVENT);
        if (test_power_mode == POWER_MODE_EVENT_ACTIVE)
        {
            test_power_mode = POWER_MODE_MONITOR;
        }
    }
    return RT_EOK;
}

void power_runtime_get_snapshot(power_policy_snapshot_t *snapshot)
{
    if (snapshot == RT_NULL)
    {
        return;
    }
    snapshot->mode = test_power_mode;
    snapshot->blockers = test_power_blockers;
    snapshot->sleep_attempt_count = 0U;
    snapshot->sleep_enter_count = 0U;
    snapshot->sleep_skip_count = 0U;
    snapshot->wake_count = 0U;
    snapshot->last_wake_reason = POWER_WAKE_UNKNOWN;
    snapshot->stop_compiled = RT_FALSE;
    snapshot->stop_runtime_allowed = RT_FALSE;
}

static sample_block_pool_t test_pool;
static rt_err_t clear_attempt_result;
static uint64_t test_monotonic_now_us;
static rt_bool_t test_sink_ready;
static rt_bool_t test_sink_begin_clear_attempt;
static rt_err_t test_sink_begin_result;
static rt_err_t test_sink_write_result;
static uint32_t test_sink_begin_calls;
static uint32_t test_sink_write_calls;
static uint32_t test_impact_feed_calls;
static uint32_t test_drop_feed_calls;

static sample_block_t *make_ready_sample_block(uint32_t sequence,
                                               uint16_t sample_count,
                                               int16_t accel_x,
                                               uint64_t first_monotonic_us)
{
    sample_block_t *block = sample_block_pool_acquire(&test_pool);
    uint16_t index;

    if (block == RT_NULL || sample_count == 0U
        || sample_count > SAMPLE_BLOCK_SAMPLE_CAPACITY)
    {
        return RT_NULL;
    }

    block->sequence = sequence;
    block->first_monotonic_us = first_monotonic_us;
    block->sample_period_ns = 625000U;
    block->sample_count = sample_count;
    for (index = 0U; index < sample_count; index++)
    {
        block->samples[index].accel[0] = accel_x;
    }
    if (sample_block_pool_publish(&test_pool, block) != RT_EOK)
    {
        return RT_NULL;
    }

    return sample_block_pool_take_ready(&test_pool);
}

uint64_t monotonic_clock_now_us(void)
{
    return test_monotonic_now_us;
}

static sample_block_t *make_ready_block(void)
{
    return make_ready_sample_block(0U, 1U, 0, 0U);
}

sample_block_t *imu_acquisition_take_ready_block(rt_int32_t timeout)
{
    (void)timeout;
    return RT_NULL;
}

sample_block_pool_t *imu_acquisition_sample_pool(void)
{
    return &test_pool;
}

void trigger_detector_init(trigger_detector_t *detector,
                           const trigger_detector_config_t *config)
{
    if (detector == RT_NULL)
    {
        return;
    }

    *detector = (trigger_detector_t){0};
    if (config != RT_NULL)
    {
        detector->config = *config;
    }
}

static uint64_t test_square_i16(int16_t value)
{
    const int64_t widened = (int64_t)value;

    return (uint64_t)(widened * widened);
}

bool trigger_detector_feed(trigger_detector_t *detector,
                           const icm45686_fifo_sample_t *sample,
                           uint32_t sequence,
                           uint16_t sample_index,
                           trigger_fact_t *trigger)
{
    uint64_t magnitude_sq;
    rt_bool_t hit;

    if (detector == &event_impact_detector)
    {
        test_impact_feed_calls++;
    }
    else if (detector == &event_drop_detector)
    {
        test_drop_feed_calls++;
    }

    if (detector == RT_NULL || sample == RT_NULL || trigger == RT_NULL
        || detector->config.consecutive_samples == 0U)
    {
        return RT_FALSE;
    }

    magnitude_sq = test_square_i16(sample->accel[0])
                   + test_square_i16(sample->accel[1])
                   + test_square_i16(sample->accel[2]);
    hit = (detector->config.comparison == TRIGGER_COMPARISON_BELOW)
          ? (magnitude_sq <= detector->config.threshold_magnitude_sq)
          : (magnitude_sq >= detector->config.threshold_magnitude_sq);
    if (!hit)
    {
        detector->consecutive_count = 0U;
        return RT_FALSE;
    }

    if (detector->consecutive_count < detector->config.consecutive_samples)
    {
        detector->consecutive_count++;
    }
    if (detector->consecutive_count < detector->config.consecutive_samples)
    {
        return RT_FALSE;
    }

    trigger->sample_sequence = sequence;
    trigger->sample_index = sample_index;
    trigger->magnitude_sq = (uint32_t)magnitude_sq;
    trigger->threshold_magnitude_sq = detector->config.threshold_magnitude_sq;
    trigger->axis_mask = sample->accel[0] != 0 ? TRIGGER_AXIS_X : 0U;
    return RT_TRUE;
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
    (void)ev01_length;
    test_sink_begin_calls++;
    if (test_sink_begin_clear_attempt)
    {
        clear_attempt_result = event_service_clear_ready();
    }
    return test_sink_begin_result;
}

static rt_err_t test_sink_write(const uint8_t *data,
                                rt_size_t length,
                                void *context)
{
    (void)data;
    (void)length;
    (void)context;
    test_sink_write_calls++;
    return test_sink_write_result;
}

static rt_err_t test_sink_abort(void *context)
{
    (void)context;
    return RT_EOK;
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

static void reset_event_service_fixture(void)
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
        .context = RT_NULL,
    };
    event_sink_configured = RT_TRUE;
    event_service_started = RT_TRUE;
    event_health_service = RT_NULL;
    event_test_trigger_pending = RT_FALSE;
    event_test_trigger_deadline_us = 0U;
    event_natural_trigger_latched = RT_FALSE;
    event_cooldown_rejection_latched = RT_FALSE;
    event_natural_cooldown_deadline_us = 0U;
    event_natural_accepted_count = 0U;
    event_cooldown_rejection_count = 0U;
    event_export_error_count = 0U;
    event_processed_block_count = 0U;
    event_power_activation_pending = RT_FALSE;
    event_power_active_owned = RT_FALSE;
    test_power_mode = POWER_MODE_MONITOR;
    test_power_blockers = 0U;
    test_power_event_active_result = RT_EOK;
    test_power_event_active_calls = 0U;
    test_sink_ready = RT_TRUE;
    test_sink_begin_clear_attempt = RT_TRUE;
    test_sink_begin_result = -RT_ERROR;
    test_sink_write_result = -RT_ERROR;
    clear_attempt_result = RT_EOK;
    test_sink_begin_calls = 0U;
    test_sink_write_calls = 0U;
    test_impact_feed_calls = 0U;
    test_drop_feed_calls = 0U;
}

static int start_test_event_service(void)
{
    event_service_started = RT_FALSE;
    return event_service_start(&event_sink) == RT_EOK;
}

static void fill_stack_block(sample_block_t *block,
                             uint32_t sequence,
                             uint16_t sample_count,
                             int16_t accel_x)
{
    uint16_t index;

    memset(block, 0, sizeof(*block));
    block->sequence = sequence;
    block->first_monotonic_us = test_monotonic_now_us;
    block->sample_period_ns = 625000U;
    block->sample_count = sample_count;
    for (index = 0U; index < sample_count; index++)
    {
        block->samples[index].accel[0] = accel_x;
    }
}

static sample_block_t *prepare_ready_event(void)
{
    sample_block_t *block = make_ready_sample_block(0U, 1U, 0, 0U);

    if (block != RT_NULL)
    {
        event_assembler.state = EVENT_READY_FOR_EXPORT;
        event_assembler.event.event_id = 1U;
        event_assembler.event.block_count = 1U;
        event_assembler.event.posttrigger_block_count = 1U;
        event_assembler.event.blocks[0] = block;
    }
    return block;
}

static int prepare_fixed_ready_event(void)
{
    uint8_t block_index;

    event_assembler.event.event_id = 1U;
    event_assembler.event.pretrigger_block_count = EVENT_PRETRIGGER_BLOCK_COUNT;
    event_assembler.event.posttrigger_block_count = 50U;
    event_assembler.event.block_count = 0U;
    for (block_index = 0U;
         block_index < EVENT_PRETRIGGER_BLOCK_COUNT + 50U;
         block_index++)
    {
        sample_block_t *block = make_ready_sample_block(
            (uint32_t)block_index * 32U,
            32U,
            0,
            (uint64_t)block_index * UINT64_C(20000));

        if (block == RT_NULL)
        {
            event_assembler.state = EVENT_READY_FOR_EXPORT;
            (void)event_assembler_clear(&event_assembler, &test_pool);
            return 0;
        }
        event_assembler.event.blocks[block_index] = block;
        event_assembler.event.block_count++;
    }
    event_assembler.state = EVENT_READY_FOR_EXPORT;
    return 1;
}

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "event service: %s\n", message);
        return 0;
    }
    return 1;
}

static int test_invalid_sample_count_is_rejected_before_export(void)
{
    sample_block_t *first;
    sample_block_t *second;

    reset_event_service_fixture();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(start_test_event_service(),
                "event service must start for invalid 64-sample event")
        || !expect(prepare_fixed_ready_event(),
                   "64-sample event must be ready"))
    {
        return 1;
    }

    first = event_assembler.event.blocks[0];
    first->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "a 64-sample event must be rejected")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U,
                   "a 64-sample event must not touch the sink")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_export_error_count == 1U
                       && event_natural_cooldown_deadline_us != 0U,
                   "a rejected 64-sample event must clear, count, and cool down"))
    {
        return 1;
    }

    reset_event_service_fixture();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(start_test_event_service(),
                "event service must restart for mixed event")
        || !expect(prepare_fixed_ready_event(),
                   "mixed event first block must be ready"))
    {
        return 1;
    }

    first = event_assembler.event.blocks[0];
    first->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    second = event_assembler.event.blocks[1];
    second->sample_count = 31U;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "a mixed-length event must be rejected")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U,
                   "a mixed-length event must not touch the sink")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_export_error_count == 1U
                       && sample_block_pool_free_count(&test_pool)
                          == SAMPLE_BLOCK_POOL_SIZE,
                   "a mixed-length event must be cleaned up and counted"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_invalid_event_metadata_is_rejected_before_export(void)
{
    reset_event_service_fixture();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(start_test_event_service(),
                "event service must start for invalid flag event")
        || !expect(prepare_fixed_ready_event(),
                   "flag event must be ready")
        || !expect(event_service_event_has_fixed_sample_count_locked()
                       == RT_TRUE,
                   "the fixed fixture must pass the shape validator"))
    {
        return 1;
    }

    event_assembler.event.flags = EVENT_FLAG_DATA_LOSS;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "an event with DATA_LOSS must be rejected")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && event_assembler.state == EVENT_ARMED
                       && event_export_error_count == 1U,
                   "DATA_LOSS must not enter the sink transaction"))
    {
        return 1;
    }

    reset_event_service_fixture();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(start_test_event_service(),
                "event service must restart for sequence event")
        || !expect(prepare_fixed_ready_event(),
                   "sequence event must be ready"))
    {
        return 1;
    }
    event_assembler.event.blocks[1]->sequence++;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "a sequence gap must be rejected")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && event_assembler.state == EVENT_ARMED,
                   "a sequence gap must not enter the sink transaction"))
    {
        return 1;
    }

    reset_event_service_fixture();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(start_test_event_service(),
                "event service must restart for period event")
        || !expect(prepare_fixed_ready_event(),
                   "period event must be ready"))
    {
        return 1;
    }
    event_assembler.event.blocks[1]->sample_period_ns++;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "a sample-period discontinuity must be rejected")
        || !expect(test_sink_begin_calls == 0U
                       && test_sink_write_calls == 0U
                       && event_assembler.state == EVENT_ARMED,
                   "a period discontinuity must not enter the sink transaction"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_validator_accepts_wrapped_block_sequence(void)
{
    uint32_t sequence = UINT32_MAX - UINT32_C(16);
    uint8_t block_index;

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for wrapped sequence validation")
        || !expect(prepare_fixed_ready_event(),
                   "wrapped sequence event must be ready"))
    {
        return 1;
    }

    for (block_index = 0U;
         block_index < EVENT_PRETRIGGER_BLOCK_COUNT + 50U;
         block_index++)
    {
        event_assembler.event.blocks[block_index]->sequence = sequence;
        sequence += IMU_SAMPLE_BATCH_SIZE;
    }
    if (!expect(event_service_event_has_fixed_sample_count_locked() == RT_TRUE,
                "the validator must accept uint32 sequence wrap")
        || !expect(event_service_clear_ready() == RT_EOK,
                   "wrapped sequence event cleanup must succeed"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_trigger_acceptance_copies_the_current_health_snapshot_once(void)
{
    health_service_t health = {0};
    health_inputs_t inputs = {0};
    trigger_fact_t trigger = {
        .sample_sequence = 7U,
        .magnitude_sq = 800U,
        .threshold_magnitude_sq = 100U,
        .axis_mask = TRIGGER_AXIS_X,
    };
    sample_block_t *block;

    sample_block_pool_init(&test_pool);
    event_assembler_init(&event_assembler);
    health_service_init(&health);
    inputs.acquisition.ready = RT_TRUE;
    inputs.event.ready = RT_TRUE;
    inputs.storage.ready = RT_TRUE;
    inputs.acquisition.progress_sequence = 1U;
    inputs.event.progress_sequence = 1U;
    inputs.storage.progress_sequence = 1U;
    inputs.utc_valid = RT_TRUE;
    inputs.utc_unix_seconds = INT64_C(1735689600);
    inputs.time_epoch_id = 3U;
    inputs.environment_valid = RT_TRUE;
    inputs.environment_fresh = RT_TRUE;
    inputs.temperature_centi_c = 2222;
    health_service_evaluate(&health, &inputs, 1000U);
    event_service_set_health_service(&health);

    block = make_ready_block();
    if (block == RT_NULL
        || event_service_process_block_locked(block, &trigger) != RT_EOK
        || event_assembler.event.health_snapshot.utc_unix_seconds != INT64_C(1735689600)
        || event_assembler.event.health_snapshot.temperature_centi_c != 2222
        || test_power_mode != POWER_MODE_EVENT_ACTIVE
        || (test_power_blockers & POWER_BLOCKER_EVENT) == 0U)
    {
        fputs("event service: accepted trigger did not freeze health snapshot\n", stderr);
        return 1;
    }

    inputs.utc_unix_seconds = 0;
    inputs.temperature_centi_c = -1;
    health_service_evaluate(&health, &inputs, 2000U);
    if (event_assembler.event.health_snapshot.utc_unix_seconds != INT64_C(1735689600)
        || event_assembler.event.health_snapshot.temperature_centi_c != 2222)
    {
        fputs("event service: pending event changed after health update\n", stderr);
        return 1;
    }

    event_assembler.state = EVENT_EXPORTING;
    event_assembler.event.health_snapshot.utc_unix_seconds = 44;
    block = make_ready_block();
    if (block == RT_NULL
        || event_service_process_block_locked(block, &trigger) != RT_EOK
        || event_assembler.event.health_snapshot.utc_unix_seconds != 44)
    {
        fputs("event service: a busy trigger overwrote frozen context\n", stderr);
        return 1;
    }

    event_service_set_health_service(RT_NULL);
    return 0;
}

static int test_trigger_resynchronizes_next_event_id_after_log_format(void)
{
    trigger_fact_t trigger = {
        .sample_sequence = 1U,
        .sample_index = 0U,
    };
    sample_block_t *block;

    sample_block_pool_init(&test_pool);
    event_assembler_init(&event_assembler);
    event_assembler.state = EVENT_ARMED;
    event_assembler.next_event_id = 204U;
    block = make_ready_block();
    if (block == RT_NULL
        || event_service_process_block_locked(block, &trigger) != RT_EOK
        || event_assembler.event.event_id != 1U)
    {
        fputs("event service: post-format event ID was not resynchronized\n",
              stderr);
        return 1;
    }

    event_assembler.state = EVENT_READY_FOR_EXPORT;
    if (event_service_clear_ready() != RT_EOK
        || test_power_mode != POWER_MODE_MONITOR
        || (test_power_blockers & POWER_BLOCKER_EVENT) != 0U)
    {
        fputs("event service: post-format test cleanup failed\n", stderr);
        return 1;
    }
    return 0;
}

static int test_delayed_manual_trigger_waits_for_deadline_sample(void)
{
    char *command[] = {"event", "trigger_delay", "3"};
    sample_block_t first_block = {0};
    sample_block_t second_block = {0};
    trigger_fact_t trigger = {0};

    event_service_started = RT_TRUE;
    event_assembler_init(&event_assembler);
    event_assembler.state = EVENT_ARMED;
    event_test_trigger_pending = RT_FALSE;
    event_test_trigger_deadline_us = 0U;
    test_monotonic_now_us = 0U;

    if (event(3, command) != RT_EOK)
    {
        fputs("event service: delayed trigger request rejected\n", stderr);
        return 1;
    }
    if (event_service_request_test_trigger() != -RT_ERROR)
    {
        fputs("event service: immediate trigger overwrote delayed request\n",
              stderr);
        return 1;
    }

    first_block.sequence = 100U;
    first_block.first_monotonic_us = 0U;
    first_block.sample_period_ns = 1000000U;
    first_block.sample_count = 3U;
    if (event_service_find_trigger(&first_block, &trigger) != RT_NULL)
    {
        fputs("event service: delayed trigger fired before deadline\n", stderr);
        return 1;
    }

    second_block.sequence = 103U;
    second_block.first_monotonic_us = 2000U;
    second_block.sample_period_ns = 1000000U;
    second_block.sample_count = 2U;
    if (event_service_find_trigger(&second_block, &trigger) == RT_NULL
        || trigger.sample_sequence != 104U
        || trigger.sample_index != 1U
        || event_test_trigger_deadline_us != 0U)
    {
        fputs("event service: delayed trigger did not fire at deadline sample\n",
              stderr);
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_manual_trigger_requires_ready_sink(void)
{
    sample_block_t block = {0};
    trigger_fact_t trigger = {0};

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for manual sink safety"))
    {
        return 1;
    }

    test_sink_ready = RT_FALSE;
    if (!expect(event_service_request_test_trigger() == -RT_ERROR,
                "manual trigger must reject an unavailable sink"))
    {
        return 1;
    }

    event_test_trigger_pending = RT_TRUE;
    fill_stack_block(&block, 1U, 1U, 0);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "a stale manual trigger must not bypass sink readiness"))
    {
        return 1;
    }

    test_sink_ready = RT_TRUE;
    if (!expect(event_service_find_trigger(&block, &trigger) != RT_NULL,
                "manual trigger must remain available once the sink is ready"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_cooldown_feeds_both_detectors_without_short_circuit(void)
{
    sample_block_t block = {0};
    trigger_fact_t trigger = {0};

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for detector feed safety")
        || !expect(prepare_ready_event() != RT_NULL,
                   "detector feed test event must be ready")
        || !expect(event_service_clear_ready() == RT_EOK,
                   "detector feed test must enter cooldown"))
    {
        return 1;
    }

    test_impact_feed_calls = 0U;
    test_drop_feed_calls = 0U;
    fill_stack_block(&block, 1U, 1U, 5120);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "a cooldown hit must be rejected")
        || !expect(test_impact_feed_calls == 1U
                       && test_drop_feed_calls == 1U,
                   "cooldown must feed both detectors on every sample"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_natural_resource_failure_releases_and_rearms(void)
{
    sample_block_t *block;
    sample_block_t *held[SAMPLE_BLOCK_POOL_SIZE - SAMPLE_BLOCK_HANDOFF_COUNT];
    trigger_fact_t trigger = {0};
    uint16_t index;

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for resource failure"))
    {
        return 1;
    }

    for (index = 0U; index < (uint16_t)(SAMPLE_BLOCK_POOL_SIZE
                                       - SAMPLE_BLOCK_HANDOFF_COUNT);
         index++)
    {
        held[index] = sample_block_pool_acquire(&test_pool);
        if (!expect(held[index] != RT_NULL,
                    "resource failure setup must hold pool blocks"))
        {
            return 1;
        }
    }

    block = make_ready_sample_block(1U, 2U, 5120, 0U);
    if (!expect(block != RT_NULL,
                "resource failure trigger block must be available")
        || !expect(event_service_find_trigger(block, &trigger) != RT_NULL,
                   "natural fact must be admitted before resource check")
        || !expect(event_service_process_block_locked(block, &trigger)
                       == -RT_ERROR,
                   "resource exhaustion must reject the event"))
    {
        return 1;
    }

    if (!expect(event_assembler.state == EVENT_ARMED,
                "resource failure must rearm the assembler")
        || !expect(event_assembler.resource_reject_count == 1U,
                   "resource failure must be counted")
        || !expect(event_impact_detector.consecutive_count == 0U
                       && event_drop_detector.consecutive_count == 0U,
                   "resource failure rearm must clear both detectors")
        || !expect(event_natural_cooldown_deadline_us != 0U,
                   "resource failure must start natural cooldown"))
    {
        return 1;
    }

    for (index = 0U; index < (uint16_t)(SAMPLE_BLOCK_POOL_SIZE
                                       - SAMPLE_BLOCK_HANDOFF_COUNT);
         index++)
    {
        (void)sample_block_pool_release(&test_pool, held[index]);
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_clear_without_ready_event_does_not_start_cooldown(void)
{
    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for empty clear")
        || !expect(event_service_clear_ready() == -RT_ERROR,
                   "clearing without an event must fail"))
    {
        return 1;
    }

    if (!expect(event_natural_cooldown_deadline_us == 0U,
                "an empty clear must not start cooldown"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_cooldown_deadline_saturates_without_wrap(void)
{
    event_service_stats_t stats;

    reset_event_service_fixture();
    test_monotonic_now_us = UINT64_MAX - UINT64_C(10000);
    if (!expect(start_test_event_service(),
                "event service must start for cooldown saturation")
        || !expect(prepare_ready_event() != RT_NULL,
                   "cooldown saturation event must be ready")
        || !expect(event_service_clear_ready() == RT_EOK,
                   "cooldown saturation clear must succeed")
        || !expect(event_natural_cooldown_deadline_us == UINT64_MAX,
                   "cooldown deadline must saturate at UINT64_MAX"))
    {
        return 1;
    }

    event_service_get_stats(&stats);
    if (!expect(stats.cooldown_remaining_ms == 10U,
                "saturated cooldown must report the remaining time"))
    {
        return 1;
    }

    test_monotonic_now_us = UINT64_MAX;
    event_service_get_stats(&stats);
    if (!expect(stats.cooldown_remaining_ms == 0U
                    && event_natural_cooldown_deadline_us == 0U,
                "saturated cooldown must expire at UINT64_MAX"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_natural_impact_profile_requires_ready_sink_and_two_hits(void)
{
    sample_block_t block;
    trigger_fact_t trigger = {0};

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for natural impact detection")
        || !expect(event_impact_detector.config.threshold_magnitude_sq
                       == UINT32_C(26214400),
                   "impact profile must use the 5120-count squared threshold")
        || !expect(event_impact_detector.config.consecutive_samples == 2U
                       && event_impact_detector.config.comparison
                          == TRIGGER_COMPARISON_ABOVE,
                   "impact profile must use two ABOVE samples"))
    {
        return 1;
    }

    fill_stack_block(&block, 1U, 1U, 2048);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "1g must not trigger impact"))
    {
        return 1;
    }
    fill_stack_block(&block, 2U, 1U, 3914);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "3914 counts must not trigger impact"))
    {
        return 1;
    }

    test_sink_ready = RT_FALSE;
    fill_stack_block(&block, 3U, 2U, 5120);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "natural impact must require a ready sink"))
    {
        return 1;
    }

    test_sink_ready = RT_TRUE;
    fill_stack_block(&block, 5U, 1U, 5120);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "the first impact sample after sink readiness must not trigger"))
    {
        return 1;
    }
    fill_stack_block(&block, 6U, 1U, 5120);
    if (!expect(event_service_find_trigger(&block, &trigger) != RT_NULL,
                "the second impact sample must trigger")
        || !expect(trigger.sample_sequence == 6U
                       && trigger.threshold_magnitude_sq == UINT32_C(26214400),
                   "impact trigger fact must retain sequence and threshold"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_natural_drop_profile_requires_eight_low_hits(void)
{
    sample_block_t block;
    trigger_fact_t trigger = {0};

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for natural drop detection")
        || !expect(event_drop_detector.config.threshold_magnitude_sq
                       == UINT32_C(2359296),
                   "drop profile must use the 1536-count squared threshold")
        || !expect(event_drop_detector.config.consecutive_samples == 8U
                       && event_drop_detector.config.comparison
                          == TRIGGER_COMPARISON_BELOW,
                   "drop profile must use eight BELOW samples"))
    {
        return 1;
    }

    fill_stack_block(&block, 10U, 1U, 2048);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "1g must not trigger drop"))
    {
        return 1;
    }
    fill_stack_block(&block, 11U, 7U, 1536);
    if (!expect(event_service_find_trigger(&block, &trigger) == RT_NULL,
                "seven low-g samples must not trigger drop"))
    {
        return 1;
    }
    fill_stack_block(&block, 18U, 1U, 1536);
    if (!expect(event_service_find_trigger(&block, &trigger) != RT_NULL,
                "the eighth low-g sample must trigger drop")
        || !expect(trigger.sample_sequence == 18U
                       && trigger.threshold_magnitude_sq == UINT32_C(2359296),
                   "drop trigger fact must retain sequence and threshold")
        || !expect(event_drop_detector.consecutive_count == 0U
                       && event_impact_detector.consecutive_count == 0U,
                   "accepted natural trigger must clear both detector counts"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_natural_trigger_is_one_shot_and_event_is_fixed_length(void)
{
    sample_block_t *block;
    trigger_fact_t trigger = {0};
    uint32_t sample_total = 0U;
    uint8_t block_index;
    unsigned int index;

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for fixed event capture"))
    {
        return 1;
    }

    for (index = 0U; index < EVENT_PRETRIGGER_BLOCK_COUNT; index++)
    {
        block = make_ready_sample_block(index * 32U, 32U, 0, 0U);
        if (!expect(block != RT_NULL
                       && event_service_process_block_locked(block, RT_NULL)
                          == RT_EOK,
                    "pretrigger block must be retained"))
        {
            return 1;
        }
    }

    block = make_ready_sample_block(EVENT_PRETRIGGER_BLOCK_COUNT * 32U,
                                    32U, 5120, 0U);
    if (!expect(block != RT_NULL,
                "natural trigger block must be available")
        || !expect(event_service_find_trigger(block, &trigger) != RT_NULL,
                   "the natural trigger must be admitted once")
        || !expect(event_service_process_block_locked(block, &trigger) == RT_EOK,
                   "the natural trigger block must start capture"))
    {
        return 1;
    }

    block = make_ready_sample_block((EVENT_PRETRIGGER_BLOCK_COUNT + 1U) * 32U,
                                    32U, 5120, 0U);
    if (!expect(block != RT_NULL,
                "post-trigger block must be available")
        || !expect(event_service_find_trigger(block, &trigger) == RT_NULL,
                   "POST capture must not admit a subtrigger")
        || !expect(event_service_process_block_locked(block, RT_NULL) == RT_EOK,
                   "post-trigger block must extend the fixed event"))
    {
        return 1;
    }

    for (index = 0U; index < 48U; index++)
    {
        block = make_ready_sample_block((EVENT_PRETRIGGER_BLOCK_COUNT + 2U
                                         + index) * 32U,
                                        32U, 0, 0U);
        if (!expect(block != RT_NULL
                       && event_service_process_block_locked(block, RT_NULL)
                          == RT_EOK,
                    "fixed post-trigger block must be captured"))
        {
            return 1;
        }
    }

    for (block_index = 0U;
         block_index < event_assembler.event.block_count;
         block_index++)
    {
        sample_total += event_assembler.event.blocks[block_index]->sample_count;
    }
    if (!expect(event_assembler.state == EVENT_READY_FOR_EXPORT,
                "fixed event must become ready after 50 post blocks")
        || !expect(event_assembler.event.pretrigger_block_count
                       == EVENT_PRETRIGGER_BLOCK_COUNT
                       && event_assembler.event.posttrigger_block_count == 50U
                       && event_assembler.event.block_count == 75U,
                   "natural event must contain 25 pre and 50 post blocks")
        || !expect(sample_total == 2400U,
                   "natural event must retain the 2400-sample AI contract")
        || !expect(event_assembler.event.subtrigger_count == 0U,
                   "one-shot natural capture must have no subtriggers"))
    {
        return 1;
    }

    if (!expect(event_service_clear_ready() == RT_EOK,
                "fixed event cleanup must re-arm the service"))
    {
        return 1;
    }
    event_service_started = RT_FALSE;
    return 0;
}

static int test_cooldown_rejects_once_then_accepts_at_deadline(void)
{
    event_service_stats_t stats;
    sample_block_t *block;
    sample_block_t deadline_block;
    trigger_fact_t trigger = {0};
    unsigned int attempt;

    reset_event_service_fixture();
    test_monotonic_now_us = UINT64_C(1000000);
    if (!expect(start_test_event_service(),
                "event service must start for cooldown test")
        || !expect(prepare_ready_event() != RT_NULL,
                   "cooldown test event must be ready")
        || !expect(event_service_clear_ready() == RT_EOK,
                   "clearing an event must start cooldown"))
    {
        return 1;
    }

    for (attempt = 0U; attempt < 2U; attempt++)
    {
        block = make_ready_sample_block(100U + attempt * 2U,
                                        2U, 5120, test_monotonic_now_us);
        if (!expect(block != RT_NULL,
                    "cooldown rejection block must be available")
            || !expect(event_service_find_trigger(block, &trigger) == RT_NULL,
                       "a qualified natural fact before deadline must be rejected")
            || !expect(event_service_process_block_locked(block, RT_NULL)
                           == RT_EOK,
                       "rejected natural block must still update pretrigger"))
        {
            return 1;
        }
    }

    event_service_get_stats(&stats);
    if (!expect(stats.cooldown_rejection_count == 1U,
                "cooldown must count at most one rejection per window")
        || !expect(stats.pretrigger_block_count == 2U,
                   "cooldown rejection must preserve pretrigger history")
        || !expect(stats.cooldown_remaining_ms == 30000U,
                   "cooldown status must expose remaining milliseconds"))
    {
        return 1;
    }

    test_monotonic_now_us = event_natural_cooldown_deadline_us;
    fill_stack_block(&deadline_block, 200U, 2U, 5120);
    if (!expect(event_service_find_trigger(&deadline_block, &trigger) != RT_NULL,
                "the first qualified sequence at the deadline must be accepted")
        || !expect(trigger.sample_sequence == 201U,
                   "deadline acceptance must use the second qualified sample"))
    {
        return 1;
    }
    event_service_get_stats(&stats);
    event_service_started = RT_FALSE;
    return !expect(stats.natural_trigger_count == 1U
                       && stats.cooldown_remaining_ms == 0U,
                   "cooldown status must clear when natural admission resumes");
}

static void seed_detector_counts(void)
{
    event_impact_detector.consecutive_count = 1U;
    event_drop_detector.consecutive_count = 3U;
}

static int test_rearm_paths_clear_detectors_and_restart_resets_policy(void)
{
    sample_block_t *block;

    reset_event_service_fixture();
    test_monotonic_now_us = UINT64_C(2000000);
    if (!expect(start_test_event_service(),
                "event service must start for successful export")
        || !expect(prepare_fixed_ready_event(),
                   "successful export event must be ready"))
    {
        return 1;
    }
    seed_detector_counts();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = RT_EOK;
    if (!expect(event_service_export_ready() == RT_EOK,
                "successful export must re-arm natural detection")
        || !expect(event_impact_detector.consecutive_count == 0U
                       && event_drop_detector.consecutive_count == 0U,
                   "successful export must clear both detector counts")
        || !expect(event_natural_cooldown_deadline_us != 0U,
                   "successful export must start cooldown"))
    {
        return 1;
    }

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for failed export")
        || !expect(prepare_fixed_ready_event(),
                   "failed export event must be ready"))
    {
        return 1;
    }
    seed_detector_counts();
    test_sink_begin_clear_attempt = RT_FALSE;
    test_sink_begin_result = RT_EOK;
    test_sink_write_result = -RT_ERROR;
    if (!expect(event_service_export_ready() == -RT_ERROR,
                "failed export must report an error")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_impact_detector.consecutive_count == 0U
                       && event_drop_detector.consecutive_count == 0U,
                   "failed export must re-arm and clear detector counts")
        || !expect(event_natural_cooldown_deadline_us != 0U,
                   "failed export must start cooldown"))
    {
        return 1;
    }

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start for failed clear")
        || !expect((block = prepare_ready_event()) != RT_NULL,
                   "failed clear event must be ready"))
    {
        return 1;
    }
    seed_detector_counts();
    (void)sample_block_pool_release(&test_pool, block);
    if (!expect(event_service_clear_ready() == -RT_ERROR,
                "failed clear must report its release error")
        || !expect(event_assembler.state == EVENT_ARMED
                       && event_impact_detector.consecutive_count == 0U
                       && event_drop_detector.consecutive_count == 0U,
                   "failed clear must re-arm and clear detector counts")
        || !expect(event_natural_cooldown_deadline_us != 0U,
                   "failed clear must start cooldown"))
    {
        return 1;
    }

    reset_event_service_fixture();
    if (!expect(start_test_event_service(),
                "event service must start before restart")
        || !expect(event_service_started, "service must be marked started"))
    {
        return 1;
    }
    seed_detector_counts();
    event_service_started = RT_FALSE;
    if (!expect(event_service_start(&event_sink) == RT_EOK,
                "service restart must succeed")
        || !expect(event_impact_detector.consecutive_count == 0U
                       && event_drop_detector.consecutive_count == 0U
                       && !event_natural_trigger_latched,
                   "restart must clear detector counts and one-shot state"))
    {
        return 1;
    }

    event_service_started = RT_FALSE;
    return 0;
}

static int test_event_callback_does_not_override_maintenance_or_fault(void)
{
    const power_mode_t protected_modes[] = {
        POWER_MODE_MAINTENANCE,
        POWER_MODE_FAULT_FALLBACK,
    };
    size_t index;

    for (index = 0U;
         index < sizeof(protected_modes) / sizeof(protected_modes[0]);
         index++)
    {
        sample_block_pool_init(&test_pool);
        event_assembler_init(&event_assembler);
        event_assembler.state = EVENT_READY_FOR_EXPORT;
        event_power_activation_pending = RT_FALSE;
        event_power_active_owned = RT_FALSE;
        test_power_mode = protected_modes[index];
        test_power_blockers = POWER_BLOCKER_EVENT;

        if (event_service_clear_ready() != RT_EOK
            || test_power_mode != protected_modes[index]
            || (test_power_blockers & POWER_BLOCKER_EVENT) != 0U)
        {
            fputs("event service: protected power mode was overwritten\n",
                  stderr);
            return 1;
        }
    }
    return 0;
}

static int test_event_power_activation_retries_after_boot(void)
{
    trigger_fact_t trigger = {
        .sample_sequence = 1U,
        .sample_index = 0U,
    };
    sample_block_t *block;

    reset_event_service_fixture();
    event_assembler.state = EVENT_ARMED;
    test_power_mode = POWER_MODE_BOOT;
    test_power_event_active_result = -RT_EINVAL;
    block = make_ready_block();
    if (!expect(block != RT_NULL
                    && event_service_process_block_locked(block, &trigger)
                           == RT_EOK,
                "BOOT event must still enter post capture")
        || !expect(test_power_event_active_calls == 1U
                       && test_power_mode == POWER_MODE_BOOT
                       && event_power_activation_pending,
                   "failed BOOT power activation must remain pending"))
    {
        return 1;
    }

    test_power_mode = POWER_MODE_MONITOR;
    test_power_event_active_result = RT_EOK;
    block = make_ready_block();
    if (!expect(block != RT_NULL
                    && event_service_process_block_locked(block, RT_NULL)
                           == RT_EOK,
                "post capture retry block must be accepted")
        || !expect(test_power_event_active_calls == 2U
                       && test_power_mode == POWER_MODE_EVENT_ACTIVE
                       && !event_power_activation_pending
                       && event_power_active_owned,
                   "post capture must retry power activation after BOOT"))
    {
        return 1;
    }

    event_assembler.state = EVENT_READY_FOR_EXPORT;
    if (!expect(event_service_clear_ready() == RT_EOK,
                "event clear after retry must not fail"))
    {
        return 1;
    }
    return expect(test_power_event_active_calls == 3U
                       && test_power_mode == POWER_MODE_MONITOR
                       && !event_power_active_owned,
                   "re-arm must release event power ownership")
               ? 0
               : 1;
}

static int test_event_power_activation_preserves_protected_modes(void)
{
    const power_mode_t protected_modes[] = {
        POWER_MODE_MAINTENANCE,
        POWER_MODE_FAULT_FALLBACK,
    };
    trigger_fact_t trigger = {
        .sample_sequence = 1U,
        .sample_index = 0U,
    };
    size_t index;

    for (index = 0U;
         index < sizeof(protected_modes) / sizeof(protected_modes[0]);
         index++)
    {
        sample_block_t *block;

        reset_event_service_fixture();
        event_assembler.state = EVENT_ARMED;
        test_power_mode = protected_modes[index];
        block = make_ready_block();
        if (!expect(block != RT_NULL
                        && event_service_process_block_locked(block, &trigger)
                               == RT_EOK,
                    "protected mode event must still capture")
            || !expect(test_power_event_active_calls == 0U
                           && test_power_mode == protected_modes[index]
                           && event_power_activation_pending,
                       "event must not override maintenance or fault"))
        {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    sample_block_t *block;

    reset_event_service_fixture();
    block = prepare_ready_event();
    if (block == RT_NULL)
    {
        fputs("event service: setup failed\n", stderr);
        return 1;
    }

    clear_attempt_result = RT_EOK;
    test_power_mode = POWER_MODE_MONITOR;
    test_power_blockers = 0U;

    if (event_service_export_ready() != -RT_ERROR
        || event_assembler.state != EVENT_ARMED
        || test_sink_begin_calls != 0U)
    {
        fputs("event service: invalid event entered an export transaction\n",
              stderr);
        return 1;
    }

    if (test_trigger_acceptance_copies_the_current_health_snapshot_once() != 0)
    {
        return 1;
    }

    if (test_invalid_sample_count_is_rejected_before_export() != 0)
    {
        return 1;
    }

    if (test_invalid_event_metadata_is_rejected_before_export() != 0)
    {
        return 1;
    }

    if (test_validator_accepts_wrapped_block_sequence() != 0)
    {
        return 1;
    }

    if (test_trigger_resynchronizes_next_event_id_after_log_format() != 0)
    {
        return 1;
    }

    if (test_delayed_manual_trigger_waits_for_deadline_sample() != 0)
    {
        return 1;
    }

    if (test_event_callback_does_not_override_maintenance_or_fault() != 0)
    {
        return 1;
    }

    if (test_event_power_activation_retries_after_boot() != 0
        || test_event_power_activation_preserves_protected_modes() != 0)
    {
        return 1;
    }

    if (test_manual_trigger_requires_ready_sink() != 0
        || test_cooldown_feeds_both_detectors_without_short_circuit() != 0
        || test_natural_resource_failure_releases_and_rearms() != 0
        || test_clear_without_ready_event_does_not_start_cooldown() != 0
        || test_cooldown_deadline_saturates_without_wrap() != 0
        || test_natural_impact_profile_requires_ready_sink_and_two_hits() != 0
        || test_natural_drop_profile_requires_eight_low_hits() != 0
        || test_natural_trigger_is_one_shot_and_event_is_fixed_length() != 0
        || test_cooldown_rejects_once_then_accepts_at_deadline() != 0
        || test_rearm_paths_clear_detectors_and_restart_resets_policy() != 0)
    {
        return 1;
    }

    puts("event service: PASS");
    return 0;
}
