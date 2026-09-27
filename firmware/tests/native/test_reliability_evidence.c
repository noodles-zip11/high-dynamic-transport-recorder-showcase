#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ai_inference_service.h"
#include "crash_record.h"
#include "event_export_format.h"
#include "event_quality.h"
#include "reliability_evidence.h"

typedef enum
{
    TEST_READ_NORMAL = 0,
    TEST_READ_SHORT,
    TEST_READ_EV02,
    TEST_READ_UNKNOWN,
    TEST_READ_DECODE_MISMATCH,
} test_read_mode_t;

typedef struct
{
    int info_result;
    int verify_result;
    int read_result;
    int ai_result;
    int ai_store_status;
    int crash_result;
    int ack_result;
    uint32_t info_calls;
    uint32_t verify_calls;
    uint32_t read_calls;
    uint32_t ai_calls;
    uint32_t ai_store_status_calls;
    uint32_t crash_calls;
    uint32_t ack_calls;
    uint32_t last_ack_sequence;
    uint32_t read_length;
    uint32_t last_read_length;
    test_read_mode_t read_mode;
    event_log_event_info_t info;
    uint8_t header[EVENT_EXPORT_HEADER_SIZE];
    ai_result_t ai;
    crash_record_v1_t crash;
} fake_evidence_t;

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

static void put_u64_le(uint8_t *data, uint64_t value)
{
    uint8_t index;

    for (index = 0U; index < 8U; index++)
    {
        data[index] = (uint8_t)(value >> (index * 8U));
    }
}

static int fake_get_event_info(uint32_t event_id,
                               event_log_event_info_t *info,
                               void *context)
{
    fake_evidence_t *fake = context;

    fake->info_calls++;
    if (fake->info_result != 0)
    {
        return fake->info_result;
    }
    *info = fake->info;
    info->event_id = event_id;
    return 0;
}

static int fake_verify_event(uint32_t event_id, void *context)
{
    fake_evidence_t *fake = context;

    (void)event_id;
    fake->verify_calls++;
    return fake->verify_result;
}

static int fake_read_event(uint32_t event_id,
                           uint32_t offset,
                           uint8_t *data,
                           uint32_t length,
                           uint32_t *read_length,
                           void *context)
{
    fake_evidence_t *fake = context;

    assert(event_id == fake->info.event_id);
    assert(offset == 0U);
    assert(length == 4U || length == EVENT_EXPORT_HEADER_SIZE);
    fake->read_calls++;
    fake->last_read_length = length;
    if (fake->read_result != 0)
    {
        return fake->read_result;
    }
    if (length == 4U)
    {
        memcpy(data, fake->header, 4U);
        if (fake->read_mode == TEST_READ_EV02)
        {
            memcpy(data, "EV02", 4U);
        }
        else if (fake->read_mode == TEST_READ_UNKNOWN)
        {
            memcpy(data, "NOPE", 4U);
        }
        *read_length = 4U;
        return 0;
    }
    memcpy(data, fake->header, EVENT_EXPORT_HEADER_SIZE);
    if (fake->read_mode == TEST_READ_EV02)
    {
        memcpy(data, "EV02", 4U);
    }
    else if (fake->read_mode == TEST_READ_UNKNOWN)
    {
        memcpy(data, "NOPE", 4U);
    }
    else if (fake->read_mode == TEST_READ_DECODE_MISMATCH)
    {
        put_u32_le(&data[8], fake->info.event_id + 1U);
    }
    *read_length = fake->read_mode == TEST_READ_SHORT
                       ? EVENT_EXPORT_HEADER_SIZE - 1U
                       : fake->read_length;
    return 0;
}

static int fake_get_ai_result(uint32_t event_id,
                              ai_result_t *result,
                              void *context)
{
    fake_evidence_t *fake = context;

    fake->ai_calls++;
    if (fake->ai_result != 0)
    {
        return fake->ai_result;
    }
    *result = fake->ai;
    result->event_id = event_id;
    return 0;
}

static int fake_get_ai_store_status(void *context)
{
    fake_evidence_t *fake = context;

    fake->ai_store_status_calls++;
    return fake->ai_store_status;
}

static int fake_get_crash_record(crash_record_v1_t *record, void *context)
{
    fake_evidence_t *fake = context;

    fake->crash_calls++;
    if (fake->crash_result != 0)
    {
        return fake->crash_result;
    }
    *record = fake->crash;
    return 0;
}

static int fake_ack_crash_record(uint32_t sequence, void *context)
{
    fake_evidence_t *fake = context;

    fake->ack_calls++;
    fake->last_ack_sequence = sequence;
    return fake->ack_result;
}

static void build_event_header(fake_evidence_t *fake,
                               uint32_t event_id,
                               uint32_t pretrigger_samples,
                               uint32_t posttrigger_samples,
                               uint16_t flags)
{
    uint32_t payload_samples = pretrigger_samples + posttrigger_samples;

    memset(fake->header, 0, sizeof(fake->header));
    memcpy(fake->header, "EV03", 4U);
    put_u16_le(&fake->header[4], 3U);
    put_u16_le(&fake->header[6], EVENT_EXPORT_HEADER_SIZE);
    put_u32_le(&fake->header[8], event_id);
    put_u32_le(&fake->header[36], 800U);
    put_u32_le(&fake->header[40], pretrigger_samples);
    put_u32_le(&fake->header[44], posttrigger_samples);
    put_u16_le(&fake->header[50], flags);
    put_u32_le(&fake->header[60], payload_samples * EVENT_EXPORT_SAMPLE_SIZE);
    fake->info.event_id = event_id;
    fake->info.ev01_length = EVENT_EXPORT_HEADER_SIZE
                             + payload_samples * EVENT_EXPORT_SAMPLE_SIZE;
    fake->read_length = EVENT_EXPORT_HEADER_SIZE;
    fake->read_mode = TEST_READ_NORMAL;
}

static void init_facade(reliability_evidence_t *service,
                        fake_evidence_t *fake,
                        rt_bool_t include_ai)
{
    const reliability_evidence_ops_t ops = {
        .get_event_info = fake_get_event_info,
        .verify_event = fake_verify_event,
        .read_event = fake_read_event,
        .get_ai_result = include_ai ? fake_get_ai_result : NULL,
        .get_ai_result_store_status = include_ai
                                     ? fake_get_ai_store_status : NULL,
        .get_crash_record = fake_get_crash_record,
        .ack_crash_record = fake_ack_crash_record,
    };

    reliability_evidence_init(service, &ops, fake);
}

static void test_event_evidence_quality_and_ai_decisions(void)
{
    fake_evidence_t fake = {0};
    reliability_evidence_t service;
    terp_event_evidence_t evidence;

    build_event_header(&fake, 42U, 800U, 1600U, 0U);
    fake.ai_store_status = -1;
    init_facade(&service, &fake, RT_TRUE);
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);
    assert(fake.ai_store_status_calls == 1U);
    assert(fake.ai_calls == 0U);

    fake.ai_store_status = 0;
    fake.ai_result = AI_INFERENCE_RESULT_NOT_FOUND;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == 0);
    assert(evidence.event_id == 42U);
    assert(evidence.evidence_version == 1U);
    assert(evidence.verdict == EVENT_QUALITY_PASS);
    assert(evidence.ai_decision == RELIABILITY_AI_DECISION_ELIGIBLE_NO_RESULT);
    assert(evidence.storage_state == RELIABILITY_STORAGE_EL01_VERIFIED);
    assert(evidence.ai_result_status == 0U);
    assert(evidence.ai_result_sequence == 0U);
    assert(fake.ai_calls == 1U);

    fake.ai.status = AI_RESULT_STATUS_PREDICTION;
    fake.ai.failure_reason = AI_RESULT_FAILURE_NONE;
    fake.ai.result_sequence = 9U;
    fake.ai_result = 0;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == 0);
    assert(evidence.ai_decision == RELIABILITY_AI_DECISION_RESULT_PRESENT);
    assert(evidence.ai_result_status == AI_RESULT_STATUS_PREDICTION);
    assert(evidence.ai_failure_reason == AI_RESULT_FAILURE_NONE);
    assert(evidence.ai_result_sequence == 9U);

    build_event_header(&fake, 42U, 768U, 1600U,
                       EVENT_FLAG_PRETRIGGER_SHORT);
    fake.ai_calls = 0U;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == 0);
    assert(evidence.verdict == EVENT_QUALITY_DEGRADED);
    assert(evidence.ai_decision == RELIABILITY_AI_DECISION_NOT_RUN_QUALITY);
    assert(fake.ai_calls == 0U);

    build_event_header(&fake, 42U, 800U, 1600U, EVENT_FLAG_DATA_LOSS);
    put_u32_le(&fake.header[128], 1U);
    put_u32_le(&fake.header[132], 900U);
    put_u32_le(&fake.header[136], 900U);
    put_u32_le(&fake.header[140], 1U);
    put_u64_le(&fake.header[144], 1000U);
    put_u64_le(&fake.header[152], 1000U);
    fake.info.ev01_length = EVENT_EXPORT_HEADER_SIZE
                             + 2400U * EVENT_EXPORT_SAMPLE_SIZE;
    fake.ai_calls = 0U;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == 0);
    assert(evidence.verdict == EVENT_QUALITY_INVALID);
    assert(evidence.ai_decision == RELIABILITY_AI_DECISION_NOT_RUN_QUALITY);
    assert((evidence.reason_flags & EVENT_QUALITY_REASON_DATA_LOSS) != 0U);
    assert(fake.ai_calls == 0U);
}

static void test_event_evidence_storage_and_header_errors(void)
{
    fake_evidence_t fake = {0};
    reliability_evidence_t service;
    terp_event_evidence_t evidence;

    build_event_header(&fake, 42U, 800U, 1600U, 0U);
    init_facade(&service, &fake, RT_FALSE);

    fake.info_result = -TERP_ERROR_NOT_FOUND;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_NOT_FOUND);

    fake.info_result = 0;
    fake.verify_result = -1;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);

    fake.verify_result = 0;
    fake.read_result = -1;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);

    fake.read_result = 0;
    fake.read_mode = TEST_READ_SHORT;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);

    fake.read_mode = TEST_READ_EV02;
    fake.info.ev01_length = 144U;
    fake.verify_calls = 0U;
    fake.read_calls = 0U;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_UNSUPPORTED);
    assert(fake.verify_calls == 1U);
    assert(fake.read_calls == 1U);
    assert(fake.last_read_length == 4U);

    fake.read_mode = TEST_READ_UNKNOWN;
    fake.verify_calls = 0U;
    fake.read_calls = 0U;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_UNSUPPORTED);
    assert(fake.verify_calls == 1U);
    assert(fake.read_calls == 1U);
    assert(fake.last_read_length == 4U);

    fake.read_mode = TEST_READ_NORMAL;
    fake.info.ev01_length = EVENT_EXPORT_HEADER_SIZE
                            + 2400U * EVENT_EXPORT_SAMPLE_SIZE;
    fake.read_length = EVENT_EXPORT_HEADER_SIZE;
    fake.verify_calls = 0U;
    fake.read_calls = 0U;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == RT_EOK);
    assert(fake.verify_calls == 1U);
    assert(fake.read_calls == 2U);
    assert(fake.last_read_length == EVENT_EXPORT_HEADER_SIZE);

    fake.read_mode = TEST_READ_DECODE_MISMATCH;
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);

    fake.read_mode = TEST_READ_NORMAL;
    fake.ai_result = AI_INFERENCE_RESULT_STORAGE_ERROR;
    init_facade(&service, &fake, RT_TRUE);
    assert(reliability_evidence_get_event_evidence(
               42U, &evidence, &service) == -TERP_ERROR_INTERNAL);
}

static void test_ai_result_access_follows_persisted_event_quality(void)
{
    fake_evidence_t fake = {0};
    reliability_evidence_t service;

    build_event_header(&fake, 42U, 800U, 1600U, 0U);
    init_facade(&service, &fake, RT_FALSE);
    assert(reliability_evidence_check_ai_result_access(42U, &service)
           == RT_EOK);

    build_event_header(&fake, 42U, 768U, 1600U,
                       EVENT_FLAG_PRETRIGGER_SHORT);
    assert(reliability_evidence_check_ai_result_access(42U, &service)
           == -TERP_ERROR_NOT_FOUND);

    build_event_header(&fake, 42U, 800U, 1600U, EVENT_FLAG_DATA_LOSS);
    put_u32_le(&fake.header[128], 1U);
    put_u32_le(&fake.header[132], 900U);
    put_u32_le(&fake.header[136], 900U);
    put_u32_le(&fake.header[140], 1U);
    put_u64_le(&fake.header[144], 1000U);
    put_u64_le(&fake.header[152], 1000U);
    assert(reliability_evidence_check_ai_result_access(42U, &service)
           == -TERP_ERROR_NOT_FOUND);

    fake.read_mode = TEST_READ_EV02;
    assert(reliability_evidence_check_ai_result_access(42U, &service)
           == -TERP_ERROR_UNSUPPORTED);
}

static void make_valid_crash_record(fake_evidence_t *fake)
{
    uint8_t encoded[CRASH_RECORD_V1_RECORD_SIZE_BYTES];

    memset(&fake->crash, 0, sizeof(fake->crash));
    fake->crash.magic = CRASH_RECORD_V1_MAGIC;
    fake->crash.format_version = CRASH_RECORD_V1_FORMAT_VERSION;
    fake->crash.header_length = CRASH_RECORD_V1_HEADER_LENGTH_BYTES;
    fake->crash.record_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    fake->crash.sequence = 17U;
    fake->crash.fault_kind = CRASH_RECORD_FAULT_HARDFAULT;
    fake->crash.commit_marker = CRASH_RECORD_V1_COMMIT_VALID;
    fake->crash.r0 = UINT32_C(0x11223344);
    fake->crash.build_id[0] = 'v';
    fake->crash.build_id[1] = '1';
    crash_record_v1_encode(&fake->crash, encoded);
    fake->crash.crc32 = crash_record_v1_crc32(encoded, 120U);
    crash_record_v1_encode(&fake->crash, encoded);
    assert(crash_record_v1_validate(encoded, &fake->crash) != 0);
}

static void test_crash_chunk_and_ack(void)
{
    fake_evidence_t fake = {0};
    reliability_evidence_t service;
    terp_crash_record_chunk_t chunk;
    uint8_t data[128];

    make_valid_crash_record(&fake);
    init_facade(&service, &fake, RT_FALSE);
    assert(reliability_evidence_get_crash_record(
               0U, 0U, data, sizeof(data), sizeof(data), &chunk,
               &service) == 0);
    assert(chunk.sequence == 17U);
    assert(chunk.total_length == CRASH_RECORD_V1_RECORD_SIZE_BYTES);
    assert(chunk.actual_length == CRASH_RECORD_V1_RECORD_SIZE_BYTES);
    assert(data[0] == (uint8_t)CRASH_RECORD_V1_MAGIC);
    assert(data[12] == 17U);

    assert(reliability_evidence_get_crash_record(
               17U, 127U, data, sizeof(data), sizeof(data), &chunk,
               &service) == 0);
    assert(chunk.actual_length == 1U);

    assert(reliability_evidence_get_crash_record(
               18U, 0U, data, sizeof(data), sizeof(data), &chunk,
               &service) == -TERP_ERROR_NOT_FOUND);
    assert(reliability_evidence_get_crash_record(
               17U, 128U, data, sizeof(data), sizeof(data), &chunk,
               &service) == -TERP_ERROR_MALFORMED);

    fake.ack_result = 0;
    assert(reliability_evidence_ack_crash_record(17U, &service) == 0);
    assert(fake.ack_calls == 1U && fake.last_ack_sequence == 17U);
    fake.ack_result = -TERP_ERROR_NOT_FOUND;
    assert(reliability_evidence_ack_crash_record(16U, &service)
           == -TERP_ERROR_NOT_FOUND);
}

static void test_msh_is_read_only_except_explicit_ack(void)
{
    fake_evidence_t fake = {0};
    reliability_evidence_t service;
    char event_id[] = "42";
    char sequence[] = "17";
    char offset[] = "0";
    char length[] = "128";
    char *event_args[] = {"evidence", "event", event_id};
    char *crash_args[] = {"evidence", "crash", sequence, offset, length};
    char *ack_args[] = {"evidence", "crash_ack", sequence};

    build_event_header(&fake, 42U, 800U, 1600U, 0U);
    make_valid_crash_record(&fake);
    init_facade(&service, &fake, RT_FALSE);
    reliability_evidence_set_msh_service(&service);
    assert(evidence(3, event_args) == RT_EOK);
    assert(evidence(5, crash_args) == RT_EOK);
    assert(fake.ack_calls == 0U);
    assert(evidence(3, ack_args) == RT_EOK);
    assert(fake.ack_calls == 1U);
    assert(evidence(2, event_args) != RT_EOK);
}

int main(void)
{
    test_event_evidence_quality_and_ai_decisions();
    test_event_evidence_storage_and_header_errors();
    test_ai_result_access_follows_persisted_event_quality();
    test_crash_chunk_and_ack();
    test_msh_is_read_only_except_explicit_ack();
    puts("reliability evidence: PASS");
    return 0;
}
