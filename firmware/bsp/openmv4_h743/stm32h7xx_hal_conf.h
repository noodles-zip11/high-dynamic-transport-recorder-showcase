#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
/* STM32H7 UART_HandleTypeDef embeds DMA handle pointers even without UART DMA. */
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_MDMA_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_QSPI_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED

#ifndef HSE_VALUE
#define HSE_VALUE UINT32_C(25000000)
#endif

#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT UINT32_C(100)
#endif

#ifndef CSI_VALUE
#define CSI_VALUE UINT32_C(4000000)
#endif

#ifndef HSI_VALUE
#define HSI_VALUE UINT32_C(64000000)
#endif

#ifndef LSE_VALUE
#define LSE_VALUE UINT32_C(32768)
#endif

#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT UINT32_C(5000)
#endif

#ifndef LSI_VALUE
#define LSI_VALUE UINT32_C(32000)
#endif

#ifndef EXTERNAL_CLOCK_VALUE
#define EXTERNAL_CLOCK_VALUE UINT32_C(12288000)
#endif

#define VDD_VALUE UINT32_C(3300)
#define TICK_INT_PRIORITY UINT32_C(0)
#define USE_RTOS 0U

#define USE_HAL_DMA_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32h7xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32h7xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32h7xx_hal_dma.h"
#endif

#ifdef HAL_MDMA_MODULE_ENABLED
#include "stm32h7xx_hal_mdma.h"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32h7xx_hal_i2c.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32h7xx_hal_cortex.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32h7xx_hal_exti.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32h7xx_hal_flash.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32h7xx_hal_pwr.h"
#endif

#ifdef HAL_QSPI_MODULE_ENABLED
#include "stm32h7xx_hal_qspi.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
#include "stm32h7xx_hal_uart.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32h7xx_hal_spi.h"
#endif



#define assert_param(expression) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif
