#include "imu_acquisition.h"

#include "dma_buffer.h"
#include "icm45686.h"
#include "icm45686_fifo.h"
#include "imu_dma_power_guard.h"
#include "imu_acquisition_service.h"
#include "imu_sample_batcher.h"
#include "imu_spi.h"
#include "power_runtime.h"
#include "sample_block_pool.h"
#include "time/monotonic_clock.h"

#define IMU_ACQUISITION_THREAD_STACK_SIZE 2048U
#define IMU_ACQUISITION_THREAD_PRIORITY 8U
#define IMU_ACQUISITION_THREAD_TICK 10U
#define IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES 1024U
#define IMU_ACQUISITION_MAX_DRAIN_CHUNKS_PER_WAKE 8U
#define IMU_ACQUISITION_SAMPLE_CAPACITY \
    (IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES \
     / ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE)
#define IMU_ACQUISITION_DMA_TIMEOUT_TICKS \
    ((RT_TICK_PER_SECOND >= 10U) ? (RT_TICK_PER_SECOND / 10U) : 1U)
#define IMU_ACQUISITION_SAMPLE_PERIOD_NS 625000U

_Static_assert(IMU_ACQUISITION_SAMPLE_CAPACITY
               <= SAMPLE_BLOCK_SAMPLE_CAPACITY,
               "sample block must hold one FIFO batch");
_Static_assert(IMU_ACQUISITION_SAMPLE_CAPACITY
               <= IMU_SAMPLE_BATCHER_MAX_INPUT_SAMPLES,
               "batcher must accept one FIFO chunk");

static struct rt_semaphore fifo_watermark_sem;
static struct rt_semaphore dma_done_sem;
static struct rt_semaphore sample_ready_sem;

static volatile rt_err_t imu_dma_result;
static uint32_t imu_dma_start_error_count;
static uint32_t imu_dma_timeout_count;
static uint32_t imu_dma_completion_error_count;
static rt_bool_t imu_acquisition_started;
static icm45686_t imu_device;
static imu_acquisition_t imu_service;
static sample_block_pool_t imu_sample_pool;
static imu_sample_batcher_t imu_sample_batcher;
static uint32_t imu_next_sample_sequence;
static uint32_t imu_batch_publish_error_count;
static uint32_t imu_published_block_count;

static uint8_t imu_dma_tx[IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES + 1U]
    TRANSPORT_DMA_BUFFER;
static uint8_t imu_dma_rx[IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES + 1U]
    TRANSPORT_DMA_BUFFER;
static uint8_t imu_raw_fifo_buffer[IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES];
static icm45686_fifo_sample_t
    imu_samples[IMU_ACQUISITION_SAMPLE_CAPACITY];

static rt_err_t imu_emit_sample_batch(
    const icm45686_fifo_sample_t *samples,
    uint16_t sample_count,
    uint32_t first_sequence,
    uint64_t first_monotonic_us,
    uint32_t sample_period_ns,
    void *context)
{
    sample_block_t *block;

    (void)context;
    if (samples == RT_NULL || sample_count != IMU_SAMPLE_BATCH_SIZE
        || sample_count > SAMPLE_BLOCK_SAMPLE_CAPACITY
        || sample_period_ns == 0U)
    {
        return -RT_ERROR;
    }

    block = sample_block_pool_acquire(&imu_sample_pool);
    if (block == RT_NULL)
    {
        return -RT_ERROR;
    }

    block->sequence = first_sequence;
    block->first_monotonic_us = first_monotonic_us;
    block->sample_period_ns = sample_period_ns;
    block->sample_count = sample_count;
    rt_memcpy(block->samples,
              samples,
              sample_count * sizeof(samples[0]));

    if (sample_block_pool_publish(&imu_sample_pool, block) == RT_EOK)
    {
        imu_published_block_count++;
        rt_sem_release(&sample_ready_sem);
        return RT_EOK;
    }

    (void)sample_block_pool_abandon(&imu_sample_pool, block);
    return -RT_ERROR;
}

static void imu_publish_samples(const imu_acquisition_result_t *result)
{
    uint32_t first_sample_sequence;
    rt_err_t publish_result;

    if (result == RT_NULL || result->samples_produced == 0U
        || result->samples_produced > IMU_SAMPLE_BATCHER_MAX_INPUT_SAMPLES)
    {
        return;
    }

    first_sample_sequence = imu_next_sample_sequence;
    imu_next_sample_sequence += (uint32_t)result->samples_produced;
    publish_result = imu_sample_batcher_push(
        &imu_sample_batcher,
        imu_samples,
        (uint16_t)result->samples_produced,
        first_sample_sequence,
        monotonic_clock_now_us(),
        IMU_ACQUISITION_SAMPLE_PERIOD_NS,
        imu_emit_sample_batch,
        RT_NULL);
    if (publish_result != RT_EOK
        && imu_batch_publish_error_count != UINT32_MAX)
    {
        /* Complete failed batches are intentionally dropped by the batcher. */
        imu_batch_publish_error_count++;
    }
}

static void imu_fifo_watermark_callback(void *context)
{
    (void)context;

    power_runtime_record_wake(POWER_WAKE_IMU_INT1);
    rt_sem_release(&fifo_watermark_sem);
}

static void imu_dma_callback(void *context, rt_err_t result)
{
    (void)context;

    imu_dma_result = result;
    power_runtime_record_wake(POWER_WAKE_DMA);
    rt_sem_release(&dma_done_sem);
}

static void imu_drain_dma_done_sem(void)
{
    while (rt_sem_take(&dma_done_sem, 0) == RT_EOK)
    {
    }
}

static rt_err_t imu_read_fifo_with_dma(uint8_t *fifo_data,
                                       rt_size_t fifo_data_length)
{
    rt_err_t result;
    imu_dma_power_guard_t dma_power_guard = {0};

    if (fifo_data == RT_NULL || fifo_data_length == 0U
        || fifo_data_length > IMU_ACQUISITION_DMA_FIFO_CAPACITY_BYTES)
    {
        return -RT_ERROR;
    }

    (void)imu_dma_power_guard_acquire(&dma_power_guard);

    imu_drain_dma_done_sem();
    imu_dma_result = -RT_ERROR;

    result = icm45686_read_fifo_dma(&imu_device,
                                    board_imu_spi_transfer_dma,
                                    imu_dma_tx,
                                    imu_dma_rx,
                                    fifo_data_length);
    if (result != RT_EOK)
    {
        imu_dma_start_error_count++;
        goto cleanup;
    }

    result = rt_sem_take(&dma_done_sem,
                         IMU_ACQUISITION_DMA_TIMEOUT_TICKS);
    if (result != RT_EOK)
    {
        imu_dma_timeout_count++;

        if (board_imu_spi_abort_dma() != RT_EOK)
        {
            result = -RT_ERROR;
        }
        goto cleanup;
    }

    if (imu_dma_result != RT_EOK)
    {
        imu_dma_completion_error_count++;
        result = imu_dma_result;
        goto cleanup;
    }

    rt_memcpy(fifo_data, &imu_dma_rx[1], fifo_data_length);
    result = RT_EOK;

cleanup:
    imu_dma_power_guard_release(&dma_power_guard);
    return result;
}

static void imu_acquisition_entry(void *parameter)
{
    imu_acquisition_result_t result;
    rt_bool_t backlog_possible;
    unsigned int chunk_index;

    (void)parameter;

    while (RT_TRUE)
    {
        rt_sem_take(&fifo_watermark_sem, RT_WAITING_FOREVER);

        backlog_possible = RT_FALSE;
        for (chunk_index = 0U;
             chunk_index < IMU_ACQUISITION_MAX_DRAIN_CHUNKS_PER_WAKE;
             ++chunk_index)
        {
            if (imu_acquisition_service(&imu_service,
                                        imu_raw_fifo_buffer,
                                        sizeof(imu_raw_fifo_buffer),
                                        imu_samples,
                                        IMU_ACQUISITION_SAMPLE_CAPACITY,
                                        &result) != RT_EOK)
            {
                break;
            }
            imu_publish_samples(&result);
            backlog_possible =
                result.fifo_bytes == sizeof(imu_raw_fifo_buffer);
            if (!backlog_possible)
            {
                break;
            }
        }

        if (chunk_index == IMU_ACQUISITION_MAX_DRAIN_CHUNKS_PER_WAKE
            && backlog_possible)
        {
            rt_sem_release(&fifo_watermark_sem);
        }
    }
}

void imu_acquisition_get_stats(imu_acquisition_stats_t *stats)
{
    rt_base_t level;

    if (stats == RT_NULL)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    stats->service_count = imu_service.service_count;
    stats->sample_count = imu_service.sample_count;
    stats->max_fifo_depth = imu_service.max_fifo_depth;
    stats->empty_fifo_count = imu_service.empty_fifo_count;
    stats->fifo_count_error_count = imu_service.fifo_count_error_count;
    stats->fifo_capacity_error_count = imu_service.fifo_capacity_error_count;
    stats->fifo_read_error_count = imu_service.fifo_read_error_count;
    stats->fifo_parse_error_count = imu_service.fifo_parse_error_count;
    stats->dma_start_error_count = imu_dma_start_error_count;
    stats->dma_timeout_count = imu_dma_timeout_count;
    stats->dma_completion_error_count = imu_dma_completion_error_count;
    stats->pool_min_free_count = imu_sample_pool.min_free_count;
    stats->pool_backpressure_count = imu_sample_pool.backpressure_count;
    stats->batch_publish_error_count = imu_batch_publish_error_count;
    stats->published_block_count = imu_published_block_count;
    rt_hw_interrupt_enable(level);
}

sample_block_t *imu_acquisition_take_ready_block(rt_int32_t timeout)
{
    if (!imu_acquisition_started
        || rt_sem_take(&sample_ready_sem, timeout) != RT_EOK)
    {
        return RT_NULL;
    }

    return sample_block_pool_take_ready(&imu_sample_pool);
}

rt_err_t imu_acquisition_release_block(sample_block_t *block)
{
    return sample_block_pool_release(&imu_sample_pool, block);
}

sample_block_pool_t *imu_acquisition_sample_pool(void)
{
    return &imu_sample_pool;
}

rt_err_t imu_acquisition_start(void)
{
    rt_err_t result;
    rt_thread_t thread;
    const icm45686_bus_t imu_bus = {
        .transfer = board_imu_spi_transfer,
    };

    if (imu_acquisition_started)
    {
        return RT_EOK;
    }

    result = rt_sem_init(&fifo_watermark_sem,
                         "imu_fifo",
                         0,
                         RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_sem_init(&dma_done_sem,
                         "imu_dma",
                         0,
                         RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_sem_init(&sample_ready_sem,
                         "imu_blk",
                         0,
                         RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        return result;
    }

    result = board_imu_spi_init();
    if (result != RT_EOK)
    {
        return result;
    }

    result = board_imu_spi_dma_init();
    if (result != RT_EOK)
    {
        return result;
    }

    result = board_imu_spi_set_dma_handler(imu_dma_callback, RT_NULL);
    if (result != RT_EOK)
    {
        return result;
    }

    result = board_imu_int1_set_handler(imu_fifo_watermark_callback, RT_NULL);
    if (result != RT_EOK)
    {
        return result;
    }

    result = board_imu_int1_init();
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_init(&imu_device, &imu_bus);
    if (result != RT_EOK)
    {
        return result;
    }

    result = icm45686_configure_acquisition(&imu_device);
    if (result != RT_EOK)
    {
        return result;
    }

    result = imu_acquisition_init(&imu_service,
                                  &imu_device,
                                  imu_read_fifo_with_dma);
    if (result != RT_EOK)
    {
        return result;
    }

    sample_block_pool_init(&imu_sample_pool);
    imu_sample_batcher_init(&imu_sample_batcher);

    thread = rt_thread_create("imu_acq",
                              imu_acquisition_entry,
                              RT_NULL,
                              IMU_ACQUISITION_THREAD_STACK_SIZE,
                              IMU_ACQUISITION_THREAD_PRIORITY,
                              IMU_ACQUISITION_THREAD_TICK);
    if (thread == RT_NULL)
    {
        return -RT_ERROR;
    }

    imu_acquisition_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}
