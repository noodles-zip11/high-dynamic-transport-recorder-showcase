#ifndef TRANSPORT_RECORDER_BOARD_H
#define TRANSPORT_RECORDER_BOARD_H

#include <rtthread.h>
#include <stm32h7xx.h>
#include "board_pinmap.h"
#include "drv_common.h"
#include "drv_gpio.h"
#include "project_config.h"


#ifdef __cplusplus
extern "C" {
#endif

#define STM32_FLASH_START_ADRESS TRANSPORT_FLASH_BASE
#define STM32_FLASH_SIZE TRANSPORT_FLASH_SIZE_BYTES
#define STM32_FLASH_END_ADDRESS (STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE)

#define STM32_SRAM_START TRANSPORT_AXI_SRAM_BASE
#define STM32_SRAM_SIZE TRANSPORT_AXI_SRAM_SIZE_BYTES
#define STM32_SRAM_END (STM32_SRAM_START + STM32_SRAM_SIZE)

extern unsigned char __heap_start;
extern unsigned char __heap_end;
#define HEAP_BEGIN ((void *)&__heap_start)
#define HEAP_END ((void *)&__heap_end)

void SystemClock_Config(void);
void board_led_init(void);
void board_led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif
