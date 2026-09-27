#include <stdio.h>

#include "system_report_format.h"


int system_report_format(char *buffer,
                         size_t buffer_size,
                         const system_report_info_t *info)
{
    return snprintf(
        buffer,
        buffer_size,
        "firmware_version=%s\n"
        "git_revision_or_local=%s\n"
        "build_time=%s\n"
        "sysclk_hz=%lu\n"
        "rt_tick_hz=%lu\n"
        "reset_reason=0x%08lx\n"
        "heap_free_bytes=%lu\n"
        "thread_count=%lu\n",
        info->firmware_version,
        info->git_revision_or_local,
        info->build_time,
        (unsigned long)info->sysclk_hz,
        (unsigned long)info->rt_tick_hz,
        (unsigned long)info->reset_reason,
        (unsigned long)info->heap_free_bytes,
        (unsigned long)info->thread_count);
}
