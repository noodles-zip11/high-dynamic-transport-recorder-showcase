#include "reliability_evidence.h"

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ai_inference_service.h"
#include "event_export_debug.h"
#include "event_quality.h"

static reliability_evidence_t *reliability_evidence_msh_service;

static int map_storage_result(int result)
{
    if (result == -TERP_ERROR_NOT_FOUND)
    {
        return -TERP_ERROR_NOT_FOUND;
    }
    return -TERP_ERROR_INTERNAL;
}

static int map_ai_result(int result)
{
    if (result == AI_INFERENCE_RESULT_NOT_FOUND)
    {
        return -TERP_ERROR_NOT_FOUND;
    }
    return -TERP_ERROR_INTERNAL;
}

static int reliability_evidence_read_header(
    reliability_evidence_t *service,
    uint32_t event_id,
    uint8_t *header)
{
    uint8_t magic[4];
    uint32_t read_length = 0U;

    if (service->ops.read_event == NULL)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    if (service->ops.read_event(event_id, 0U, magic, sizeof(magic),
                                &read_length, service->ops.context) != 0
        || read_length != sizeof(magic))
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (memcmp(magic, "EV03", sizeof(magic)) != 0)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    read_length = 0U;
    if (service->ops.read_event(event_id, 0U, header,
                                EVENT_EXPORT_HEADER_SIZE, &read_length,
                                service->ops.context) != 0
        || read_length != EVENT_EXPORT_HEADER_SIZE)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return RT_EOK;
}

void reliability_evidence_init(reliability_evidence_t *service,
                               const reliability_evidence_ops_t *ops,
                               void *context)
{
    if (service == NULL)
    {
        return;
    }
    memset(service, 0, sizeof(*service));
    if (ops != NULL)
    {
        service->ops = *ops;
        service->ops.context = context;
    }
}

static int reliability_evidence_read_quality(
    reliability_evidence_t *service,
    uint32_t event_id,
    event_quality_result_t *quality)
{
    event_log_event_info_t info;
    event_quality_facts_t facts;
    uint8_t header[EVENT_EXPORT_HEADER_SIZE];
    int result;

    if (service == NULL || quality == NULL || event_id == 0U)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (service->ops.get_event_info == NULL)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    result = service->ops.get_event_info(event_id, &info,
                                         service->ops.context);
    if (result != 0)
    {
        return map_storage_result(result);
    }
    if (info.event_id != event_id)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (service->ops.verify_event == NULL)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    if (service->ops.verify_event(event_id, service->ops.context) != 0)
    {
        return -TERP_ERROR_INTERNAL;
    }
    result = reliability_evidence_read_header(service, event_id, header);
    if (result != RT_EOK)
    {
        return result;
    }
    if (event_export_debug_decode_header(header, EVENT_EXPORT_HEADER_SIZE,
                                         info.ev01_length, &facts) != RT_EOK
        || facts.event_id != event_id
        || event_quality_assess_facts(&facts, quality) != RT_EOK)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return RT_EOK;
}

int reliability_evidence_get_event_evidence(
    uint32_t event_id,
    terp_event_evidence_t *evidence,
    void *context)
{
    reliability_evidence_t *service = context;
    event_quality_result_t quality;
    ai_result_t ai_result;
    int result;

    if (service == NULL || evidence == NULL || event_id == 0U)
    {
        return -TERP_ERROR_INTERNAL;
    }
    memset(evidence, 0, sizeof(*evidence));
    result = reliability_evidence_read_quality(service, event_id, &quality);
    if (result != RT_EOK)
    {
        return result;
    }

    evidence->event_id = event_id;
    evidence->evidence_version = RELIABILITY_EVIDENCE_VERSION;
    evidence->verdict = (uint8_t)quality.verdict;
    evidence->ai_decision = RELIABILITY_AI_DECISION_NOT_RUN_QUALITY;
    evidence->reason_flags = quality.reason_flags;
    evidence->storage_state = RELIABILITY_STORAGE_EL01_VERIFIED;
    if (quality.verdict != EVENT_QUALITY_PASS
        || service->ops.get_ai_result == NULL)
    {
        if (quality.verdict == EVENT_QUALITY_PASS)
        {
            evidence->ai_decision =
                RELIABILITY_AI_DECISION_ELIGIBLE_NO_RESULT;
        }
        return RT_EOK;
    }

    if (service->ops.get_ai_result_store_status == NULL
        || service->ops.get_ai_result_store_status(
               service->ops.context) != 0)
    {
        return -TERP_ERROR_INTERNAL;
    }

    result = service->ops.get_ai_result(event_id, &ai_result,
                                        service->ops.context);
    if (result != 0)
    {
        if (map_ai_result(result) == -TERP_ERROR_NOT_FOUND)
        {
            evidence->ai_decision =
                RELIABILITY_AI_DECISION_ELIGIBLE_NO_RESULT;
            return RT_EOK;
        }
        return -TERP_ERROR_INTERNAL;
    }
    if (ai_result.event_id != event_id)
    {
        return -TERP_ERROR_INTERNAL;
    }
    evidence->ai_decision = RELIABILITY_AI_DECISION_RESULT_PRESENT;
    evidence->ai_result_status = ai_result.status;
    evidence->ai_failure_reason = ai_result.failure_reason;
    evidence->ai_result_sequence = ai_result.result_sequence;
    return RT_EOK;
}

int reliability_evidence_check_ai_result_access(uint32_t event_id,
                                                void *context)
{
    reliability_evidence_t *service = context;
    event_quality_result_t quality;
    int result;

    result = reliability_evidence_read_quality(service, event_id, &quality);
    if (result != RT_EOK)
    {
        return result;
    }
    return quality.verdict == EVENT_QUALITY_PASS
               ? RT_EOK : -TERP_ERROR_NOT_FOUND;
}

int reliability_evidence_get_crash_record(
    uint32_t sequence,
    uint32_t offset,
    uint8_t *data,
    uint32_t requested_length,
    uint32_t data_capacity,
    terp_crash_record_chunk_t *chunk,
    void *context)
{
    reliability_evidence_t *service = context;
    crash_record_v1_t record;
    uint32_t actual_length;
    int result;

    if (service == NULL || data == NULL || chunk == NULL
        || requested_length == 0U)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (offset >= CRASH_RECORD_V1_RECORD_SIZE_BYTES)
    {
        return -TERP_ERROR_MALFORMED;
    }
    if (service->ops.get_crash_record == NULL)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    result = service->ops.get_crash_record(&record, service->ops.context);
    if (result != 0)
    {
        return result == -TERP_ERROR_NOT_FOUND
                   ? -TERP_ERROR_NOT_FOUND
                   : -TERP_ERROR_INTERNAL;
    }
    if (!crash_record_v1_validate((const volatile uint8_t *)&record, NULL))
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (record.sequence == 0U
        || (sequence != 0U && record.sequence != sequence))
    {
        return -TERP_ERROR_NOT_FOUND;
    }
    actual_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES - offset;
    if (actual_length > requested_length)
    {
        actual_length = requested_length;
    }
    if (actual_length == 0U || actual_length > data_capacity)
    {
        return -TERP_ERROR_INTERNAL;
    }
    memcpy(data, ((const uint8_t *)&record) + offset, actual_length);
    chunk->sequence = record.sequence;
    chunk->total_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    chunk->actual_length = actual_length;
    return RT_EOK;
}

int reliability_evidence_ack_crash_record(uint32_t sequence,
                                          void *context)
{
    reliability_evidence_t *service = context;
    int result;

    if (service == NULL || sequence == 0U)
    {
        return -TERP_ERROR_MALFORMED;
    }
    if (service->ops.ack_crash_record == NULL)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    result = service->ops.ack_crash_record(sequence, service->ops.context);
    if (result == 0)
    {
        return RT_EOK;
    }
    return result == -TERP_ERROR_NOT_FOUND
               ? -TERP_ERROR_NOT_FOUND
               : -TERP_ERROR_INTERNAL;
}

void reliability_evidence_set_msh_service(reliability_evidence_t *service)
{
    reliability_evidence_msh_service = service;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || value == NULL || *text == '\0')
    {
        return -RT_ERROR;
    }
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0'
        || parsed > UINT32_MAX)
    {
        return -RT_ERROR;
    }
    *value = (uint32_t)parsed;
    return RT_EOK;
}

static int evidence_msh_event(int argc, char **argv)
{
    terp_event_evidence_t evidence_result;
    uint32_t event_id;

    if (argc != 3 || parse_u32(argv[2], &event_id) != RT_EOK
        || reliability_evidence_msh_service == NULL
        || reliability_evidence_get_event_evidence(
               event_id, &evidence_result,
               reliability_evidence_msh_service) != RT_EOK)
    {
        return -RT_ERROR;
    }
    rt_kprintf("event=%lu verdict=%u reason=0x%08lx ai=%u\n",
               (unsigned long)evidence_result.event_id,
               (unsigned)evidence_result.verdict,
               (unsigned long)evidence_result.reason_flags,
               (unsigned)evidence_result.ai_decision);
    return RT_EOK;
}

static int evidence_msh_crash(int argc, char **argv)
{
    uint8_t data[CRASH_RECORD_V1_RECORD_SIZE_BYTES];
    terp_crash_record_chunk_t chunk;
    uint32_t sequence = 0U;
    uint32_t offset = 0U;
    uint32_t length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    uint32_t index;

    if (argc < 2 || argc > 5 || reliability_evidence_msh_service == NULL
        || (argc > 2 && parse_u32(argv[2], &sequence) != RT_EOK)
        || (argc > 3 && parse_u32(argv[3], &offset) != RT_EOK)
        || (argc > 4 && parse_u32(argv[4], &length) != RT_EOK)
        || reliability_evidence_get_crash_record(
               sequence, offset, data, length, sizeof(data), &chunk,
               reliability_evidence_msh_service) != RT_EOK)
    {
        return -RT_ERROR;
    }
    rt_kprintf("crash sequence=%lu offset=%lu length=%lu total=%lu data=",
               (unsigned long)chunk.sequence,
               (unsigned long)offset,
               (unsigned long)chunk.actual_length,
               (unsigned long)chunk.total_length);
    for (index = 0U; index < chunk.actual_length; index++)
    {
        rt_kprintf("%02x", (unsigned)data[index]);
    }
    rt_kprintf("\n");
    return RT_EOK;
}

static int evidence_msh_crash_ack(int argc, char **argv)
{
    uint32_t sequence;

    if (argc != 3 || parse_u32(argv[2], &sequence) != RT_EOK
        || reliability_evidence_msh_service == NULL
        || reliability_evidence_ack_crash_record(
               sequence, reliability_evidence_msh_service) != RT_EOK)
    {
        return -RT_ERROR;
    }
    rt_kprintf("crash_ack sequence=%lu\n", (unsigned long)sequence);
    return RT_EOK;
}

int evidence(int argc, char **argv)
{
    if (argc < 2 || argv == NULL)
    {
        return -RT_ERROR;
    }
    if (strcmp(argv[1], "event") == 0)
    {
        return evidence_msh_event(argc, argv);
    }
    if (strcmp(argv[1], "crash") == 0)
    {
        return evidence_msh_crash(argc, argv);
    }
    if (strcmp(argv[1], "crash_ack") == 0)
    {
        return evidence_msh_crash_ack(argc, argv);
    }
    rt_kprintf("usage: evidence event <event_id> | evidence crash [sequence] "
               "[offset] [length] | evidence crash_ack <sequence>\n");
    return -RT_ERROR;
}

MSH_CMD_EXPORT(evidence, reliability event and crash evidence readback);

#endif
