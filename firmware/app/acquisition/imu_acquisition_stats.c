#include "imu_acquisition_stats.h"

#include <stdio.h>

int imu_acquisition_stats_format(char *buffer,
                                 size_t buffer_size,
                                 const imu_acquisition_stats_t *stats)
{
    if (buffer == NULL || buffer_size == 0U || stats == NULL)
    {
        return -1;
    }

    return snprintf(
        buffer,
        buffer_size,
        "imu services=%lu samples=%lu fifo_max_bytes=%u fifo_empty=%lu "
        "fifo_count_err=%lu fifo_capacity_err=%lu fifo_read_err=%lu "
        "fifo_parse_err=%lu dma_start_err=%lu dma_timeout=%lu "
        "dma_complete_err=%lu pool_min_free=%u pool_backpressure=%lu "
        "batch_publish_err=%lu blocks=%lu "
        "\n",
        (unsigned long)stats->service_count,
        (unsigned long)stats->sample_count,
        (unsigned int)stats->max_fifo_depth,
        (unsigned long)stats->empty_fifo_count,
        (unsigned long)stats->fifo_count_error_count,
        (unsigned long)stats->fifo_capacity_error_count,
        (unsigned long)stats->fifo_read_error_count,
        (unsigned long)stats->fifo_parse_error_count,
        (unsigned long)stats->dma_start_error_count,
        (unsigned long)stats->dma_timeout_count,
        (unsigned long)stats->dma_completion_error_count,
        (unsigned int)stats->pool_min_free_count,
        (unsigned long)stats->pool_backpressure_count,
        (unsigned long)stats->batch_publish_error_count,
        (unsigned long)stats->published_block_count);
}
