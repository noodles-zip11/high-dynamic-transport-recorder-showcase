#include "uart3_echo_service.h"

#include <rtdevice.h>

#include "uart3_echo.h"

#define UART3_ECHO_DEVICE_NAME "uart3"
#define UART3_ECHO_BUFFER_BYTES 64U

typedef struct
{
    rt_device_t device;
    struct rt_semaphore received;
    uint8_t buffer[UART3_ECHO_BUFFER_BYTES];
} uart3_echo_service_t;

static uart3_echo_service_t uart3_echo_service;

static rt_err_t uart3_echo_rx_indicate(rt_device_t device, rt_size_t size)
{
    (void)device;
    (void)size;
    return rt_sem_release(&uart3_echo_service.received);
}

static void uart3_echo_drain(void)
{
    rt_size_t length;
    rt_size_t index;

    do
    {
        length = rt_device_read(uart3_echo_service.device, 0, uart3_echo_service.buffer,
                                sizeof(uart3_echo_service.buffer));
        for (index = 0U; index < length; index++)
        {
            uint8_t echoed;

            if (uart3_echo_byte(uart3_echo_service.buffer[index], &echoed) == 0)
            {
                (void)rt_device_write(uart3_echo_service.device, 0, &echoed, 1U);
            }
        }
    } while (length == sizeof(uart3_echo_service.buffer));
}

static void uart3_echo_entry(void *parameter)
{
    (void)parameter;

    while (RT_TRUE)
    {
        if (rt_sem_take(&uart3_echo_service.received, RT_WAITING_FOREVER) == RT_EOK)
        {
            uart3_echo_drain();
        }
    }
}

rt_err_t uart3_echo_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    uart3_echo_service.device = rt_device_find(UART3_ECHO_DEVICE_NAME);
    if (uart3_echo_service.device == RT_NULL)
    {
        return -RT_ENOSYS;
    }
    result = rt_sem_init(&uart3_echo_service.received, "u3_echo", 0U, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        return result;
    }
    result = rt_device_open(uart3_echo_service.device,
                            RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (result != RT_EOK)
    {
        rt_sem_detach(&uart3_echo_service.received);
        return result;
    }
    result = rt_device_set_rx_indicate(uart3_echo_service.device, uart3_echo_rx_indicate);
    if (result != RT_EOK)
    {
        rt_device_close(uart3_echo_service.device);
        rt_sem_detach(&uart3_echo_service.received);
        return result;
    }
    thread = rt_thread_create("u3_echo", uart3_echo_entry, RT_NULL, 512U, 18U, 10U);
    if (thread == RT_NULL)
    {
        rt_device_set_rx_indicate(uart3_echo_service.device, RT_NULL);
        rt_device_close(uart3_echo_service.device);
        rt_sem_detach(&uart3_echo_service.received);
        return -RT_ENOMEM;
    }
    rt_thread_startup(thread);
    return RT_EOK;
}
