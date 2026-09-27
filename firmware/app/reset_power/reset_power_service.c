#include "reset_power_service.h"

#include <string.h>

reset_power_cause_t reset_power_normalize(uint32_t raw_flags)
{
    if ((raw_flags & (RESET_POWER_RAW_BROWN_OUT
                      | RESET_POWER_RAW_POWER_ON)) != 0U)
    {
        return RESET_POWER_CAUSE_BOR_OR_POR;
    }

    if ((raw_flags & RESET_POWER_RAW_IWDG) != 0U)
    {
        return RESET_POWER_CAUSE_IWDG;
    }

    if ((raw_flags & RESET_POWER_RAW_WWDG) != 0U)
    {
        return RESET_POWER_CAUSE_WWDG;
    }

    if ((raw_flags & RESET_POWER_RAW_SOFTWARE) != 0U)
    {
        return RESET_POWER_CAUSE_SOFTWARE;
    }

    if ((raw_flags & RESET_POWER_RAW_PIN) != 0U)
    {
        return RESET_POWER_CAUSE_PIN;
    }

    return RESET_POWER_CAUSE_UNKNOWN;
}

int reset_power_service_init(reset_power_service_t *service,
                             const reset_power_ops_t *ops)
{
    if (service == NULL || ops == NULL || ops->read_raw_flags == NULL
        || ops->clear_raw_flags == NULL)
    {
        return -1;
    }

    memset(service, 0, sizeof(*service));
    service->ops = *ops;
    service->initialized = true;
    return 0;
}

int reset_power_service_capture(reset_power_service_t *service)
{
    uint32_t raw_flags;

    if (service == NULL || !service->initialized)
    {
        return -1;
    }

    if (service->ops.read_raw_flags(&raw_flags, service->ops.context) != 0)
    {
        return -1;
    }

    if (service->ops.clear_raw_flags(service->ops.context) != 0)
    {
        return -1;
    }

    service->status.captured = true;
    service->status.raw_flags = raw_flags;
    service->status.cause = reset_power_normalize(raw_flags);
    return 0;
}

int reset_power_service_get_status(const reset_power_service_t *service,
                                   reset_power_status_t *status)
{
    if (service == NULL || status == NULL || !service->initialized)
    {
        return -1;
    }

    *status = service->status;
    return 0;
}
