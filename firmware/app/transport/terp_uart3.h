#ifndef TRANSPORT_RECORDER_TERP_UART3_H
#define TRANSPORT_RECORDER_TERP_UART3_H

#include <rtthread.h>

#include "ai_runtime.h"
#include "health_service.h"

/* Model package validation nests QSPI reads, SHA-256, model decode, and
 * golden-vector inference on the TERP worker; keep that path off the edge of
 * the RT-Thread stack. */
#define TERP_UART3_THREAD_STACK_BYTES 4096U

/* RX commands are valid only while this drain owns a maintenance lease. */
static inline rt_bool_t terp_uart3_rx_maintenance_acquired(
    rt_err_t maintenance_result,
    rt_bool_t maintenance_entered)
{
    return maintenance_result == RT_EOK && maintenance_entered;
}

/* Starts the board-mapped USART3 byte adapter used by TERP. */
rt_err_t terp_uart3_start(const health_service_t *health_service);
const ai_model_t *terp_uart3_get_runtime_model(void);

#endif
