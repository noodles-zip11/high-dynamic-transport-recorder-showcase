#ifndef TRANSPORT_RECORDER_AI_INFERENCE_SERVICE_H
#define TRANSPORT_RECORDER_AI_INFERENCE_SERVICE_H

#include <stdint.h>

#include "ai_runtime.h"
#include "ai_result_sidecar.h"
#include "sample_block_pool.h"

#define AI_INFERENCE_QUEUE_CAPACITY 2U
/* Failure persistence is bounded and admission-controlled; a full queue
 * rejects a new failure before publishing an unpersisted RAM-only result.
 * The worker keeps the head entry in-flight until QSPI readback succeeds and
 * retries storage errors with backoff. */
#define AI_RESULT_PERSIST_QUEUE_CAPACITY 8U
/* One worker result is in flight and up to AI_INFERENCE_QUEUE_CAPACITY
 * completed events can be waiting while failure results are admitted. */
#define AI_RESULT_PERSIST_NORMAL_RESULT_RESERVE \
    (AI_INFERENCE_QUEUE_CAPACITY + 1U)
#define AI_RESULT_PERSIST_FAILURE_CAPACITY \
    (AI_RESULT_PERSIST_QUEUE_CAPACITY - AI_RESULT_PERSIST_NORMAL_RESULT_RESERVE)

#define AI_INFERENCE_RESULT_NOT_FOUND (-2)
#define AI_INFERENCE_RESULT_STORAGE_ERROR (-3)

typedef struct
{
    uint32_t submitted_count;
    uint32_t processed_count;
    uint32_t queue_drop_count;
    uint32_t feature_error_count;
    uint32_t runtime_error_count;
    uint32_t result_queue_drop_count;
    uint32_t result_queue_backpressure_count;
    uint32_t result_queue_depth;
    uint32_t result_queue_high_watermark;
    uint32_t last_event_id;
    uint8_t last_class_index;
    float last_confidence;
    uint32_t result_store_error_count;
    rt_bool_t model_ready;
} ai_inference_stats_t;

rt_err_t ai_inference_service_start(const ai_model_t *model);

/* Quiesces inference while a caller commits the model's persistent state. */
rt_err_t ai_inference_service_prepare_model(const ai_model_t *model);
/* Publishes the prepared model and releases the inference gate. */
rt_err_t ai_inference_service_publish_model(const ai_model_t *model);
/* Aborts a prepared transition and releases the inference gate. */
void ai_inference_service_abort_model(void);
/* Quarantines the runtime after an ambiguous persistent activation. */
void ai_inference_service_quarantine_model(void);
/* Convenience wrapper for callers that do not have a persistent commit. */
rt_err_t ai_inference_service_set_model(const ai_model_t *model);

/* Attaches a mounted, power-loss-tolerant result store before event capture. */
rt_err_t ai_inference_service_set_result_sidecar(ai_result_sidecar_t *sidecar);
rt_err_t ai_inference_service_get_result_store_status(void);

/* Normal submissions retain completed event blocks and enqueue only bounded
 * metadata; admission is rejected before retain when a result-persistence
 * credit is unavailable. Under TRANSPORT_FAULT_INJECTION_ENABLED, an active
 * queue-pressure snapshot may instead precompute features outside ai_mutex
 * and enqueue without retaining pool/block references; the caller keeps the
 * event blocks valid until this function returns. */
rt_err_t ai_inference_service_submit_event(sample_block_pool_t *pool,
                                           const event_record_t *event);

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
rt_err_t ai_inference_service_arm_queue_pressure(void);
void ai_inference_service_cancel_queue_pressure(void);
rt_bool_t ai_inference_service_queue_pressure_active(void);
rt_err_t ai_inference_service_test_worker_step(void);
#endif

void ai_inference_service_get_stats(ai_inference_stats_t *stats);

/* event_id == 0 returns the newest available result. */
rt_err_t ai_inference_service_get_result(uint32_t event_id,
                                         ai_result_t *result);

#endif
