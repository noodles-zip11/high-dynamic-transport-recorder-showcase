#include <stdio.h>
#include <string.h>

#include "imu_acquisition_stats.h"

int main(void)
{
    char report[384];
    const imu_acquisition_stats_t stats = {
        .service_count = 100U,
        .sample_count = 1600U,
        .max_fifo_depth = 512U,
        .empty_fifo_count = 3U,
        .fifo_count_error_count = 1U,
        .fifo_capacity_error_count = 2U,
        .fifo_read_error_count = 4U,
        .fifo_parse_error_count = 5U,
        .dma_start_error_count = 6U,
        .dma_timeout_count = 7U,
        .dma_completion_error_count = 8U,
        .pool_min_free_count = 190U,
        .pool_backpressure_count = 9U,
        .batch_publish_error_count = 10U,
        .published_block_count = 11U,
    };

    if (imu_acquisition_stats_format(report, sizeof(report), &stats) <= 0
        || strstr(report, "samples=1600") == NULL
        || strstr(report, "fifo_max_bytes=512") == NULL
        || strstr(report, "dma_timeout=7") == NULL
        || strstr(report, "pool_min_free=190") == NULL
        || strstr(report, "pool_backpressure=9") == NULL
        || strstr(report, "batch_publish_err=10") == NULL
        || strstr(report, "blocks=11") == NULL)
    {
        fputs("imu acquisition stats: FAIL\n", stderr);
        return 1;
    }

    puts("imu acquisition stats: PASS");
    return 0;
}
