#ifndef TRANSPORT_RECORDER_STORAGE_FORMAT_CONFIRMATION_H
#define TRANSPORT_RECORDER_STORAGE_FORMAT_CONFIRMATION_H

#include <rtthread.h>

#define STORAGE_FORMAT_CONFIRM_WINDOW_TICKS (10U * RT_TICK_PER_SECOND)

typedef struct
{
    rt_tick_t requested_at;
    rt_bool_t pending;
} storage_format_confirmation_t;

void storage_format_confirmation_init(storage_format_confirmation_t *confirmation);
void storage_format_confirmation_request(storage_format_confirmation_t *confirmation,
                                         rt_tick_t now);
rt_bool_t storage_format_confirmation_confirm(storage_format_confirmation_t *confirmation,
                                              rt_tick_t now);

#endif
