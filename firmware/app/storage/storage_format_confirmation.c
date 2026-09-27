#include "storage_format_confirmation.h"

void storage_format_confirmation_init(storage_format_confirmation_t *confirmation)
{
    if (confirmation != RT_NULL)
    {
        confirmation->requested_at = 0U;
        confirmation->pending = RT_FALSE;
    }
}

void storage_format_confirmation_request(storage_format_confirmation_t *confirmation,
                                         rt_tick_t now)
{
    if (confirmation != RT_NULL)
    {
        confirmation->requested_at = now;
        confirmation->pending = RT_TRUE;
    }
}

rt_bool_t storage_format_confirmation_confirm(storage_format_confirmation_t *confirmation,
                                              rt_tick_t now)
{
    if (confirmation == RT_NULL || !confirmation->pending
        || now - confirmation->requested_at >= STORAGE_FORMAT_CONFIRM_WINDOW_TICKS)
    {
        if (confirmation != RT_NULL)
        {
            confirmation->pending = RT_FALSE;
        }
        return RT_FALSE;
    }

    confirmation->pending = RT_FALSE;
    return RT_TRUE;
}
