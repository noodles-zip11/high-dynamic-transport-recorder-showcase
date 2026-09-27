#include <rtdevice.h>
#include <rtthread.h>

#include "acquisition/imu_acquisition.h"
#include "acquisition/imu_acquisition_stats.h"
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
#include "crash_record_target.h"
#endif
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
#include "fault_injection.h"
#endif
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
#include "ai_model_data.h"
#include "ai_inference_service.h"
#include "ai_result_sidecar.h"
#endif
#include "board.h"
#include "build_identity.h"
#include "diagnostics/phase11_qspi_hil.h"
#include "diagnostics/system_report_format.h"
#include "diagnostics/uart3_echo_service.h"
#include "event/event_service.h"
#include "health/health_service.h"
#include "memory_layout.h"
#include "ota/ota_state_app_store.h"
#include "power_runtime.h"
#include "ota/ota_qspi_candidate.h"
#include "storage/storage_service.h"
#include "transport/terp_uart3.h"
#include "runtime/app_runtime.h"
#include "time/monotonic_clock.h"

#ifndef TRANSPORT_PHASE11_QSPI_HIL_CYCLES
#define TRANSPORT_PHASE11_QSPI_HIL_CYCLES 100U
#endif

#define TRANSPORT_FIRMWARE_VERSION "phase08-terp-uart3"
#define TRANSPORT_BUILD_TIME __DATE__ " " __TIME__
#define IMU_HEALTH_REPORT_INTERVAL_MS 1000U
#define SYSTEM_REPORT_INTERVAL_SECONDS 60U
#define OTA_TRIAL_CONFIRM_HEALTHY_SECONDS 30U

static health_service_t runtime_health_service;
static rt_bool_t runtime_event_started;
static rt_bool_t runtime_trial_confirmed;
static uint32_t runtime_trial_healthy_seconds;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static void runtime_crash_record_startup(void)
{
    if (crash_record_target_init() == CRASH_RECORD_TARGET_OK)
    {
        (void)crash_record_target_recover();
    }
}
#endif

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
static ai_result_sidecar_t runtime_ai_result_sidecar;
static const ai_result_sidecar_storage_t runtime_ai_result_storage = {
    .read = ota_qspi_ai_result_read,
    .write = ota_qspi_ai_result_write,
    .erase = ota_qspi_ai_result_erase,
    .context = RT_NULL,
    .capacity_bytes = TRANSPORT_OTA_QSPI_AI_RESULT_SIZE_BYTES,
    .erase_sector_bytes = TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES,
};

static void runtime_ai_result_sidecar_start(void)
{
    if (ai_result_sidecar_mount(&runtime_ai_result_sidecar,
                                &runtime_ai_result_storage) == 0
        && ai_inference_service_set_result_sidecar(&runtime_ai_result_sidecar)
           == RT_EOK)
    {
        rt_kprintf("AI result sidecar=ready\n");
    }
    else
    {
        rt_kprintf("AI result sidecar=fallback\n");
    }
}
#endif

static void runtime_health_evaluate(void)
{
    imu_acquisition_stats_t imu = {0};
    event_service_stats_t event = {0};
    event_log_status_t storage = {0};
    health_inputs_t inputs = {0};

    imu_acquisition_get_stats(&imu);
    event_service_get_stats(&event);
    inputs.acquisition.ready = imu.sample_count != 0U;
    inputs.acquisition.progress_sequence = imu.sample_count;
    inputs.acquisition.error_count = imu.fifo_read_error_count + imu.dma_timeout_count;
    inputs.sample_pool_min_free = imu.pool_min_free_count;
    inputs.sample_pool_backpressure_count = imu.pool_backpressure_count;
    inputs.imu_transport_error_count = imu.fifo_read_error_count
                                       + imu.fifo_parse_error_count;
    inputs.imu_dma_error_count = imu.dma_start_error_count
                                 + imu.dma_timeout_count
                                 + imu.dma_completion_error_count;
    inputs.event.ready = runtime_event_started;
    inputs.event.progress_sequence = event.processed_block_count;
    inputs.event.error_count = event.export_error_count;
    inputs.event_export_error_count = event.export_error_count;
    if (storage_service_get_status(&storage) == RT_EOK)
    {
        inputs.storage.ready = storage.state == EVENT_LOG_READY;
        inputs.storage.progress_sequence = storage.committed_event_count;
    }
    inputs.storage_progress_required = RT_FALSE;
    health_service_evaluate(&runtime_health_service, &inputs,
                            monotonic_clock_now_us());
}

static void runtime_confirm_healthy_trial(void)
{
    health_snapshot_t snapshot;

    if (runtime_trial_confirmed)
    {
        return;
    }
    health_service_get_snapshot(&runtime_health_service, &snapshot);
    if (snapshot.state != HEALTH_HEALTHY)
    {
        runtime_trial_healthy_seconds = 0U;
        return;
    }
    if (runtime_trial_healthy_seconds < OTA_TRIAL_CONFIRM_HEALTHY_SECONDS)
    {
        runtime_trial_healthy_seconds++;
    }
    if (runtime_trial_healthy_seconds == OTA_TRIAL_CONFIRM_HEALTHY_SECONDS)
    {
        rt_bool_t maintenance_entered = RT_FALSE;
        rt_err_t maintenance_result;
        int confirmation;

        maintenance_result =
            power_runtime_enter_maintenance_if_monitor(&maintenance_entered);
        if (maintenance_result != RT_EOK || !maintenance_entered)
        {
            rt_kprintf("OTA trial confirmation deferred; maintenance unavailable\n");
            return;
        }
        confirmation = ota_state_app_store_confirm_trial(0);
        (void)power_runtime_exit_maintenance_if_owned();

        if (confirmation >= 0)
        {
            runtime_trial_confirmed = RT_TRUE;
            if (confirmation == 0)
            {
                rt_kprintf("OTA trial confirmed after %u healthy seconds\n",
                           (unsigned int)OTA_TRIAL_CONFIRM_HEALTHY_SECONDS);
            }
        }
        else
        {
            rt_kprintf("OTA trial confirmation failed\n");
            runtime_trial_healthy_seconds = 0U;
        }
    }
}

static void print_system_report(void)
{
    char report[384];
    rt_size_t heap_total = 0;
    rt_size_t heap_used = 0;
    rt_size_t heap_max_used = 0;
    int thread_count;
    system_report_info_t info;

    rt_memory_info(&heap_total, &heap_used, &heap_max_used);
    thread_count = rt_object_get_length(RT_Object_Class_Thread);

    info.firmware_version = TRANSPORT_FIRMWARE_VERSION;
    info.git_revision_or_local = TRANSPORT_GIT_REVISION;
    info.build_time = TRANSPORT_BUILD_TIME;
    info.sysclk_hz = TRANSPORT_SYSCLK_HZ;
    info.rt_tick_hz = RT_TICK_PER_SECOND;
    info.reset_reason = RCC->RSR;
    info.heap_free_bytes = (uint32_t)(heap_total - heap_used);
    info.thread_count = (thread_count > 0) ? (uint32_t)thread_count : 0U;

    if (system_report_format(report, sizeof(report), &info) > 0)
    {
        rt_kprintf("%s", report);
    }
}

static void print_imu_health_report(void)
{
    char report[384];
    imu_acquisition_stats_t stats;
    event_service_stats_t event_stats;

    imu_acquisition_get_stats(&stats);
    if (imu_acquisition_stats_format(report, sizeof(report), &stats) > 0)
    {
        rt_kprintf("%s", report);
    }

    event_service_get_stats(&event_stats);
    rt_kprintf("event state=%u pretrigger_blocks=%u busy=%lu export_err=%lu resource_err=%lu\n",
               (unsigned int)event_stats.state,
               (unsigned int)event_stats.pretrigger_block_count,
               (unsigned long)event_stats.busy_trigger_count,
               (unsigned long)event_stats.export_error_count,
               (unsigned long)event_stats.resource_reject_count);
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    {
        ai_inference_stats_t ai_stats;

        ai_inference_service_get_stats(&ai_stats);
        rt_kprintf("ai model_ready=%u submitted=%lu processed=%lu queue_drop=%lu result_queue_drop=%lu result_queue_backpressure=%lu result_queue_depth=%lu result_queue_high_watermark=%lu store_err=%lu feature_err=%lu runtime_err=%lu last_event=%lu class=%u confidence_permille=%lu\n",
                   (unsigned int)ai_stats.model_ready,
                   (unsigned long)ai_stats.submitted_count,
                   (unsigned long)ai_stats.processed_count,
                   (unsigned long)ai_stats.queue_drop_count,
                   (unsigned long)ai_stats.result_queue_drop_count,
                   (unsigned long)ai_stats.result_queue_backpressure_count,
                   (unsigned long)ai_stats.result_queue_depth,
                   (unsigned long)ai_stats.result_queue_high_watermark,
                   (unsigned long)ai_stats.result_store_error_count,
                   (unsigned long)ai_stats.feature_error_count,
                   (unsigned long)ai_stats.runtime_error_count,
                   (unsigned long)ai_stats.last_event_id,
                   (unsigned int)ai_stats.last_class_index,
                   (unsigned long)(ai_stats.last_confidence * 1000.0F));
    }
#endif
}

static void system_report_entry(void *parameter)
{
    uint32_t elapsed_seconds = 0U;

    (void)parameter;

    while (RT_TRUE)
    {
        runtime_health_evaluate();
        runtime_confirm_healthy_trial();

        if (elapsed_seconds % SYSTEM_REPORT_INTERVAL_SECONDS == 0U)
        {
            print_imu_health_report();
            print_system_report();
        }

        elapsed_seconds++;
        rt_thread_mdelay(IMU_HEALTH_REPORT_INTERVAL_MS);
    }
}

static void sysinfo(void)
{
    print_system_report();
}
MSH_CMD_EXPORT(sysinfo, show transport recorder system information);

void app_runtime_start(void)
{
#ifdef TRANSPORT_FAULT_INJECTION_ENABLED
    fault_injection_register_event_quality_hook();
#endif
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    runtime_crash_record_startup();
#endif

    const rt_err_t power_runtime_result = power_runtime_start();

    if (power_runtime_result != RT_EOK)
    {
        rt_kprintf("power runtime start failed; fault fallback active\n");
    }

#ifdef TRANSPORT_PHASE11_QSPI_HIL
    rt_kprintf("P11,BOOT,HIL_ONLY,OK,0,0,0\n");
    phase11_qspi_hil_run(TRANSPORT_PHASE11_QSPI_HIL_CYCLES);
#elif defined(TRANSPORT_PHASE11_UART3_ECHO)
    if (uart3_echo_start() != RT_EOK)
    {
        rt_kprintf("P11,UART3_ECHO,FAIL,0,0,0\n");
    }
#elif defined(TRANSPORT_PHASE11_OTA_ONLY)
    health_service_init(&runtime_health_service);
    rt_kprintf("P11,BOOT,OTA_ONLY,OK,0,0,0\n");
    if (terp_uart3_start(&runtime_health_service) != RT_EOK)
    {
        rt_kprintf("P11,TERP,UART3,FAIL,0,0,0\n");
    }
    else
    {
        rt_kprintf("P11,TERP,UART3,OK,0,0,0\n");
    }
#else
    event_export_sink_t event_sink;
    rt_thread_t reporter;
    rt_err_t terp_result;
    rt_err_t imu_result;
    rt_bool_t acquisition_started = RT_FALSE;

    health_service_init(&runtime_health_service);
    event_service_set_health_service(&runtime_health_service);

    terp_result = terp_uart3_start(&runtime_health_service);
    if (terp_result != RT_EOK)
    {
        rt_kprintf("TERP UART3 start failed\n");
    }

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    {
        const ai_model_t *runtime_model = terp_uart3_get_runtime_model();

        if (runtime_model == RT_NULL)
        {
            runtime_model = &transport_ai_model_v1;
        }
        if (ai_inference_service_start(runtime_model) != RT_EOK)
        {
            rt_kprintf("AI inference service start failed; fallback active\n");
        }
        else
        {
            ai_inference_stats_t ai_stats;

            ai_inference_service_get_stats(&ai_stats);
            rt_kprintf("AI inference model=%s\n",
                       ai_stats.model_ready ? "validated-runtime-model" : "fallback");
            runtime_ai_result_sidecar_start();
        }
    }
#endif

    if (storage_service_start() != RT_EOK)
    {
        rt_kprintf("event log start failed\n");
    }
    storage_service_get_event_sink(&event_sink);

    imu_result = imu_acquisition_start();
    if (imu_result != RT_EOK)
    {
        rt_kprintf("imu acquisition start failed\n");
    }
    else
    {
        acquisition_started = RT_TRUE;
        if (event_service_start(&event_sink) != RT_EOK)
        {
            rt_kprintf("event service start failed\n");
        }
        else
        {
            runtime_event_started = RT_TRUE;
        }
    }

    if (power_runtime_result == RT_EOK && acquisition_started
        && runtime_event_started)
    {
        (void)power_runtime_set_mode(POWER_MODE_MONITOR);
    }
    else
    {
        (void)power_runtime_set_mode(POWER_MODE_FAULT_FALLBACK);
    }

    board_led_init();
    reporter = rt_thread_create("sysreport", system_report_entry, RT_NULL,
                                2048, 12, 10);

    if (reporter != RT_NULL)
    {
        rt_thread_startup(reporter);
    }
    else
    {
        rt_kprintf("system reporter start failed; fault fallback active\n");
        (void)power_runtime_set_mode(POWER_MODE_FAULT_FALLBACK);
    }
#endif
}
