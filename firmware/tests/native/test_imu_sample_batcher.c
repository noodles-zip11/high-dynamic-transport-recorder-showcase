#include <stdio.h>
#include <string.h>

#include "imu_sample_batcher.h"

typedef struct
{
    uint16_t sample_count[4];
    uint32_t first_sequence[4];
    uint64_t first_monotonic_us[4];
    uint32_t sample_period_ns[4];
    icm45686_fifo_sample_t samples[4][IMU_SAMPLE_BATCH_SIZE];
    unsigned int call_count;
    unsigned int fail_emit_count;
} capture_t;

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "imu_sample_batcher: %s\n", message);
        return 0;
    }

    return 1;
}

static rt_err_t capture_emit(const icm45686_fifo_sample_t *samples,
                             uint16_t sample_count,
                             uint32_t first_sequence,
                             uint64_t first_monotonic_us,
                             uint32_t sample_period_ns,
                             void *context)
{
    capture_t *capture = context;

    if (capture == RT_NULL || samples == RT_NULL
        || capture->call_count >= 4U
        || sample_count != IMU_SAMPLE_BATCH_SIZE)
    {
        return -RT_ERROR;
    }
    if (capture->fail_emit_count != 0U)
    {
        capture->fail_emit_count--;
        return -RT_ERROR;
    }

    capture->sample_count[capture->call_count] = sample_count;
    capture->first_sequence[capture->call_count] = first_sequence;
    capture->first_monotonic_us[capture->call_count] = first_monotonic_us;
    capture->sample_period_ns[capture->call_count] = sample_period_ns;
    memcpy(capture->samples[capture->call_count], samples,
           sizeof(capture->samples[capture->call_count]));
    capture->call_count++;
    return RT_EOK;
}

static void fill_samples(icm45686_fifo_sample_t *samples,
                         uint16_t sample_count,
                         int16_t first_value)
{
    uint16_t index;

    memset(samples, 0, sample_count * sizeof(samples[0]));
    for (index = 0U; index < sample_count; index++)
    {
        samples[index].accel[0] = (int16_t)(first_value + index);
    }
}

static int test_64_samples_emit_two_full_batches(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t samples[64];

    fill_samples(samples, 64U, 0);
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, samples, 64U, 100U,
                                        UINT64_C(1000000), 625000U,
                                        capture_emit, &capture) == RT_EOK,
                    "64 samples must be accepted")
        || !expect(capture.call_count == 2U
                       && batcher.sample_count == 0U,
                   "64 samples must emit two complete batches")
        || !expect(capture.first_sequence[0] == 100U
                       && capture.first_sequence[1] == 132U,
                   "batch sequences must advance by 32 samples")
        || !expect(capture.first_monotonic_us[0] == UINT64_C(1000000)
                       && capture.first_monotonic_us[1] == UINT64_C(1020000),
                   "batch timestamps must advance by 32 sample periods")
        || !expect(capture.samples[0][0].accel[0] == 0
                       && capture.samples[0][31].accel[0] == 31
                       && capture.samples[1][0].accel[0] == 32
                       && capture.samples[1][31].accel[0] == 63,
                   "64 samples must remain ordered without loss"))
    {
        return 1;
    }

    return 0;
}

static int test_one_plus_31_samples_emit_from_original_start(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t first_sample[1];
    icm45686_fifo_sample_t remaining_samples[31];

    fill_samples(first_sample, 1U, 0);
    fill_samples(remaining_samples, 31U, 1);
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, first_sample, 1U, 200U,
                                        UINT64_C(2000000), 625000U,
                                        capture_emit, &capture) == RT_EOK,
                    "the first sample must be retained")
        || !expect(capture.call_count == 0U && batcher.sample_count == 1U,
                   "one sample must remain pending until a full batch exists")
        || !expect(imu_sample_batcher_push(&batcher, remaining_samples, 31U,
                                           201U, UINT64_C(2000625), 625000U,
                                           capture_emit, &capture) == RT_EOK,
                    "the remaining 31 samples must complete the batch")
        || !expect(capture.call_count == 1U && batcher.sample_count == 0U,
                   "one plus 31 samples must emit one complete batch")
        || !expect(capture.first_sequence[0] == 200U
                       && capture.first_monotonic_us[0] == UINT64_C(2000000)
                       && capture.samples[0][0].accel[0] == 0
                       && capture.samples[0][31].accel[0] == 31,
                   "carry must preserve the first sample metadata and order"))
    {
        return 1;
    }

    return 0;
}

static int test_mixed_inputs_preserve_all_samples(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t first[1];
    icm45686_fifo_sample_t second[64];
    icm45686_fifo_sample_t third[31];

    fill_samples(first, 1U, 0);
    fill_samples(second, 64U, 1);
    fill_samples(third, 31U, 65);
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, first, 1U, 300U,
                                        UINT64_C(3000000), 625000U,
                                        capture_emit, &capture) == RT_EOK,
                    "mixed input first sample must be accepted")
        || !expect(imu_sample_batcher_push(&batcher, second, 64U, 301U,
                                           UINT64_C(3000625), 625000U,
                                           capture_emit, &capture) == RT_EOK,
                    "mixed input 64-sample chunk must be accepted")
        || !expect(imu_sample_batcher_push(&batcher, third, 31U, 365U,
                                           UINT64_C(3040625), 625000U,
                                           capture_emit, &capture) == RT_EOK,
                    "mixed input tail must be accepted")
        || !expect(capture.call_count == 3U && batcher.sample_count == 0U,
                   "mixed inputs must emit three complete batches")
        || !expect(capture.first_sequence[0] == 300U
                       && capture.first_sequence[1] == 332U
                       && capture.first_sequence[2] == 364U,
                   "mixed input sequences must remain continuous")
        || !expect(capture.samples[0][0].accel[0] == 0
                       && capture.samples[0][31].accel[0] == 31
                       && capture.samples[1][0].accel[0] == 32
                       && capture.samples[1][31].accel[0] == 63
                       && capture.samples[2][0].accel[0] == 64
                       && capture.samples[2][31].accel[0] == 95,
                   "mixed inputs must not reorder or drop samples"))
    {
        return 1;
    }

    return 0;
}

static int test_oversized_input_is_rejected(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t samples[65];

    fill_samples(samples, 65U, 0);
    imu_sample_batcher_init(&batcher);
    return !expect(imu_sample_batcher_push(&batcher, samples, 65U, 0U, 0U,
                                           625000U, capture_emit, &capture)
                       == -RT_ERROR,
                   "input above one FIFO chunk must be rejected");
}

static int test_failed_batches_are_dropped_and_next_input_recovers(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t failed_samples[64];
    icm45686_fifo_sample_t next_samples[64];

    fill_samples(failed_samples, 64U, 0);
    fill_samples(next_samples, 64U, 1000);
    capture.fail_emit_count = 2U;
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, failed_samples, 64U, 400U,
                                        UINT64_C(4000000), 625000U,
                                        capture_emit, &capture) == -RT_ERROR,
                    "a failed 64-sample publish must be returned to acquisition")
        || !expect(capture.call_count == 0U
                       && batcher.sample_count == 0U,
                   "failed complete batches must not remain pending")
        || !expect(imu_sample_batcher_push(&batcher, next_samples, 64U, 464U,
                                           UINT64_C(4040000), 625000U,
                                           capture_emit, &capture) == RT_EOK,
                   "a later 64-sample input must recover after publish failure")
        || !expect(capture.call_count == 2U && batcher.sample_count == 0U,
                   "later input must emit only its two complete batches")
        || !expect(capture.first_sequence[0] == 464U
                       && capture.first_sequence[1] == 496U
                       && capture.samples[0][0].accel[0] == 1000
                       && capture.samples[0][31].accel[0] == 1031
                       && capture.samples[1][0].accel[0] == 1032
                       && capture.samples[1][31].accel[0] == 1063,
                   "later input must not resend samples from failed batches"))
    {
        return 1;
    }

    return 0;
}

static int test_one_plus_31_failure_then_next_32_recovers(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t first_sample[1];
    icm45686_fifo_sample_t remaining_samples[31];
    icm45686_fifo_sample_t next_samples[IMU_SAMPLE_BATCH_SIZE];

    fill_samples(first_sample, 1U, 0);
    fill_samples(remaining_samples, 31U, 1);
    fill_samples(next_samples, IMU_SAMPLE_BATCH_SIZE, 100);
    capture.fail_emit_count = 1U;
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, first_sample, 1U, 700U,
                                        UINT64_C(7000000), 625000U,
                                        capture_emit, &capture) == RT_EOK,
                    "the first sample must remain pending")
        || !expect(imu_sample_batcher_push(&batcher, remaining_samples, 31U,
                                           701U, UINT64_C(7000625), 625000U,
                                           capture_emit, &capture) == -RT_ERROR,
                    "a failed 1+31 batch must report the publish error")
        || !expect(capture.call_count == 0U && batcher.sample_count == 0U,
                   "a failed 1+31 batch must leave no full carry")
        || !expect(imu_sample_batcher_push(&batcher, next_samples,
                                           IMU_SAMPLE_BATCH_SIZE, 732U,
                                           UINT64_C(7020000), 625000U,
                                           capture_emit, &capture) == RT_EOK,
                    "the next complete batch must recover")
        || !expect(capture.call_count == 1U && batcher.sample_count == 0U
                       && capture.first_sequence[0] == 732U
                       && capture.samples[0][0].accel[0] == 100
                       && capture.samples[0][31].accel[0] == 131,
                   "recovery must publish only the next input sequence"))
    {
        return 1;
    }

    return 0;
}

static int test_sequence_wrap_and_timestamp_saturation(void)
{
    imu_sample_batcher_t batcher;
    capture_t capture = {0};
    icm45686_fifo_sample_t samples[64];
    const uint32_t first_sequence = UINT32_MAX - UINT32_C(16);
    const uint64_t first_time = UINT64_MAX - UINT64_C(10000);

    fill_samples(samples, 64U, 0);
    imu_sample_batcher_init(&batcher);
    if (!expect(imu_sample_batcher_push(&batcher, samples, 64U,
                                        first_sequence, first_time, 625000U,
                                        capture_emit, &capture) == RT_EOK,
                    "a wrapped sequence must be accepted")
        || !expect(capture.call_count == 2U,
                   "a 64-sample input must still emit two wrapped batches")
        || !expect(capture.first_sequence[0] == first_sequence
                       && capture.first_sequence[1] == UINT32_C(15),
                   "batch sequence advancement must wrap naturally")
        || !expect(capture.first_monotonic_us[0] == first_time
                       && capture.first_monotonic_us[1] == UINT64_MAX,
                   "batch timestamp advancement must saturate at UINT64_MAX"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_64_samples_emit_two_full_batches() != 0
        || test_one_plus_31_samples_emit_from_original_start() != 0
        || test_mixed_inputs_preserve_all_samples() != 0
        || test_oversized_input_is_rejected() != 0
        || test_failed_batches_are_dropped_and_next_input_recovers() != 0
        || test_one_plus_31_failure_then_next_32_recovers() != 0
        || test_sequence_wrap_and_timestamp_saturation() != 0)
    {
        return 1;
    }

    puts("imu sample batcher: PASS");
    return 0;
}
