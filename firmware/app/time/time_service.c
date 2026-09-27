#include "time_service.h"

#include <string.h>

static bool time_service_ops_are_valid(const time_service_ops_t *ops)
{
    return ops != NULL
        && ops->rtc_get_utc != NULL
        && ops->rtc_set_utc != NULL
        && ops->rtc_calendar_valid != NULL
        && ops->backup_read != NULL
        && ops->backup_write != NULL
        && ops->monotonic_us != NULL;
}

int time_service_init(time_service_t *service, const time_service_ops_t *ops)
{
    bool calendar_valid;
    uint32_t backup_marker;

    if (service == NULL || !time_service_ops_are_valid(ops))
    {
        return -1;
    }

    memset(service, 0, sizeof(*service));
    service->ops = *ops;

    if (service->ops.rtc_calendar_valid(&calendar_valid,
                                        service->ops.context) != 0
        || service->ops.backup_read(&backup_marker,
                                    service->ops.context) != 0)
    {
        return -1;
    }

    service->rtc_calendar_valid = calendar_valid;
    service->utc_valid = calendar_valid
        && backup_marker == TIME_SERVICE_BACKUP_MARKER;
    service->initialized = true;
    return 0;
}

int time_service_get_status(time_service_t *service,
                            time_service_status_t *status)
{
    int64_t utc_unix_seconds = 0;

    if (service == NULL || status == NULL || !service->initialized)
    {
        return -1;
    }

    if (service->utc_valid
        && service->ops.rtc_get_utc(&utc_unix_seconds,
                                     service->ops.context) != 0)
    {
        return -1;
    }

    status->utc_valid = service->utc_valid;
    status->rtc_calendar_valid = service->rtc_calendar_valid;
    status->utc_unix_seconds = service->utc_valid ? utc_unix_seconds : 0;
    status->epoch_id = service->epoch_id;
    status->monotonic_us = service->ops.monotonic_us(service->ops.context);
    return 0;
}

int time_service_get_utc(time_service_t *service, int64_t *utc_unix_seconds)
{
    time_service_status_t status;

    if (utc_unix_seconds == NULL)
    {
        return -1;
    }

    if (time_service_get_status(service, &status) != 0 || !status.utc_valid)
    {
        return -1;
    }

    *utc_unix_seconds = status.utc_unix_seconds;
    return 0;
}

int time_service_set_utc(time_service_t *service, int64_t utc_unix_seconds)
{
    if (service == NULL || !service->initialized)
    {
        return -1;
    }

    if (service->ops.rtc_set_utc(utc_unix_seconds, service->ops.context) != 0)
    {
        return -1;
    }

    if (service->ops.backup_write(TIME_SERVICE_BACKUP_MARKER,
                                  service->ops.context) != 0)
    {
        service->utc_valid = false;
        service->rtc_calendar_valid = true;
        return -1;
    }

    service->utc_valid = true;
    service->rtc_calendar_valid = true;
    service->epoch_id++;
    return 0;
}

int time_service_invalidate(time_service_t *service)
{
    if (service == NULL || !service->initialized)
    {
        return -1;
    }

    if (service->ops.backup_write(TIME_SERVICE_BACKUP_INVALID_MARKER,
                                  service->ops.context) != 0)
    {
        return -1;
    }

    service->utc_valid = false;
    return 0;
}
