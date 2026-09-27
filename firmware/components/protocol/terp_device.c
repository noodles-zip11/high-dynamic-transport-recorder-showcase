#include "terp_device.h"

#include <string.h>

#define TERP_EVENT_INFO_BYTES UINT32_C(12)
#define TERP_EVENT_CHUNK_HEADER_BYTES UINT32_C(20)

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static int64_t get_i64_le(const uint8_t *data)
{
    uint64_t value = (uint64_t)get_u32_le(data)
                     | ((uint64_t)get_u32_le(&data[4]) << 32U);

    return (int64_t)value;
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

static void put_float_le(uint8_t *data, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_u32_le(data, bits);
}

static void put_i64_le(uint8_t *data, int64_t value)
{
    uint64_t unsigned_value = (uint64_t)value;

    put_u32_le(data, (uint32_t)unsigned_value);
    put_u32_le(&data[4], (uint32_t)(unsigned_value >> 32U));
}

static int encode_response(uint16_t message_type,
                           uint32_t sequence,
                           const uint8_t *payload,
                           uint32_t payload_length,
                           uint8_t *response_buffer,
                           uint32_t response_capacity,
                           uint32_t *response_length)
{
    const terp_message_t response = {
        .message_type = message_type,
        .flags = TERP_FLAG_RESPONSE,
        .sequence = sequence,
        .payload = payload,
        .payload_length = payload_length,
    };

    return terp_encode_frame(&response, response_buffer, response_capacity,
                             response_length);
}

static int encode_error(const terp_message_t *request,
                        terp_error_code_t error,
                        uint8_t *work_buffer,
                        uint8_t *response_buffer,
                        uint32_t response_capacity,
                        uint32_t *response_length)
{
    put_u16_le(work_buffer, (uint16_t)error);
    put_u16_le(&work_buffer[2], request->message_type);
    return encode_response(TERP_MESSAGE_ERROR, request->sequence, work_buffer, 4U,
                           response_buffer, response_capacity, response_length);
}

static int append_text(uint8_t *output,
                       uint32_t capacity,
                       uint32_t *length,
                       const char *text,
                       uint8_t text_length)
{
    if (*length >= capacity || text_length > capacity - *length - 1U
        || (text_length != 0U && text == 0))
    {
        return -1;
    }
    output[(*length)++] = text_length;
    if (text_length != 0U)
    {
        memcpy(&output[*length], text, text_length);
        *length += text_length;
    }
    return 0;
}

static int handle_hello(terp_device_t *device,
                        const terp_message_t *request,
                        uint8_t *work_buffer,
                        uint8_t *response_buffer,
                        uint32_t response_capacity,
                        uint32_t *response_length)
{
    int result;

    if (request->payload_length != 1U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (request->payload[0] != TERP_VERSION)
    {
        return encode_error(request, TERP_ERROR_INCOMPATIBLE, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    work_buffer[0] = TERP_VERSION;
    put_u32_le(&work_buffer[1], TERP_MAX_PAYLOAD_BYTES);
    put_u32_le(&work_buffer[5], device->maximum_event_chunk_bytes);
    put_u32_le(&work_buffer[9], device->info.capability_flags);
    result = encode_response(terp_response_type(request->message_type), request->sequence,
                             work_buffer, 13U, response_buffer, response_capacity,
                             response_length);
    if (result == 0)
    {
        device->session_ready = 1U;
    }
    return result;
}

static int handle_device_info(terp_device_t *device,
                              const terp_message_t *request,
                              uint8_t *work_buffer,
                              uint32_t work_capacity,
                              uint8_t *response_buffer,
                              uint32_t response_capacity,
                              uint32_t *response_length)
{
    uint32_t length = 0U;

    if (request->payload_length != 0U
        || append_text(work_buffer, work_capacity, &length, device->info.model,
                       device->info.model_length) != 0
        || append_text(work_buffer, work_capacity, &length,
                       device->info.firmware_version,
                       device->info.firmware_version_length) != 0
        || append_text(work_buffer, work_capacity, &length,
                       device->info.hardware_version,
                       device->info.hardware_version_length) != 0
        || append_text(work_buffer, work_capacity, &length,
                       device->info.serial_number,
                       device->info.serial_number_length) != 0
        || length > work_capacity - 4U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_u32_le(&work_buffer[length], device->info.capability_flags);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, length + 4U, response_buffer, response_capacity,
                           response_length);
}

static int handle_health(terp_device_t *device,
                         const terp_message_t *request,
                         uint8_t *work_buffer,
                         uint8_t *response_buffer,
                         uint32_t response_capacity,
                         uint32_t *response_length)
{
    terp_health_info_t health;

    if (request->payload_length != 0U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_health == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->get_health(&health, device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    work_buffer[0] = health.state;
    work_buffer[1] = health.storage_ready;
    put_u32_le(&work_buffer[2], health.free_log_bytes);
    put_u32_le(&work_buffer[6], health.storage_error_count);
    put_u32_le(&work_buffer[10], health.event_export_error_count);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 14U, response_buffer, response_capacity,
                           response_length);
}

static int handle_get_time(terp_device_t *device,
                           const terp_message_t *request,
                           uint8_t *work_buffer,
                           uint8_t *response_buffer,
                           uint32_t response_capacity,
                           uint32_t *response_length)
{
    int64_t utc_unix_seconds;
    uint32_t epoch_id;

    if (request->payload_length != 0U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_time == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->get_time(&utc_unix_seconds, &epoch_id, device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_i64_le(work_buffer, utc_unix_seconds);
    put_u32_le(&work_buffer[8], epoch_id);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 12U, response_buffer, response_capacity,
                           response_length);
}

static int handle_set_time(terp_device_t *device,
                           const terp_message_t *request,
                           uint8_t *work_buffer,
                           uint8_t *response_buffer,
                           uint32_t response_capacity,
                           uint32_t *response_length)
{
    uint32_t epoch_id;

    if (request->payload_length != 8U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->set_time == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->set_time(get_i64_le(request->payload), &epoch_id,
                              device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_i64_le(work_buffer, get_i64_le(request->payload));
    put_u32_le(&work_buffer[8], epoch_id);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 12U, response_buffer, response_capacity,
                           response_length);
}

static int encode_ota_progress(const terp_message_t *request,
                               const terp_ota_progress_t *progress,
                               uint8_t *work_buffer,
                               uint8_t *response_buffer,
                               uint32_t response_capacity,
                               uint32_t *response_length)
{
    put_u32_le(work_buffer, progress->total_bytes);
    put_u32_le(&work_buffer[4], progress->verified_bytes);
    work_buffer[8] = progress->pending_install;
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 9U, response_buffer, response_capacity,
                           response_length);
}

static int handle_ota(terp_device_t *device, const terp_message_t *request,
                      uint8_t *work_buffer, uint8_t *response_buffer,
                      uint32_t response_capacity, uint32_t *response_length)
{
    terp_ota_progress_t progress = {0};
    terp_device_ota_begin_fn begin;
    terp_device_ota_write_fn write;
    terp_device_ota_query_fn query;
    terp_device_ota_finalize_fn finalize;
    terp_device_ota_cancel_fn cancel;
    int result;

    if (device->ops == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (request->message_type == TERP_MESSAGE_OTA_RECOVERY_BEGIN
        || request->message_type == TERP_MESSAGE_OTA_RECOVERY_WRITE_CHUNK
        || request->message_type == TERP_MESSAGE_OTA_RECOVERY_QUERY
        || request->message_type == TERP_MESSAGE_OTA_RECOVERY_FINALIZE
        || request->message_type == TERP_MESSAGE_OTA_RECOVERY_CANCEL)
    {
        begin = device->ops->recovery_begin;
        write = device->ops->recovery_write;
        query = device->ops->recovery_query;
        finalize = device->ops->recovery_finalize;
        cancel = device->ops->recovery_cancel;
    }
    else
    {
        begin = device->ops->ota_begin;
        write = device->ops->ota_write;
        query = device->ops->ota_query;
        finalize = device->ops->ota_finalize;
        cancel = device->ops->ota_cancel;
    }
    switch (request->message_type)
    {
    case TERP_MESSAGE_OTA_BEGIN:
    case TERP_MESSAGE_OTA_RECOVERY_BEGIN:
        if (request->payload_length != 4U)
        {
            return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        if (begin == 0)
        {
            return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        result = begin(get_u32_le(request->payload), &progress, device->context);
        goto done;
    case TERP_MESSAGE_OTA_WRITE_CHUNK:
    case TERP_MESSAGE_OTA_RECOVERY_WRITE_CHUNK:
        if (request->payload_length <= 8U)
        {
            return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        if (write == 0)
        {
            return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        result = write(get_u32_le(request->payload), &request->payload[8],
                       request->payload_length - 8U, get_u32_le(&request->payload[4]),
                       &progress, device->context);
        goto done;
    case TERP_MESSAGE_OTA_QUERY:
    case TERP_MESSAGE_OTA_RECOVERY_QUERY:
        if (request->payload_length != 0U)
        {
            return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        if (query == 0)
        {
            return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        result = query(&progress, device->context);
        goto done;
    case TERP_MESSAGE_OTA_FINALIZE:
    case TERP_MESSAGE_OTA_RECOVERY_FINALIZE:
        if (request->payload_length != 0U)
        {
            return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        if (finalize == 0)
        {
            return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        result = finalize(&progress, device->context);
        goto done;
    case TERP_MESSAGE_OTA_CANCEL:
    case TERP_MESSAGE_OTA_RECOVERY_CANCEL:
        if (request->payload_length != 0U)
        {
            return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        if (cancel == 0)
        {
            return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                                response_buffer, response_capacity, response_length);
        }
        result = cancel(device->context);
        if (result == 0)
        {
            return encode_response(terp_response_type(request->message_type),
                                   request->sequence, 0, 0U, response_buffer,
                                   response_capacity, response_length);
        }
        goto failed;
    default:
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
done:
    if (result == 0)
    {
        return encode_ota_progress(request, &progress, work_buffer, response_buffer,
                                   response_capacity, response_length);
    }
failed:
    return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                        response_buffer, response_capacity, response_length);
}

static int encode_model_ota_progress(const terp_message_t *request,
                                      const terp_model_ota_progress_t *progress,
                                      uint8_t *work_buffer,
                                      uint8_t *response_buffer,
                                      uint32_t response_capacity,
                                      uint32_t *response_length)
{
    put_u32_le(work_buffer, progress->total_bytes);
    put_u32_le(&work_buffer[4], progress->verified_bytes);
    work_buffer[8] = progress->pending_install;
    work_buffer[9] = progress->active_slot;
    work_buffer[10] = progress->model_valid;
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 11U, response_buffer, response_capacity,
                           response_length);
}

static terp_error_code_t model_ota_error_code(int result)
{
    const int error_code = -result;

    return result < 0 && error_code >= TERP_ERROR_MALFORMED
                   && error_code <= TERP_ERROR_HANDSHAKE_REQUIRED
               ? (terp_error_code_t)error_code
               : TERP_ERROR_INTERNAL;
}

static int handle_model_ota(terp_device_t *device,
                            const terp_message_t *request,
                            uint8_t *work_buffer,
                            uint8_t *response_buffer,
                            uint32_t response_capacity,
                            uint32_t *response_length)
{
    terp_model_ota_progress_t progress = {0};
    int result;

    if (device->ops == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    switch (request->message_type)
    {
    case TERP_MESSAGE_MODEL_OTA_BEGIN:
        if (request->payload_length != 4U
            || device->ops->model_ota_begin == 0)
        {
            return encode_error(request,
                                request->payload_length != 4U
                                    ? TERP_ERROR_MALFORMED : TERP_ERROR_UNSUPPORTED,
                                work_buffer, response_buffer, response_capacity,
                                response_length);
        }
        result = device->ops->model_ota_begin(get_u32_le(request->payload),
                                              &progress, device->context);
        break;
    case TERP_MESSAGE_MODEL_OTA_WRITE_CHUNK:
        if (request->payload_length <= 8U
            || device->ops->model_ota_write == 0)
        {
            return encode_error(request,
                                request->payload_length <= 8U
                                    ? TERP_ERROR_MALFORMED : TERP_ERROR_UNSUPPORTED,
                                work_buffer, response_buffer, response_capacity,
                                response_length);
        }
        result = device->ops->model_ota_write(
            get_u32_le(request->payload), &request->payload[8],
            request->payload_length - 8U, get_u32_le(&request->payload[4]),
            &progress, device->context);
        break;
    case TERP_MESSAGE_MODEL_OTA_QUERY:
        if (request->payload_length != 0U
            || device->ops->model_ota_query == 0)
        {
            return encode_error(request,
                                request->payload_length != 0U
                                    ? TERP_ERROR_MALFORMED : TERP_ERROR_UNSUPPORTED,
                                work_buffer, response_buffer, response_capacity,
                                response_length);
        }
        result = device->ops->model_ota_query(&progress, device->context);
        break;
    case TERP_MESSAGE_MODEL_OTA_FINALIZE:
        if (request->payload_length != 0U
            || device->ops->model_ota_finalize == 0)
        {
            return encode_error(request,
                                request->payload_length != 0U
                                    ? TERP_ERROR_MALFORMED : TERP_ERROR_UNSUPPORTED,
                                work_buffer, response_buffer, response_capacity,
                                response_length);
        }
        result = device->ops->model_ota_finalize(&progress, device->context);
        break;
    case TERP_MESSAGE_MODEL_OTA_CANCEL:
        if (request->payload_length != 0U
            || device->ops->model_ota_cancel == 0)
        {
            return encode_error(request,
                                request->payload_length != 0U
                                    ? TERP_ERROR_MALFORMED : TERP_ERROR_UNSUPPORTED,
                                work_buffer, response_buffer, response_capacity,
                                response_length);
        }
        result = device->ops->model_ota_cancel(device->context);
        if (result == 0)
        {
            return encode_response(terp_response_type(request->message_type),
                                   request->sequence, 0, 0U, response_buffer,
                                   response_capacity, response_length);
        }
        break;
    default:
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (result == 0)
    {
        return encode_model_ota_progress(request, &progress, work_buffer,
                                         response_buffer, response_capacity,
                                         response_length);
    }
    return encode_error(request, model_ota_error_code(result), work_buffer,
                        response_buffer, response_capacity, response_length);
}

static int handle_list_events(terp_device_t *device,
                              const terp_message_t *request,
                              uint8_t *work_buffer,
                              uint32_t work_capacity,
                              uint8_t *response_buffer,
                              uint32_t response_capacity,
                              uint32_t *response_length)
{
    terp_event_info_t events[TERP_LIST_MAXIMUM_COUNT];
    uint32_t after_event_id;
    uint32_t next_event_id;
    uint32_t requested_count;
    uint16_t maximum_count;
    uint16_t event_count;
    uint16_t index;
    uint32_t length = 6U;

    if (request->payload_length != 8U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->list_events == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    after_event_id = get_u32_le(request->payload);
    requested_count = get_u32_le(&request->payload[4]);
    if (requested_count == 0U || requested_count > TERP_LIST_MAXIMUM_COUNT)
    {
        maximum_count = TERP_LIST_MAXIMUM_COUNT;
    }
    else
    {
        maximum_count = (uint16_t)requested_count;
    }
    if (device->ops->list_events(after_event_id, maximum_count, events,
                                 TERP_LIST_MAXIMUM_COUNT, &event_count,
                                 &next_event_id, device->context) != 0
        || event_count > maximum_count
        || event_count > TERP_LIST_MAXIMUM_COUNT
        || event_count > (work_capacity - length) / TERP_EVENT_INFO_BYTES)
    {
        return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_u32_le(work_buffer, next_event_id);
    put_u16_le(&work_buffer[4], event_count);
    for (index = 0U; index < event_count; index++)
    {
        put_u32_le(&work_buffer[length], events[index].event_id);
        put_u32_le(&work_buffer[length + 4U], events[index].total_length);
        put_u32_le(&work_buffer[length + 8U], events[index].event_crc32);
        length += TERP_EVENT_INFO_BYTES;
    }
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, length, response_buffer, response_capacity,
                           response_length);
}

static int handle_get_event_info(terp_device_t *device,
                                 const terp_message_t *request,
                                 uint8_t *work_buffer,
                                 uint8_t *response_buffer,
                                 uint32_t response_capacity,
                                 uint32_t *response_length)
{
    terp_event_info_t info;

    if (request->payload_length != 4U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_event_info == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->get_event_info(get_u32_le(request->payload), &info,
                                    device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_NOT_FOUND, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_u32_le(work_buffer, info.event_id);
    put_u32_le(&work_buffer[4], info.total_length);
    put_u32_le(&work_buffer[8], info.event_crc32);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, TERP_EVENT_INFO_BYTES, response_buffer,
                           response_capacity, response_length);
}

static int handle_read_event_chunk(terp_device_t *device,
                                   const terp_message_t *request,
                                   uint8_t *work_buffer,
                                   uint32_t work_capacity,
                                   uint8_t *response_buffer,
                                   uint32_t response_capacity,
                                   uint32_t *response_length)
{
    terp_event_info_t info;
    uint32_t event_id;
    uint32_t offset;
    uint32_t requested_length;
    uint32_t actual_length;
    uint32_t maximum_length;

    if (request->payload_length != 12U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_event_info == 0
        || device->ops->read_event == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    event_id = get_u32_le(request->payload);
    offset = get_u32_le(&request->payload[4]);
    requested_length = get_u32_le(&request->payload[8]);
    maximum_length = device->maximum_event_chunk_bytes;
    if (maximum_length > work_capacity - TERP_EVENT_CHUNK_HEADER_BYTES)
    {
        maximum_length = work_capacity - TERP_EVENT_CHUNK_HEADER_BYTES;
    }
    if (requested_length == 0U || requested_length > maximum_length)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->get_event_info(event_id, &info, device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_NOT_FOUND, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (offset > info.total_length
        || device->ops->read_event(event_id, offset,
                                   &work_buffer[TERP_EVENT_CHUNK_HEADER_BYTES],
                                   requested_length, &actual_length,
                                   device->context) != 0
        || actual_length > requested_length
        || actual_length > info.total_length - offset)
    {
        return encode_error(request, TERP_ERROR_INTERNAL, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    put_u32_le(work_buffer, event_id);
    put_u32_le(&work_buffer[4], offset);
    put_u32_le(&work_buffer[8], info.total_length);
    put_u32_le(&work_buffer[12], actual_length);
    put_u32_le(&work_buffer[16], terp_crc32(&work_buffer[20], actual_length));
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, TERP_EVENT_CHUNK_HEADER_BYTES + actual_length,
                           response_buffer, response_capacity, response_length);
}

static int handle_live(terp_device_t *device,
                       const terp_message_t *request,
                       int enabled,
                       uint8_t *work_buffer,
                       uint8_t *response_buffer,
                       uint32_t response_capacity,
                       uint32_t *response_length)
{
    if (request->payload_length != 0U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->set_live == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops->set_live(enabled, device->context) != 0)
    {
        return encode_error(request, TERP_ERROR_BUSY, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           0, 0U, response_buffer, response_capacity,
                           response_length);
}

static int handle_get_ai_result(terp_device_t *device,
                                const terp_message_t *request,
                                uint8_t *work_buffer,
                                uint32_t work_capacity,
                                uint8_t *response_buffer,
                                uint32_t response_capacity,
                                uint32_t *response_length)
{
    terp_ai_result_t result;
    uint32_t index;
    int callback_result;

    if (request->payload_length != 4U || work_capacity < 48U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_ai_result == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    memset(&result, 0, sizeof(result));
    callback_result = device->ops->get_ai_result(get_u32_le(request->payload),
                                                 &result, device->context);
    if (callback_result != 0)
    {
        return encode_error(request,
                            callback_result == -TERP_ERROR_NOT_FOUND
                                ? TERP_ERROR_NOT_FOUND : TERP_ERROR_INTERNAL,
                            work_buffer, response_buffer, response_capacity,
                            response_length);
    }
    put_u32_le(&work_buffer[0], result.event_id);
    put_u16_le(&work_buffer[4], result.model_version);
    work_buffer[6] = result.status;
    work_buffer[7] = result.class_index;
    work_buffer[8] = result.class_count;
    work_buffer[9] = result.quality_flags;
    put_u16_le(&work_buffer[10], result.event_flags);
    put_u32_le(&work_buffer[12], result.sample_count);
    put_u32_le(&work_buffer[16], result.model_crc32);
    put_float_le(&work_buffer[20], result.confidence);
    for (index = 0U; index < 4U; index++)
    {
        put_float_le(&work_buffer[24U + index * sizeof(float)], result.logits[index]);
    }
    put_u16_le(&work_buffer[40], result.failure_reason);
    put_u16_le(&work_buffer[42], 0U);
    put_u32_le(&work_buffer[44], result.result_sequence);
    return encode_response(terp_response_type(request->message_type), request->sequence,
                           work_buffer, 48U, response_buffer, response_capacity,
                           response_length);
}

static terp_error_code_t callback_error_code(int callback_result)
{
    switch (callback_result)
    {
    case -TERP_ERROR_MALFORMED:
        return TERP_ERROR_MALFORMED;
    case -TERP_ERROR_UNSUPPORTED:
        return TERP_ERROR_UNSUPPORTED;
    case -TERP_ERROR_NOT_FOUND:
        return TERP_ERROR_NOT_FOUND;
    case -TERP_ERROR_INTERNAL:
        return TERP_ERROR_INTERNAL;
    default:
        return TERP_ERROR_INTERNAL;
    }
}

static int handle_get_event_evidence(terp_device_t *device,
                                     const terp_message_t *request,
                                     uint8_t *work_buffer,
                                     uint8_t *response_buffer,
                                     uint32_t response_capacity,
                                     uint32_t *response_length)
{
    terp_event_evidence_t evidence;
    int callback_result;

    if (request->payload_length != 4U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_event_evidence == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    memset(&evidence, 0, sizeof(evidence));
    callback_result = device->ops->get_event_evidence(
        get_u32_le(request->payload), &evidence, device->context);
    if (callback_result != 0)
    {
        return encode_error(request, callback_error_code(callback_result),
                            work_buffer, response_buffer, response_capacity,
                            response_length);
    }
    put_u32_le(&work_buffer[0], evidence.event_id);
    put_u16_le(&work_buffer[4], evidence.evidence_version);
    work_buffer[6] = evidence.verdict;
    work_buffer[7] = evidence.ai_decision;
    put_u32_le(&work_buffer[8], evidence.reason_flags);
    work_buffer[12] = evidence.storage_state;
    work_buffer[13] = evidence.ai_result_status;
    put_u16_le(&work_buffer[14], evidence.ai_failure_reason);
    put_u32_le(&work_buffer[16], evidence.ai_result_sequence);
    return encode_response(terp_response_type(request->message_type),
                           request->sequence, work_buffer,
                           TERP_EVENT_EVIDENCE_RESPONSE_BYTES, response_buffer,
                           response_capacity, response_length);
}

static int handle_get_crash_record(terp_device_t *device,
                                   const terp_message_t *request,
                                   uint8_t *work_buffer,
                                   uint32_t work_capacity,
                                   uint8_t *response_buffer,
                                   uint32_t response_capacity,
                                   uint32_t *response_length)
{
    terp_crash_record_chunk_t chunk;
    uint32_t sequence;
    uint32_t offset;
    uint32_t requested_length;
    uint32_t response_payload_length;
    uint32_t maximum_length;
    int callback_result;

    if (request->payload_length != 12U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    sequence = get_u32_le(&request->payload[0]);
    offset = get_u32_le(&request->payload[4]);
    requested_length = get_u32_le(&request->payload[8]);
    maximum_length = device->maximum_event_chunk_bytes;
    if (maximum_length > TERP_DEVICE_MAX_CRASH_RECORD_CHUNK_BYTES)
    {
        maximum_length = TERP_DEVICE_MAX_CRASH_RECORD_CHUNK_BYTES;
    }
    if (maximum_length > work_capacity
        - TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES)
    {
        maximum_length = work_capacity
                         - TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES;
    }
    if (requested_length == 0U
        || requested_length > maximum_length
        || offset > UINT32_MAX - requested_length)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->get_crash_record == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    memset(&chunk, 0, sizeof(chunk));
    callback_result = device->ops->get_crash_record(
        sequence, offset, &work_buffer[TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES],
        requested_length, requested_length, &chunk, device->context);
    if (callback_result != 0)
    {
        return encode_error(request, callback_error_code(callback_result),
                            work_buffer, response_buffer, response_capacity,
                            response_length);
    }
    if (chunk.sequence == 0U
        || (sequence != 0U && chunk.sequence != sequence)
        || chunk.total_length == 0U
        || chunk.actual_length == 0U
        || chunk.actual_length > requested_length
        || offset > chunk.total_length
        || chunk.actual_length > chunk.total_length - offset)
    {
        terp_error_code_t error = (sequence != 0U
                                   && chunk.sequence != sequence)
                                      ? TERP_ERROR_NOT_FOUND
                                      : TERP_ERROR_INTERNAL;
        return encode_error(request, error, work_buffer, response_buffer,
                            response_capacity, response_length);
    }
    put_u32_le(&work_buffer[0], chunk.sequence);
    put_u32_le(&work_buffer[4], offset);
    put_u32_le(&work_buffer[8], chunk.total_length);
    put_u32_le(&work_buffer[12], chunk.actual_length);
    put_u32_le(&work_buffer[16], terp_crc32(
                                    &work_buffer[TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES],
                                    chunk.actual_length));
    response_payload_length = TERP_CRASH_RECORD_RESPONSE_HEADER_BYTES
                              + chunk.actual_length;
    return encode_response(terp_response_type(request->message_type),
                           request->sequence, work_buffer,
                           response_payload_length, response_buffer,
                           response_capacity, response_length);
}

static int handle_ack_crash_record(terp_device_t *device,
                                   const terp_message_t *request,
                                   uint8_t *work_buffer,
                                   uint8_t *response_buffer,
                                   uint32_t response_capacity,
                                   uint32_t *response_length)
{
    uint32_t sequence;
    int callback_result;

    if (request->payload_length != 4U
        || get_u32_le(request->payload) == 0U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (device->ops == 0 || device->ops->ack_crash_record == 0)
    {
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    sequence = get_u32_le(request->payload);
    callback_result = device->ops->ack_crash_record(sequence, device->context);
    if (callback_result != 0)
    {
        return encode_error(request, callback_error_code(callback_result),
                            work_buffer, response_buffer, response_capacity,
                            response_length);
    }
    put_u32_le(work_buffer, sequence);
    return encode_response(terp_response_type(request->message_type),
                           request->sequence, work_buffer, 4U, response_buffer,
                           response_capacity, response_length);
}

void terp_device_init(terp_device_t *device,
                      const terp_device_ops_t *ops,
                      void *context)
{
    static const char model[] = "STM32H743";
    static const char firmware[] = "phase08-unbound";
    static const char hardware[] = "usb-pending";
    static const char serial[] = "pending";

    if (device == 0)
    {
        return;
    }
    memset(device, 0, sizeof(*device));
    device->ops = ops;
    device->context = context;
    device->info.model = model;
    device->info.model_length = (uint8_t)(sizeof(model) - 1U);
    device->info.firmware_version = firmware;
    device->info.firmware_version_length = (uint8_t)(sizeof(firmware) - 1U);
    device->info.hardware_version = hardware;
    device->info.hardware_version_length = (uint8_t)(sizeof(hardware) - 1U);
    device->info.serial_number = serial;
    device->info.serial_number_length = (uint8_t)(sizeof(serial) - 1U);
    device->info.capability_flags = TERP_CAPABILITY_DEVICE_INFO;
    device->maximum_event_chunk_bytes = TERP_DEVICE_MAX_EVENT_CHUNK_BYTES;
}

void terp_device_set_info(terp_device_t *device,
                          const terp_device_info_t *info)
{
    if (device != 0 && info != 0)
    {
        device->info = *info;
    }
}

void terp_device_reset_session(terp_device_t *device)
{
    if (device != 0)
    {
        device->session_ready = 0U;
    }
}

int terp_device_handle_request(terp_device_t *device,
                               const terp_message_t *request,
                               uint8_t *work_buffer,
                               uint32_t work_capacity,
                               uint8_t *response_buffer,
                               uint32_t response_capacity,
                               uint32_t *response_length)
{
    if (device == 0 || request == 0 || work_buffer == 0 || work_capacity < 32U
        || response_buffer == 0 || response_length == 0
        || (request->payload_length != 0U && request->payload == 0))
    {
        return -1;
    }
    if ((request->flags & (TERP_FLAG_RESPONSE | TERP_FLAG_NOTIFICATION)) != 0U)
    {
        return encode_error(request, TERP_ERROR_MALFORMED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
    if (request->message_type != TERP_MESSAGE_HELLO && !device->session_ready)
    {
        return encode_error(request, TERP_ERROR_HANDSHAKE_REQUIRED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }

    switch (request->message_type)
    {
    case TERP_MESSAGE_HELLO:
        return handle_hello(device, request, work_buffer, response_buffer,
                            response_capacity, response_length);
    case TERP_MESSAGE_GET_DEVICE_INFO:
        return handle_device_info(device, request, work_buffer, work_capacity,
                                  response_buffer, response_capacity,
                                  response_length);
    case TERP_MESSAGE_GET_HEALTH:
        return handle_health(device, request, work_buffer, response_buffer,
                             response_capacity, response_length);
    case TERP_MESSAGE_GET_TIME:
        return handle_get_time(device, request, work_buffer, response_buffer,
                               response_capacity, response_length);
    case TERP_MESSAGE_SET_TIME:
        return handle_set_time(device, request, work_buffer, response_buffer,
                               response_capacity, response_length);
    case TERP_MESSAGE_LIST_EVENTS:
        return handle_list_events(device, request, work_buffer, work_capacity,
                                  response_buffer, response_capacity,
                                  response_length);
    case TERP_MESSAGE_GET_EVENT_INFO:
        return handle_get_event_info(device, request, work_buffer, response_buffer,
                                     response_capacity, response_length);
    case TERP_MESSAGE_READ_EVENT_CHUNK:
        return handle_read_event_chunk(device, request, work_buffer, work_capacity,
                                       response_buffer, response_capacity,
                                       response_length);
    case TERP_MESSAGE_DELETE_EVENT:
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    case TERP_MESSAGE_START_LIVE:
        return handle_live(device, request, 1, work_buffer, response_buffer,
                           response_capacity, response_length);
    case TERP_MESSAGE_STOP_LIVE:
        return handle_live(device, request, 0, work_buffer, response_buffer,
                           response_capacity, response_length);
    case TERP_MESSAGE_GET_AI_RESULT:
        return handle_get_ai_result(device, request, work_buffer, work_capacity,
                                    response_buffer, response_capacity,
                                    response_length);
    case TERP_MESSAGE_GET_EVENT_EVIDENCE:
        return handle_get_event_evidence(device, request, work_buffer,
                                         response_buffer, response_capacity,
                                         response_length);
    case TERP_MESSAGE_GET_CRASH_RECORD:
        return handle_get_crash_record(device, request, work_buffer, work_capacity,
                                       response_buffer, response_capacity,
                                       response_length);
    case TERP_MESSAGE_ACK_CRASH_RECORD:
        return handle_ack_crash_record(device, request, work_buffer,
                                       response_buffer, response_capacity,
                                       response_length);
    case TERP_MESSAGE_OTA_BEGIN:
    case TERP_MESSAGE_OTA_WRITE_CHUNK:
    case TERP_MESSAGE_OTA_QUERY:
    case TERP_MESSAGE_OTA_FINALIZE:
    case TERP_MESSAGE_OTA_CANCEL:
    case TERP_MESSAGE_OTA_RECOVERY_BEGIN:
    case TERP_MESSAGE_OTA_RECOVERY_WRITE_CHUNK:
    case TERP_MESSAGE_OTA_RECOVERY_QUERY:
    case TERP_MESSAGE_OTA_RECOVERY_FINALIZE:
    case TERP_MESSAGE_OTA_RECOVERY_CANCEL:
        return handle_ota(device, request, work_buffer, response_buffer,
                          response_capacity, response_length);
    case TERP_MESSAGE_MODEL_OTA_BEGIN:
    case TERP_MESSAGE_MODEL_OTA_WRITE_CHUNK:
    case TERP_MESSAGE_MODEL_OTA_QUERY:
    case TERP_MESSAGE_MODEL_OTA_FINALIZE:
    case TERP_MESSAGE_MODEL_OTA_CANCEL:
        return handle_model_ota(device, request, work_buffer, response_buffer,
                                response_capacity, response_length);
    default:
        return encode_error(request, TERP_ERROR_UNSUPPORTED, work_buffer,
                            response_buffer, response_capacity, response_length);
    }
}
