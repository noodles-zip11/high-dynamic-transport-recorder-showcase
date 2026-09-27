#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_MDMA_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_QSPI_MODULE_ENABLED

#define HSE_VALUE UINT32_C(25000000)
#define HSE_STARTUP_TIMEOUT UINT32_C(100)
#define CSI_VALUE UINT32_C(4000000)
#define HSI_VALUE UINT32_C(64000000)
#define LSE_VALUE UINT32_C(32768)
#define LSE_STARTUP_TIMEOUT UINT32_C(5000)
#define LSI_VALUE UINT32_C(32000)
#define EXTERNAL_CLOCK_VALUE UINT32_C(12288000)
#define VDD_VALUE UINT32_C(3300)
#define TICK_INT_PRIORITY UINT32_C(0)
#define USE_RTOS 0U

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_mdma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_qspi.h"

#define assert_param(expression) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif
