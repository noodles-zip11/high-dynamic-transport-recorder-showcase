#include <stdio.h>
#include <string.h>

#include "system_report_format.h"


int main(void)
{
    char report[512];
    const system_report_info_t info = {
        .firmware_version = "phase03-bsp",
        .git_revision_or_local = "local",
        .build_time = "test-build",
        .sysclk_hz = 240000000U,
        .rt_tick_hz = 1000U,
        .reset_reason = 0x1234U,
        .heap_free_bytes = 4096U,
        .thread_count = 3U,
    };

    if (system_report_format(report, sizeof(report), &info) <= 0 ||
        strstr(report, "firmware_version=phase03-bsp") == NULL ||
        strstr(report, "sysclk_hz=240000000") == NULL ||
        strstr(report, "thread_count=3") == NULL) {
        fputs("system report format: FAIL\n", stderr);
        return 1;
    }

    puts("system report format: PASS");
    return 0;
}
