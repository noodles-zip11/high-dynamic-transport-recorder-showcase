#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "event_export_debug.h"

typedef struct
{
    uint8_t bytes[256];
    rt_size_t length;
    uint32_t write_count;
} byte_sink_t;

typedef struct
{
    rt_size_t length;
    uint32_t write_count;
} write_count_sink_t;

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "event_export_debug: %s\n", message);
        return 0;
    }

    return 1;
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint64_t get_u64_le(const uint8_t *data)
{
    return (uint64_t)get_u32_le(data)
           | ((uint64_t)get_u32_le(&data[4]) << 32U);
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U)
                  ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

static rt_err_t write_to_sink(const uint8_t *data, rt_size_t length, void *context)
{
    byte_sink_t *sink = context;

    if (sink == RT_NULL || sink->length + length > sizeof(sink->bytes))
    {
        return -RT_ERROR;
    }

    memcpy(&sink->bytes[sink->length], data, length);
    sink->length += length;
    sink->write_count++;
    return RT_EOK;
}

static rt_err_t fail_write(const uint8_t *data, rt_size_t length, void *context)
{
    (void)data;
    (void)length;
    (void)context;
    return -RT_ERROR;
}

static rt_err_t count_write(const uint8_t *data, rt_size_t length, void *context)
{
    write_count_sink_t *sink = context;

    if (data == RT_NULL || sink == RT_NULL)
    {
        return -RT_ERROR;
    }
    sink->length += length;
    sink->write_count++;
    return RT_EOK;
}

static sample_block_t *make_export_block(sample_block_pool_t *pool)
{
    sample_block_t *block = sample_block_pool_acquire(pool);

    if (block == RT_NULL)
    {
        return RT_NULL;
    }

    block->sequence = 88U;
    block->first_monotonic_us = UINT64_C(123456000);
    block->sample_period_ns = 625000U;
    block->sample_count = 2U;
    block->samples[0].accel[0] = 1;
    block->samples[0].accel[1] = -2;
    block->samples[0].accel[2] = 3;
    block->samples[0].gyro[0] = -4;
    block->samples[0].gyro[1] = 5;
    block->samples[0].gyro[2] = -6;
    block->samples[0].timestamp = 0x1234U;
    block->samples[0].temperature = -7;
    block->samples[0].header = 0xA5U;
    block->samples[1].accel[0] = 0x1122;
    block->samples[1].accel[1] = 0x3344;
    block->samples[1].accel[2] = 0x5566;
    block->samples[1].gyro[0] = -0x1122;
    block->samples[1].gyro[1] = -0x3344;
    block->samples[1].gyro[2] = -0x5566;
    block->samples[1].timestamp = 0xBEEFU;
    block->samples[1].temperature = 8;
    block->samples[1].header = 0x5AU;
    if (sample_block_pool_publish(pool, block) != RT_EOK)
    {
        return RT_NULL;
    }

    return sample_block_pool_take_ready(pool);
}

static int test_exports_a_little_endian_ev03_golden_vector(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    sample_block_t *block;
    byte_sink_t sink = {0};

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    block = make_export_block(&pool);
    if (!expect(block != RT_NULL, "the golden-vector test must own one block"))
    {
        return 1;
    }

    assembler.state = EVENT_EXPORTING;
    assembler.event.event_id = 0x11223344U;
    assembler.event.trigger_monotonic_us = UINT64_C(123456625);
    assembler.event.trigger_sequence = 0x55667788U;
    assembler.event.trigger_sample_index = 1U;
    assembler.event.peak_magnitude_sq = 0x01020304U;
    assembler.event.threshold_magnitude_sq = 100U;
    assembler.event.subtrigger_count = 2U;
    assembler.event.flags = EVENT_FLAG_PRETRIGGER_SHORT;
    assembler.event.posttrigger_block_count = 1U;
    assembler.event.block_count = 1U;
    assembler.event.blocks[0] = block;
    assembler.event.health_snapshot.state = HEALTH_DEGRADED;
    assembler.event.health_snapshot.utc_valid = RT_TRUE;
    assembler.event.health_snapshot.environment_valid = RT_TRUE;
    assembler.event.health_snapshot.environment_fresh = RT_TRUE;
    assembler.event.health_snapshot.utc_unix_seconds = INT64_C(1735689600);
    assembler.event.health_snapshot.time_epoch_id = 9U;
    assembler.event.health_snapshot.power_state = HEALTH_POWER_STATE_UNAVAILABLE;
    assembler.event.health_snapshot.reset_raw_flags = 0xAABBCCDDU;
    assembler.event.health_snapshot.temperature_centi_c = -1234;
    assembler.event.health_snapshot.environment_age_seconds = 7U;
    assembler.event.health_snapshot.humidity_milli_rh = 56789U;
    assembler.event.health_snapshot.free_log_bytes = 0x01020304U;
    assembler.event.health_snapshot.sample_pool_min_free = 12U;
    assembler.event.health_snapshot.last_fault_code = 0x11223344U;
    assembler.event.health_snapshot.imu_transport_error_count = 21U;
    assembler.event.health_snapshot.imu_dma_error_count = 22U;
    assembler.event.health_snapshot.sample_pool_backpressure_count = 23U;
    assembler.event.health_snapshot.storage_error_count = 24U;
    assembler.event.health_snapshot.event_export_error_count = 25U;
    assembler.event.health_snapshot.transition_sequence = 0x55667788U;

    if (!expect(event_export_debug_write(&assembler, &pool, write_to_sink, &sink)
                == RT_EOK,
                "the writer must export a valid ready event")
        || !expect(sink.length == EVENT_EXPORT_HEADER_SIZE + 32U,
                   "the output must contain a 160-byte header and two 16-byte samples")
        || !expect(sink.write_count == 2U,
                   "the writer must send one header and one complete sample block")
        || !expect(memcmp(sink.bytes, "EV03", 4U) == 0,
                   "the header must start with the EV03 magic")
        || !expect(sink.bytes[4] == 3U && sink.bytes[5] == 0U
                   && sink.bytes[6] == EVENT_EXPORT_HEADER_SIZE && sink.bytes[7] == 0U,
                   "version and header length must be little endian")
        || !expect(sink.bytes[8] == 0x44U && sink.bytes[11] == 0x11U,
                   "the event identifier must be little endian")
        || !expect(sink.bytes[12] == 0x71U && sink.bytes[13] == 0xCCU
                   && sink.bytes[14] == 0x5BU && sink.bytes[15] == 0x07U,
                   "the trigger monotonic time must be encoded in microseconds")
        || !expect(sink.bytes[20] == 0x80U && sink.bytes[23] == 0x67U
                   && sink.bytes[27] == 0x00U,
                   "UTC seconds must use the documented little-endian offset")
        || !expect(sink.bytes[28] == 9U && sink.bytes[36] == 0x88U
                   && sink.bytes[39] == 0x55U,
                   "epoch and trigger sequence must retain their EV02 offsets")
        || !expect(sink.bytes[40] == 1U && sink.bytes[44] == 1U,
                   "the trigger sample must divide one pre and one post sample")
        || !expect(get_u32_le(&sink.bytes[60]) == 32U,
                   "payload length must be the exact sample byte count")
        || !expect(get_u32_le(&sink.bytes[64])
                       == crc32_bytes(&sink.bytes[160], 32U),
                   "payload CRC must cover the exact encoded sample bytes")
        || !expect(sink.bytes[68] == HEALTH_DEGRADED
                   && sink.bytes[69] == HEALTH_POWER_STATE_UNAVAILABLE
                   && sink.bytes[70] == 0x07U,
                   "health, power and context-valid flags must be frozen into EV03")
        || !expect(sink.bytes[72] == 0xDDU && sink.bytes[75] == 0xAAU
                   && sink.bytes[76] == 0x2EU && sink.bytes[77] == 0xFBU
                   && sink.bytes[78] == 7U,
                   "reset and environment fields must retain their EV02 offsets")
        || !expect(sink.bytes[84] == 0x04U && sink.bytes[87] == 0x01U
                   && sink.bytes[88] == 12U && sink.bytes[92] == 0x44U
                   && sink.bytes[95] == 0x11U,
                   "storage and fault fields must be little endian")
        || !expect(sink.bytes[124] == 0x88U && sink.bytes[127] == 0x55U,
                   "health transition sequence must terminate the fixed header")
        || !expect(get_u32_le(&sink.bytes[128]) == 0U
                   && get_u32_le(&sink.bytes[132]) == 0U
                   && get_u32_le(&sink.bytes[136]) == 0U
                   && get_u32_le(&sink.bytes[140]) == 0U
                   && get_u64_le(&sink.bytes[144]) == 0U
                   && get_u64_le(&sink.bytes[152]) == 0U,
                   "a gap-free event must zero every EV03 loss field")
        || !expect(sink.bytes[160] == 1U && sink.bytes[161] == 0U
                   && sink.bytes[162] == 0xFEU && sink.bytes[163] == 0xFFU,
                   "the payload must encode signed samples field by field")
        || !expect(sink.bytes[172] == 0x34U && sink.bytes[173] == 0x12U
                   && sink.bytes[174] == 0xF9U && sink.bytes[175] == 0xA5U,
                   "timestamp, temperature and FIFO header must use the fixed layout")
        || !expect(sink.bytes[176] == 0x22U && sink.bytes[177] == 0x11U,
                   "the first post-trigger payload sample must be the trigger sample")
        || !expect(assembler.state == EVENT_ARMED,
                   "a completed export must re-arm capture")
        || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                   "a completed export must release its block immediately"))
    {
        return 1;
    }

    return 0;
}

static sample_block_t *make_loss_block(sample_block_pool_t *pool,
                                       uint32_t sequence,
                                       uint64_t first_monotonic_us)
{
    sample_block_t *block = sample_block_pool_acquire(pool);

    if (block == RT_NULL)
    {
        return RT_NULL;
    }
    block->sequence = sequence;
    block->first_monotonic_us = first_monotonic_us;
    block->sample_period_ns = 625000U;
    block->sample_count = 2U;
    if (sample_block_pool_publish(pool, block) != RT_EOK)
    {
        return RT_NULL;
    }
    return sample_block_pool_take_ready(pool);
}

static int test_exports_exact_loss_summary_for_multiple_gaps(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    byte_sink_t sink = {0};

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    assembler.event.blocks[0] = make_loss_block(&pool, 100U, UINT64_C(1000000));
    assembler.event.blocks[1] = make_loss_block(&pool, 105U, UINT64_C(1003125));
    assembler.event.blocks[2] = make_loss_block(&pool, 110U, UINT64_C(1006250));
    if (!expect(assembler.event.blocks[0] != RT_NULL
                && assembler.event.blocks[1] != RT_NULL
                && assembler.event.blocks[2] != RT_NULL,
                "the loss test must own all three blocks"))
    {
        return 1;
    }
    assembler.state = EVENT_EXPORTING;
    assembler.event.block_count = 3U;
    assembler.event.posttrigger_block_count = 3U;

    if (!expect(event_export_debug_write(&assembler, &pool, write_to_sink, &sink)
                == RT_EOK,
                "a lossy event must export")
        || !expect((get_u16_le(&sink.bytes[50]) & EVENT_FLAG_DATA_LOSS) != 0U,
                   "a sequence gap must set DATA_LOSS")
        || !expect(get_u32_le(&sink.bytes[128]) == 6U,
                   "two three-sample gaps must report six lost samples")
        || !expect(get_u32_le(&sink.bytes[132]) == 102U
                   && get_u32_le(&sink.bytes[136]) == 109U,
                   "loss sequence bounds must include the first and last missing sample")
        || !expect(get_u32_le(&sink.bytes[140]) == 2U,
                   "separate gaps must be counted as separate episodes")
        || !expect(get_u64_le(&sink.bytes[144]) == UINT64_C(1001250)
                   && get_u64_le(&sink.bytes[152]) == UINT64_C(1005625),
                   "loss time bounds must follow the sample timeline"))
    {
        return 1;
    }

    return 0;
}

static int test_failed_write_releases_unwritten_event_blocks(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    sample_block_t *block;

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    block = make_export_block(&pool);
    if (!expect(block != RT_NULL, "the failed-write test must own one block"))
    {
        return 1;
    }

    assembler.state = EVENT_EXPORTING;
    assembler.event.block_count = 1U;
    assembler.event.blocks[0] = block;
    if (!expect(event_export_debug_write(&assembler, &pool, fail_write, RT_NULL)
                == -RT_ERROR,
                "a failed first write must report an export error")
        || !expect(assembler.state == EVENT_ARMED,
                   "a failed export must re-arm capture")
        || !expect(sample_block_pool_free_count(&pool) == SAMPLE_BLOCK_POOL_SIZE,
                   "a failed export must release every unwritten block"))
    {
        return 1;
    }

    return 0;
}

static int test_batches_a_full_sample_block_into_four_payload_writes(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    sample_block_t *block;
    write_count_sink_t sink = {0};

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    block = make_export_block(&pool);
    if (!expect(block != RT_NULL, "the batching test must own one block"))
    {
        return 1;
    }

    block->sample_count = SAMPLE_BLOCK_SAMPLE_CAPACITY;
    assembler.state = EVENT_EXPORTING;
    assembler.event.block_count = 1U;
    assembler.event.blocks[0] = block;
    if (!expect(event_export_debug_write(&assembler, &pool, count_write, &sink) == RT_EOK,
                "a full block must export")
        || !expect(sink.length == EVENT_EXPORT_HEADER_SIZE
                   + SAMPLE_BLOCK_SAMPLE_CAPACITY * EVENT_EXPORT_SAMPLE_SIZE,
                   "the complete EV03 byte count must be preserved")
        || !expect(sink.write_count == 5U,
                   "a full block must use one header and four payload writes"))
    {
        return 1;
    }

    return 0;
}

static int test_feature_off_export_keeps_v1_writer_behavior(void)
{
    sample_block_pool_t pool = {0};
    event_assembler_t assembler = {0};
    sample_block_t *block;
    byte_sink_t sink = {0};

    sample_block_pool_init(&pool);
    event_assembler_init(&assembler);
    block = make_export_block(&pool);
    if (!expect(block != RT_NULL,
                "the feature-off regression must own one block"))
    {
        return 1;
    }
    assembler.state = EVENT_EXPORTING;
    assembler.event.event_id = 1U;
    assembler.event.trigger_sample_index = 1U;
    assembler.event.posttrigger_block_count = 1U;
    assembler.event.block_count = 1U;
    assembler.event.flags = UINT16_C(1U << 15);
    assembler.event.blocks[0] = block;

    if (!expect(event_export_debug_write(&assembler, &pool,
                                         write_to_sink, &sink) == RT_EOK,
                "feature-off export must retain the V1 writer path")
        || !expect(get_u16_le(&sink.bytes[50]) == UINT16_C(1U << 15),
                    "feature-off export must not add a quality gate")
        || !expect(sink.length == EVENT_EXPORT_HEADER_SIZE + 32U,
                   "feature-off export byte count must remain unchanged")
        || !expect(memcmp(sink.bytes, "EV03", 4U) == 0,
                   "feature-off regression must retain EV03 bytes")
        || !expect(sample_block_pool_free_count(&pool)
                       == SAMPLE_BLOCK_POOL_SIZE,
                   "feature-off regression must release its block"))
    {
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_exports_a_little_endian_ev03_golden_vector() != 0
        || test_exports_exact_loss_summary_for_multiple_gaps() != 0
        || test_failed_write_releases_unwritten_event_blocks() != 0
        || test_batches_a_full_sample_block_into_four_payload_writes() != 0
        || test_feature_off_export_keeps_v1_writer_behavior() != 0)
    {
        return 1;
    }

    puts("event export debug: PASS");
    return 0;
}
