#ifndef TRANSPORT_RECORDER_RELIABILITY_EVIDENCE_H
#define TRANSPORT_RECORDER_RELIABILITY_EVIDENCE_H

#include <stdint.h>

#include "ai_result.h"
#include "crash_record.h"
#include "event_log.h"
#include "terp_device.h"

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED

#define RELIABILITY_EVIDENCE_VERSION UINT16_C(1)
#define RELIABILITY_STORAGE_EL01_VERIFIED UINT8_C(1)
#define RELIABILITY_AI_DECISION_NOT_RUN_QUALITY UINT8_C(0)
#define RELIABILITY_AI_DECISION_ELIGIBLE_NO_RESULT UINT8_C(1)
#define RELIABILITY_AI_DECISION_RESULT_PRESENT UINT8_C(2)

typedef int (*reliability_get_event_info_fn)(
    uint32_t event_id, event_log_event_info_t *info, void *context);
typedef int (*reliability_verify_event_fn)(uint32_t event_id, void *context);
typedef int (*reliability_read_event_fn)(
    uint32_t event_id,
    uint32_t offset,
    uint8_t *data,
    uint32_t length,
    uint32_t *read_length,
    void *context);
typedef int (*reliability_get_ai_result_fn)(
    uint32_t event_id, ai_result_t *result, void *context);
typedef int (*reliability_get_ai_result_store_status_fn)(void *context);
typedef int (*reliability_get_crash_record_fn)(
    crash_record_v1_t *record, void *context);
typedef int (*reliability_ack_crash_record_fn)(
    uint32_t sequence, void *context);

typedef struct
{
    reliability_get_event_info_fn get_event_info;
    reliability_verify_event_fn verify_event;
    reliability_read_event_fn read_event;
    reliability_get_ai_result_store_status_fn get_ai_result_store_status;
    reliability_get_ai_result_fn get_ai_result;
    reliability_get_crash_record_fn get_crash_record;
    reliability_ack_crash_record_fn ack_crash_record;
    void *context;
} reliability_evidence_ops_t;

typedef struct
{
    reliability_evidence_ops_t ops;
} reliability_evidence_t;

void reliability_evidence_init(reliability_evidence_t *service,
                               const reliability_evidence_ops_t *ops,
                               void *context);

int reliability_evidence_get_event_evidence(
    uint32_t event_id,
    terp_event_evidence_t *evidence,
    void *context);

int reliability_evidence_check_ai_result_access(uint32_t event_id,
                                                void *context);

int reliability_evidence_get_crash_record(
    uint32_t sequence,
    uint32_t offset,
    uint8_t *data,
    uint32_t requested_length,
    uint32_t data_capacity,
    terp_crash_record_chunk_t *chunk,
    void *context);

int reliability_evidence_ack_crash_record(uint32_t sequence,
                                          void *context);

void reliability_evidence_set_msh_service(reliability_evidence_t *service);
int evidence(int argc, char **argv);

#endif

#endif
