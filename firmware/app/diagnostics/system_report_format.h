#ifndef TRANSPORT_RECORDER_SYSTEM_REPORT_FORMAT_H
#define TRANSPORT_RECORDER_SYSTEM_REPORT_FORMAT_H

#include <stddef.h>
#include <stdint.h>


typedef struct
{
    const char *firmware_version;
    const char *git_revision_or_local;
    const char *build_time;
    uint32_t sysclk_hz;
    uint32_t rt_tick_hz;
    uint32_t reset_reason;
    uint32_t heap_free_bytes;
    uint32_t thread_count;
} system_report_info_t;

int system_report_format(char *buffer,
                         size_t buffer_size,
                         const system_report_info_t *info);

#endif
