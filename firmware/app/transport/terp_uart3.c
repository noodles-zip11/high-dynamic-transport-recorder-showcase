#include "terp_uart3.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "build_identity.h"
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
#include "ai_inference_service.h"
#endif
#include "terp_device.h"
#include "terp_service.h"
#include "power_runtime.h"

#define TERP_UART3_DEVICE_NAME "uart3"
#define TERP_UART3_RX_BUFFER_BYTES 256U
#define TERP_UART3_IDLE_TIMEOUT_TICKS RT_TICK_PER_SECOND
#define TERP_UART3_THREAD_PRIORITY 18U
#define TERP_UART3_THREAD_TICK 10U

typedef struct
{
    rt_device_t device;
    struct rt_semaphore rx_ready;
    terp_service_t service;
    uint8_t rx_buffer[TERP_UART3_RX_BUFFER_BYTES];
} terp_uart3_t;

static terp_uart3_t terp_uart3;
static rt_bool_t terp_uart3_started;

#ifdef TRANSPORT_AI_INFERENCE_ENABLED
static int terp_uart3_prepare_runtime_model(const ai_model_t *model,
                                             void *context)
{
    (void)context;
    return ai_inference_service_prepare_model(model) == RT_EOK ? 0 : -1;
}

static int terp_uart3_publish_runtime_model(const ai_model_t *model,
                                            void *context)
{
    (void)context;
    return ai_inference_service_publish_model(model) == RT_EOK ? 0 : -1;
}

static void terp_uart3_abort_runtime_model(void *context)
{
    (void)context;
    ai_inference_service_abort_model();
}

static void terp_uart3_quarantine_runtime_model(void *context)
{
    (void)context;
    ai_inference_service_quarantine_model();
}
#endif

static int terp_uart3_write(const uint8_t *data, uint32_t length, void *context)
{
    terp_uart3_t *transport = context;
    rt_ssize_t written;
    rt_bool_t transport_blocker_held;

    if (transport == RT_NULL || transport->device == RT_NULL)
    {
        return -1;
    }
    transport_blocker_held = power_runtime_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                             == RT_EOK;
    written = rt_device_write(transport->device, 0, data, length);
    if (transport_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_TRANSPORT);
    }
    return written >= 0 && (rt_size_t)written == (rt_size_t)length ? 0 : -1;
}

static rt_err_t terp_uart3_rx_indicate(rt_device_t device, rt_size_t size)
{
    (void)device;
    (void)size;
    power_runtime_record_wake(POWER_WAKE_UART);
    return rt_sem_release(&terp_uart3.rx_ready);
}

static void terp_uart3_drain_rx(terp_uart3_t *transport)
{
    rt_size_t length;
    rt_bool_t transport_blocker_held;

    transport_blocker_held = power_runtime_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                             == RT_EOK;
    do
    {
        rt_bool_t ota_blocker_held = RT_FALSE;
        rt_bool_t maintenance_entered = RT_FALSE;
        rt_err_t maintenance_result;

        length = rt_device_read(transport->device, 0, transport->rx_buffer,
                                sizeof(transport->rx_buffer));
        if (length != 0U)
        {
            maintenance_result =
                power_runtime_enter_maintenance_if_monitor(&maintenance_entered);
            if (!terp_uart3_rx_maintenance_acquired(maintenance_result,
                                                    maintenance_entered))
            {
                /* Drain and discard bytes received during BOOT/EVENT/FAULT. */
                continue;
            }
            ota_blocker_held =
                power_runtime_acquire_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC)
                == RT_EOK;
            if (ota_blocker_held)
            {
                terp_service_receive(&transport->service, transport->rx_buffer,
                                     length);
            }
            if (ota_blocker_held)
            {
                power_runtime_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
            }
            if (maintenance_entered)
            {
                (void)power_runtime_exit_maintenance_if_owned();
            }
        }
    } while (length == sizeof(transport->rx_buffer));
    if (transport_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_TRANSPORT);
    }
}

static void terp_uart3_entry(void *parameter)
{
    terp_uart3_t *transport = parameter;

    while (RT_TRUE)
    {
        if (rt_sem_take(&transport->rx_ready,
                        TERP_UART3_IDLE_TIMEOUT_TICKS) == RT_EOK)
        {
            terp_uart3_drain_rx(transport);
        }
        else
        {
            terp_service_on_timeout(&transport->service);
        }
    }
}

rt_err_t terp_uart3_start(const health_service_t *health_service)
{
    static const char model[] = "STM32H743";
    static const char firmware[] = "phase08-terp-uart3";
    static const char hardware[] = "openmv4-h743-pd8-pd9";
    static const char serial[] = TRANSPORT_DEVICE_SERIAL;
    static const terp_device_info_t device_info = {
        .model = model,
        .model_length = sizeof(model) - 1U,
        .firmware_version = firmware,
        .firmware_version_length = sizeof(firmware) - 1U,
        .hardware_version = hardware,
        .hardware_version_length = sizeof(hardware) - 1U,
        .serial_number = serial,
        .serial_number_length = sizeof(serial) - 1U,
        .capability_flags = TERP_CAPABILITY_DEVICE_INFO
                            | TERP_CAPABILITY_HEALTH
                            | TERP_CAPABILITY_EVENT_READ,
    };
    rt_thread_t thread;
    rt_err_t result;

    if (terp_uart3_started)
    {
        return RT_EOK;
    }
    terp_uart3.device = rt_device_find(TERP_UART3_DEVICE_NAME);
    if (terp_uart3.device == RT_NULL)
    {
        return -RT_ENOSYS;
    }
    result = rt_sem_init(&terp_uart3.rx_ready, "terp_rx", 0U, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        return result;
    }
    result = rt_device_open(terp_uart3.device,
                            RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (result != RT_EOK)
    {
        rt_sem_detach(&terp_uart3.rx_ready);
        return result;
    }
    result = rt_device_set_rx_indicate(terp_uart3.device, terp_uart3_rx_indicate);
    if (result != RT_EOK)
    {
        rt_device_close(terp_uart3.device);
        rt_sem_detach(&terp_uart3.rx_ready);
        return result;
    }
    terp_service_init(&terp_uart3.service, &device_info, health_service, NULL,
                      terp_uart3_write, &terp_uart3);
#ifdef TRANSPORT_AI_INFERENCE_ENABLED
    terp_service_set_runtime_model_activation(
        &terp_uart3.service, terp_uart3_prepare_runtime_model,
        terp_uart3_publish_runtime_model, terp_uart3_abort_runtime_model,
        terp_uart3_quarantine_runtime_model, RT_NULL);
#endif
    thread = rt_thread_create("terp_rx", terp_uart3_entry, &terp_uart3,
                              TERP_UART3_THREAD_STACK_BYTES,
                              TERP_UART3_THREAD_PRIORITY,
                              TERP_UART3_THREAD_TICK);
    if (thread == RT_NULL)
    {
        rt_device_set_rx_indicate(terp_uart3.device, RT_NULL);
        rt_device_close(terp_uart3.device);
        rt_sem_detach(&terp_uart3.rx_ready);
        return -RT_ENOMEM;
    }
    terp_uart3_started = RT_TRUE;
    rt_thread_startup(thread);
    (void)rt_sem_release(&terp_uart3.rx_ready);
    return RT_EOK;
}

const ai_model_t *terp_uart3_get_runtime_model(void)
{
    return terp_uart3_started
               ? terp_service_get_runtime_model(&terp_uart3.service)
               : RT_NULL;
}
