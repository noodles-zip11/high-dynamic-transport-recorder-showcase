#include "terp_service.h"

#include <string.h>

#include "memory_layout.h"
#include "ota_qspi_candidate.h"
#include "ota_state_app_store.h"
#include "storage_service.h"
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "crash_record_target.h"
#include "reliability_evidence.h"
#endif
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
#include "ai_inference_service.h"
#endif

static int terp_service_get_health(terp_health_info_t *health, void *context)
{
    terp_service_t *service = context;
    health_snapshot_t snapshot;
    event_log_status_t storage_status;

    if (service == 0 || service->health_service == 0 || health == 0)
    {
        return -1;
    }
    health_service_get_snapshot(service->health_service, &snapshot);
    health->state = (uint8_t)snapshot.state;
    health->storage_ready = storage_service_get_status(&storage_status) == RT_EOK
                            && storage_status.state == EVENT_LOG_READY ? 1U : 0U;
    health->free_log_bytes = snapshot.free_log_bytes;
    health->storage_error_count = snapshot.storage_error_count;
    health->event_export_error_count = snapshot.event_export_error_count;
    return 0;
}

static int terp_service_get_time(int64_t *utc_unix_seconds,
                                 uint32_t *epoch_id,
                                 void *context)
{
    terp_service_t *service = context;
    time_service_status_t status;

    if (service == 0 || service->time_service == 0 || utc_unix_seconds == 0
        || epoch_id == 0 || time_service_get_status(service->time_service, &status) != 0
        || !status.utc_valid)
    {
        return -1;
    }
    *utc_unix_seconds = status.utc_unix_seconds;
    *epoch_id = status.epoch_id;
    return 0;
}

static int terp_service_set_time(int64_t utc_unix_seconds,
                                 uint32_t *epoch_id,
                                 void *context)
{
    terp_service_t *service = context;
    time_service_status_t status;

    if (service == 0 || service->time_service == 0 || epoch_id == 0
        || time_service_set_utc(service->time_service, utc_unix_seconds) != 0
        || time_service_get_status(service->time_service, &status) != 0)
    {
        return -1;
    }
    *epoch_id = status.epoch_id;
    return 0;
}

static int terp_service_get_event_info(uint32_t event_id,
                                       terp_event_info_t *info,
                                       void *context)
{
    event_log_event_info_t storage_info;

    (void)context;
    if (info == 0
        || storage_service_get_event_info(event_id, &storage_info) != RT_EOK)
    {
        return -1;
    }
    info->event_id = storage_info.event_id;
    info->total_length = storage_info.ev01_length;
    info->event_crc32 = storage_info.ev01_crc32;
    return 0;
}

static int terp_service_list_events(uint32_t after_event_id,
                                    uint16_t maximum_count,
                                    terp_event_info_t *events,
                                    uint16_t events_capacity,
                                    uint16_t *event_count,
                                    uint32_t *next_event_id,
                                    void *context)
{
    terp_service_t *service = context;
    uint16_t count;
    uint16_t index;

    if (service == 0 || events == 0 || event_count == 0 || next_event_id == 0
        || maximum_count > TERP_LIST_MAXIMUM_COUNT
        || events_capacity > TERP_LIST_MAXIMUM_COUNT
        || storage_service_list_events(after_event_id, maximum_count,
                                       service->list_storage_events,
                                       events_capacity, &count,
                                       next_event_id) != RT_EOK
        || count > maximum_count || count > events_capacity)
    {
        return -1;
    }
    for (index = 0U; index < count; index++)
    {
        events[index].event_id = service->list_storage_events[index].event_id;
        events[index].total_length =
            service->list_storage_events[index].ev01_length;
        events[index].event_crc32 =
            service->list_storage_events[index].ev01_crc32;
    }
    *event_count = count;
    return 0;
}

static int terp_service_read_event(uint32_t event_id,
                                   uint32_t offset,
                                   uint8_t *data,
                                   uint32_t requested_length,
                                   uint32_t *actual_length,
                                   void *context)
{
    (void)context;
    return storage_service_read_event(event_id, offset, data, requested_length,
                                      actual_length) == RT_EOK ? 0 : -1;
}

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
static int terp_service_get_ai_result(uint32_t event_id,
                                      terp_ai_result_t *result,
                                      void *context)
{
    ai_result_t ai_result;
    uint32_t index;
    rt_err_t service_result;

    if (result == RT_NULL)
    {
        return -TERP_ERROR_NOT_FOUND;
    }
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    {
        terp_service_t *service = context;
        int access_result;

        if (service != RT_NULL
            && service->device_ops.get_event_evidence != RT_NULL)
        {
            access_result = reliability_evidence_check_ai_result_access(
                event_id, &service->reliability_evidence);
            if (access_result == -TERP_ERROR_NOT_FOUND)
            {
                return -TERP_ERROR_NOT_FOUND;
            }
            if (access_result != RT_EOK
                && access_result != -TERP_ERROR_UNSUPPORTED)
            {
                return -TERP_ERROR_INTERNAL;
            }
        }
    }
#else
    (void)context;
#endif
    service_result = ai_inference_service_get_result(event_id, &ai_result);
    if (service_result != RT_EOK)
    {
        return service_result == AI_INFERENCE_RESULT_NOT_FOUND
                   ? -TERP_ERROR_NOT_FOUND
                   : -TERP_ERROR_INTERNAL;
    }
    result->event_id = ai_result.event_id;
    result->model_version = ai_result.model_version;
    result->status = ai_result.status;
    result->class_index = ai_result.class_index;
    result->class_count = ai_result.class_count;
    result->quality_flags = ai_result.quality_flags;
    result->event_flags = ai_result.event_flags;
    result->sample_count = ai_result.sample_count;
    result->model_crc32 = ai_result.model_crc32;
    result->confidence = ai_result.confidence;
    for (index = 0U; index < 4U; index++)
    {
        result->logits[index] = ai_result.logits[index];
    }
    result->failure_reason = ai_result.failure_reason;
    result->result_sequence = ai_result.result_sequence;
    return 0;
}
#endif

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static int terp_service_reliability_get_event_info(
    uint32_t event_id, event_log_event_info_t *info, void *context)
{
    event_log_status_t status;

    (void)context;
    if (info == NULL || !storage_service_is_ready())
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (storage_service_get_event_info(event_id, info) == RT_EOK)
    {
        return 0;
    }
    if (storage_service_get_status(&status) != RT_EOK)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return event_id >= status.next_event_id
               ? -TERP_ERROR_NOT_FOUND
               : -TERP_ERROR_INTERNAL;
}

static int terp_service_reliability_verify_event(uint32_t event_id,
                                                 void *context)
{
    (void)context;
    if (!storage_service_is_ready())
    {
        return -TERP_ERROR_INTERNAL;
    }
    return storage_service_verify_event(event_id) == RT_EOK
               ? 0
               : -TERP_ERROR_INTERNAL;
}

static int terp_service_reliability_read_event(
    uint32_t event_id,
    uint32_t offset,
    uint8_t *data,
    uint32_t length,
    uint32_t *read_length,
    void *context)
{
    (void)context;
    if (!storage_service_is_ready())
    {
        return -TERP_ERROR_INTERNAL;
    }
    return storage_service_read_event(event_id, offset, data, length,
                                      read_length) == RT_EOK
               ? 0
               : -TERP_ERROR_INTERNAL;
}

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
static int terp_service_reliability_get_ai_result_store_status(void *context)
{
    (void)context;
    return ai_inference_service_get_result_store_status() == RT_EOK ? 0 : -1;
}

static int terp_service_reliability_get_ai_result(
    uint32_t event_id, ai_result_t *result, void *context)
{
    ai_result_t stored;
    rt_err_t service_result;

    (void)context;
    if (result == NULL)
    {
        return AI_INFERENCE_RESULT_STORAGE_ERROR;
    }
    service_result = ai_inference_service_get_result(event_id, &stored);
    if (service_result == AI_INFERENCE_RESULT_NOT_FOUND)
    {
        return AI_INFERENCE_RESULT_NOT_FOUND;
    }
    if (service_result != RT_EOK)
    {
        return AI_INFERENCE_RESULT_STORAGE_ERROR;
    }
    *result = stored;
    return 0;
}
#endif

static int terp_service_reliability_get_crash_record(
    crash_record_v1_t *record, void *context)
{
    (void)context;
    return crash_record_target_get_visible_record(record)
                   == CRASH_RECORD_TARGET_OK
               ? 0
               : -TERP_ERROR_NOT_FOUND;
}

static int terp_service_reliability_ack_crash_record(uint32_t sequence,
                                                     void *context)
{
    crash_record_status_t status;

    (void)context;
    status = crash_record_target_ack(sequence);
    if (status == CRASH_RECORD_STATUS_OK)
    {
        return 0;
    }
    if (status == CRASH_RECORD_STATUS_NO_VALID
        || status == CRASH_RECORD_STATUS_STALE)
    {
        return -TERP_ERROR_NOT_FOUND;
    }
    return -TERP_ERROR_INTERNAL;
}

static int terp_service_get_event_evidence(uint32_t event_id,
                                           terp_event_evidence_t *evidence,
                                           void *context)
{
    terp_service_t *service = context;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return reliability_evidence_get_event_evidence(
        event_id, evidence, &service->reliability_evidence);
}

static int terp_service_get_crash_record(
    uint32_t sequence,
    uint32_t offset,
    uint8_t *data,
    uint32_t requested_length,
    uint32_t data_capacity,
    terp_crash_record_chunk_t *chunk,
    void *context)
{
    terp_service_t *service = context;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return reliability_evidence_get_crash_record(
        sequence, offset, data, requested_length, data_capacity, chunk,
        &service->reliability_evidence);
}

static int terp_service_ack_crash_record(uint32_t sequence, void *context)
{
    terp_service_t *service = context;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    return reliability_evidence_ack_crash_record(
        sequence, &service->reliability_evidence);
}
#endif

static void terp_service_copy_ota_progress(const ota_download_service_t *download,
                                           terp_ota_progress_t *progress)
{
    ota_download_progress_t download_progress;

    ota_download_query(download, &download_progress);
    progress->total_bytes = download_progress.total_bytes;
    progress->verified_bytes = download_progress.verified_bytes;
    progress->pending_install = download_progress.pending_install;
}

static int terp_service_model_erase(void *context, ota_model_slot_t slot,
                                    uint32_t offset, uint32_t length)
{
    (void)context;
    if (slot == OTA_MODEL_SLOT_A)
    {
        return ota_qspi_model_a_erase(context, offset, length);
    }
    if (slot == OTA_MODEL_SLOT_B)
    {
        return ota_qspi_model_b_erase(context, offset, length);
    }
    return -1;
}

static int terp_service_model_write(void *context, ota_model_slot_t slot,
                                    uint32_t offset, const uint8_t *data,
                                    uint32_t length)
{
    (void)context;
    if (slot == OTA_MODEL_SLOT_A)
    {
        return ota_qspi_model_a_write(context, offset, data, length);
    }
    if (slot == OTA_MODEL_SLOT_B)
    {
        return ota_qspi_model_b_write(context, offset, data, length);
    }
    return -1;
}

static int terp_service_model_read(void *context, ota_model_slot_t slot,
                                   uint32_t offset, uint8_t *data,
                                   uint32_t length)
{
    (void)context;
    if (slot == OTA_MODEL_SLOT_A)
    {
        return ota_qspi_model_a_read(context, offset, data, length);
    }
    if (slot == OTA_MODEL_SLOT_B)
    {
        return ota_qspi_model_b_read(context, offset, data, length);
    }
    return -1;
}

static void terp_service_copy_model_progress(
    const ota_model_lifecycle_t *lifecycle,
    terp_model_ota_progress_t *progress)
{
    ota_download_progress_t download_progress;

    ota_model_lifecycle_query(lifecycle, &download_progress);
    progress->total_bytes = download_progress.total_bytes;
    progress->verified_bytes = download_progress.verified_bytes;
    /* Model activation is synchronous with FINALIZE; unlike firmware OTA,
     * there is no deferred boot install represented on the wire. */
    progress->pending_install = 0U;
    progress->active_slot = (uint8_t)ota_model_lifecycle_get_active_slot(lifecycle);
    progress->model_valid = ota_model_lifecycle_has_valid_model(lifecycle) ? 1U : 0U;
}

static int terp_service_model_ota_status_error(ota_download_status_t status)
{
    switch (status)
    {
    case OTA_DOWNLOAD_STATUS_INVALID_ARGUMENT:
    case OTA_DOWNLOAD_STATUS_INVALID_PACKAGE:
        return -TERP_ERROR_INCOMPATIBLE;
    case OTA_DOWNLOAD_STATUS_NOT_STARTED:
    case OTA_DOWNLOAD_STATUS_OUT_OF_ORDER:
    case OTA_DOWNLOAD_STATUS_REPLAY_MISMATCH:
    case OTA_DOWNLOAD_STATUS_INCOMPLETE:
        return -TERP_ERROR_BUSY;
    case OTA_DOWNLOAD_STATUS_STORAGE_ERROR:
    default:
        return -TERP_ERROR_INTERNAL;
    }
}

static int terp_service_model_ota_begin(uint32_t total_bytes,
                                        terp_model_ota_progress_t *progress,
                                        void *context)
{
    terp_service_t *service = context;
    ota_download_status_t status;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (!service->model_ota_available)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    status = ota_model_lifecycle_begin(&service->model_lifecycle, total_bytes);
    if (status != OTA_DOWNLOAD_STATUS_OK)
    {
        return terp_service_model_ota_status_error(status);
    }
    terp_service_copy_model_progress(&service->model_lifecycle, progress);
    return 0;
}

static int terp_service_model_ota_write(uint32_t offset, const uint8_t *data,
                                        uint32_t length, uint32_t crc32,
                                        terp_model_ota_progress_t *progress,
                                        void *context)
{
    terp_service_t *service = context;
    ota_download_status_t status;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (!service->model_ota_available)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    status = ota_model_lifecycle_write(&service->model_lifecycle, offset, data,
                                       length, crc32);
    if (status != OTA_DOWNLOAD_STATUS_OK)
    {
        return terp_service_model_ota_status_error(status);
    }
    terp_service_copy_model_progress(&service->model_lifecycle, progress);
    return 0;
}

static int terp_service_model_ota_query(terp_model_ota_progress_t *progress,
                                        void *context)
{
    terp_service_t *service = context;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (!service->model_ota_available)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    if (ota_model_lifecycle_is_activation_uncertain(
            &service->model_lifecycle))
    {
        /* A progress page with INVALID/false would look like a clean
         * uninstalled state; force the host to surface the reboot-required
         * storage condition instead. */
        return -TERP_ERROR_INTERNAL;
    }
    terp_service_copy_model_progress(&service->model_lifecycle, progress);
    return 0;
}

static int terp_service_model_ota_finalize(
    terp_model_ota_progress_t *progress, void *context)
{
    terp_service_t *service = context;
    ota_download_status_t status;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (!service->model_ota_available)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    status = ota_model_lifecycle_finalize(&service->model_lifecycle);
    if (status != OTA_DOWNLOAD_STATUS_OK)
    {
        return terp_service_model_ota_status_error(status);
    }
    terp_service_copy_model_progress(&service->model_lifecycle, progress);
    return 0;
}

static int terp_service_model_ota_cancel(void *context)
{
    terp_service_t *service = context;
    ota_download_status_t status;

    if (service == NULL)
    {
        return -TERP_ERROR_INTERNAL;
    }
    if (!service->model_ota_available)
    {
        return -TERP_ERROR_UNSUPPORTED;
    }
    status = ota_model_lifecycle_cancel(&service->model_lifecycle);
    return status == OTA_DOWNLOAD_STATUS_OK
               ? 0
               : terp_service_model_ota_status_error(status);
}

static int terp_service_ota_begin(uint32_t total_bytes,
                                  terp_ota_progress_t *progress,
                                  void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_begin(&service->ota_download, total_bytes) != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->ota_download, progress);
    return 0;
}

static int terp_service_ota_write(uint32_t offset, const uint8_t *data,
                                  uint32_t length, uint32_t crc32,
                                  terp_ota_progress_t *progress,
                                  void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_write(&service->ota_download, offset, data, length, crc32)
           != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->ota_download, progress);
    return 0;
}

static int terp_service_ota_query(terp_ota_progress_t *progress, void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->ota_download, progress);
    return 0;
}

static int terp_service_ota_finalize(terp_ota_progress_t *progress, void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_finalize(&service->ota_download) != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->ota_download, progress);
    return 0;
}

static int terp_service_ota_cancel(void *context)
{
    terp_service_t *service = context;

    return service != 0 && service->ota_available
           && ota_download_cancel(&service->ota_download) == OTA_DOWNLOAD_STATUS_OK ? 0 : -1;
}

static int terp_service_recovery_begin(uint32_t total_bytes,
                                       terp_ota_progress_t *progress,
                                       void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_begin(&service->recovery_download, total_bytes) != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->recovery_download, progress);
    return 0;
}

static int terp_service_recovery_write(uint32_t offset, const uint8_t *data,
                                       uint32_t length, uint32_t crc32,
                                       terp_ota_progress_t *progress,
                                       void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_write(&service->recovery_download, offset, data, length, crc32)
           != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->recovery_download, progress);
    return 0;
}

static int terp_service_recovery_query(terp_ota_progress_t *progress, void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->recovery_download, progress);
    return 0;
}

static int terp_service_recovery_finalize(terp_ota_progress_t *progress, void *context)
{
    terp_service_t *service = context;

    if (service == 0 || !service->ota_available
        || ota_download_finalize(&service->recovery_download) != OTA_DOWNLOAD_STATUS_OK)
    {
        return -1;
    }
    terp_service_copy_ota_progress(&service->recovery_download, progress);
    return 0;
}

static int terp_service_recovery_cancel(void *context)
{
    terp_service_t *service = context;

    return service != 0 && service->ota_available
           && ota_download_cancel(&service->recovery_download) == OTA_DOWNLOAD_STATUS_OK ? 0 : -1;
}

static void terp_service_handle_message(const terp_message_t *message,
                                        void *context)
{
    terp_service_t *service = context;
    uint32_t response_length = 0U;

    if (service == 0 || service->write == 0
        || terp_device_handle_request(&service->device, message,
                                      service->work_buffer,
                                      sizeof(service->work_buffer),
                                      service->response_buffer,
                                      sizeof(service->response_buffer),
                                      &response_length) != 0)
    {
        return;
    }
    if (service->write(service->response_buffer, response_length,
                       service->write_context) == 0)
    {
        service->stats.tx_frame_count++;
    }
    else
    {
        service->stats.tx_error_count++;
    }
}

void terp_service_init(terp_service_t *service,
                       const terp_device_info_t *device_info,
                       const health_service_t *health_service,
                       time_service_t *time_service,
                       terp_service_write_fn write,
                       void *write_context)
{
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    int reliability_backend_ready = 0;
#endif
    const ota_download_storage_t ota_storage = {
        .erase = ota_qspi_candidate_erase,
        .write = ota_qspi_candidate_write,
        .read = ota_qspi_candidate_read,
        .mark_pending_install = ota_state_app_store_mark_pending_install,
        .context = 0,
        .capacity_bytes = TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
    };
    const ota_download_storage_t recovery_storage = {
        .erase = ota_qspi_recovery_erase,
        .write = ota_qspi_recovery_write,
        .read = ota_qspi_recovery_read,
        .mark_pending_install = 0,
        .context = 0,
        .capacity_bytes = TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES,
    };
    const ota_model_storage_t model_storage = {
        .erase_slot = terp_service_model_erase,
        .write_slot = terp_service_model_write,
        .read_slot = terp_service_model_read,
        .erase_state = ota_qspi_model_state_erase,
        .write_state = ota_qspi_model_state_write,
        .read_state = ota_qspi_model_state_read,
        .erase_state_bank = ota_qspi_model_state_bank_erase,
        .write_state_bank = ota_qspi_model_state_bank_write,
        .read_state_bank = ota_qspi_model_state_bank_read,
        .context = 0,
        .slot_capacity_bytes = TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
        .state_capacity_bytes = TRANSPORT_OTA_QSPI_MODEL_STATE_SIZE_BYTES,
        .erase_block_bytes = TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES,
        .state_bank_count = 2U,
    };
    static const terp_device_ops_t default_ops = {
        .get_health = terp_service_get_health,
        .get_time = terp_service_get_time,
        .set_time = terp_service_set_time,
        .list_events = terp_service_list_events,
        .get_event_info = terp_service_get_event_info,
        .read_event = terp_service_read_event,
        .set_live = 0,
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
        .get_ai_result = terp_service_get_ai_result,
#endif
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
        .get_event_evidence = 0,
        .get_crash_record = 0,
        .ack_crash_record = 0,
#endif
        .ota_begin = terp_service_ota_begin,
        .ota_write = terp_service_ota_write,
        .ota_query = terp_service_ota_query,
        .ota_finalize = terp_service_ota_finalize,
        .ota_cancel = terp_service_ota_cancel,
        .recovery_begin = terp_service_recovery_begin,
        .recovery_write = terp_service_recovery_write,
        .recovery_query = terp_service_recovery_query,
        .recovery_finalize = terp_service_recovery_finalize,
        .recovery_cancel = terp_service_recovery_cancel,
        .model_ota_begin = terp_service_model_ota_begin,
        .model_ota_write = terp_service_model_ota_write,
        .model_ota_query = terp_service_model_ota_query,
        .model_ota_finalize = terp_service_model_ota_finalize,
        .model_ota_cancel = terp_service_model_ota_cancel,
    };

    if (service == 0)
    {
        return;
    }
    memset(service, 0, sizeof(*service));
    service->health_service = health_service;
    service->time_service = time_service;
    service->write = write;
    service->write_context = write_context;
    service->device_ops = default_ops;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    reliability_backend_ready = crash_record_target_is_ready();
#endif
    if (ota_qspi_candidate_init())
    {
        ota_download_service_init(&service->ota_download, &ota_storage);
        ota_download_service_init(&service->recovery_download, &recovery_storage);
        service->ota_available = 1U;
        if (ota_model_lifecycle_init(&service->model_lifecycle, &model_storage))
        {
            service->model_ota_available = 1U;
        }
    }
    else
    {
        service->device_ops.ota_begin = 0;
        service->device_ops.ota_write = 0;
        service->device_ops.ota_query = 0;
        service->device_ops.ota_finalize = 0;
        service->device_ops.ota_cancel = 0;
        service->device_ops.recovery_begin = 0;
        service->device_ops.recovery_write = 0;
        service->device_ops.recovery_query = 0;
        service->device_ops.recovery_finalize = 0;
        service->device_ops.recovery_cancel = 0;
        service->device_ops.model_ota_begin = 0;
        service->device_ops.model_ota_write = 0;
        service->device_ops.model_ota_query = 0;
        service->device_ops.model_ota_finalize = 0;
        service->device_ops.model_ota_cancel = 0;
    }
    if (!service->model_ota_available)
    {
        service->device_ops.model_ota_begin = 0;
        service->device_ops.model_ota_write = 0;
        service->device_ops.model_ota_query = 0;
        service->device_ops.model_ota_finalize = 0;
        service->device_ops.model_ota_cancel = 0;
    }
    if (time_service == 0)
    {
        service->device_ops.get_time = 0;
        service->device_ops.set_time = 0;
    }
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    reliability_evidence_set_msh_service(NULL);
    if (reliability_backend_ready)
    {
        const reliability_evidence_ops_t evidence_ops = {
            .get_event_info = terp_service_reliability_get_event_info,
            .verify_event = terp_service_reliability_verify_event,
            .read_event = terp_service_reliability_read_event,
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
            .get_ai_result_store_status =
                terp_service_reliability_get_ai_result_store_status,
            .get_ai_result = terp_service_reliability_get_ai_result,
#endif
            .get_crash_record = terp_service_reliability_get_crash_record,
            .ack_crash_record = terp_service_reliability_ack_crash_record,
        };

        reliability_evidence_init(&service->reliability_evidence,
                                  &evidence_ops, NULL);
        service->device_ops.get_event_evidence =
            terp_service_get_event_evidence;
        service->device_ops.get_crash_record = terp_service_get_crash_record;
        service->device_ops.ack_crash_record = terp_service_ack_crash_record;
        reliability_evidence_set_msh_service(&service->reliability_evidence);
    }
#endif
    terp_parser_init(&service->parser);
    terp_device_init(&service->device, &service->device_ops, service);
    if (device_info != 0)
    {
        terp_device_set_info(&service->device, device_info);
    }
    if (service->ota_available)
    {
        service->device.info.capability_flags |= TERP_CAPABILITY_OTA;
    }
    if (service->model_ota_available)
    {
        service->device.info.capability_flags |= TERP_CAPABILITY_MODEL_OTA;
    }
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    service->device.info.capability_flags |= TERP_CAPABILITY_AI_RESULTS;
#endif
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    service->device.info.capability_flags &=
        ~TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1;
    if (reliability_backend_ready)
    {
        service->device.info.capability_flags |=
            TERP_CAPABILITY_RELIABILITY_EVIDENCE_V1;
    }
#endif
}

void terp_service_receive(terp_service_t *service,
                          const uint8_t *data,
                          uint32_t length)
{
    if (service != 0)
    {
        terp_parser_feed(&service->parser, data, length,
                         terp_service_handle_message, service);
    }
}

void terp_service_on_timeout(terp_service_t *service)
{
    if (service != 0)
    {
        terp_parser_on_timeout(&service->parser);
    }
}

void terp_service_get_stats(const terp_service_t *service,
                            terp_service_stats_t *stats)
{
    if (stats != 0)
    {
        if (service != 0)
        {
            *stats = service->stats;
        }
        else
        {
            memset(stats, 0, sizeof(*stats));
        }
    }
}

void terp_service_set_runtime_model_activation(
    terp_service_t *service,
    ota_model_runtime_prepare_fn prepare_runtime_model,
    ota_model_runtime_publish_fn publish_runtime_model,
    ota_model_runtime_abort_fn abort_runtime_model,
    ota_model_runtime_quarantine_fn quarantine_runtime_model,
    void *context)
{
    if (service != NULL)
    {
        ota_model_lifecycle_set_runtime_activation(
            &service->model_lifecycle, prepare_runtime_model,
            publish_runtime_model, abort_runtime_model,
            quarantine_runtime_model, context);
    }
}

const ai_model_t *terp_service_get_runtime_model(
    const terp_service_t *service)
{
    return service == NULL
               ? RT_NULL
               : ota_model_lifecycle_get_runtime_model(&service->model_lifecycle);
}
