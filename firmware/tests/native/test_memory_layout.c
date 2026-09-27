#include <stdint.h>
#include <stdio.h>

#include "project_config.h"
#include "rtconfig.h"


_Static_assert(TRANSPORT_FLASH_BASE == UINT32_C(0x08000000),
               "STM32H743VIT6 internal Flash must start at 0x08000000");
_Static_assert(TRANSPORT_BOOTLOADER_BASE == UINT32_C(0x08000000),
               "Bootloader must own the first 128 KiB of internal Flash");
_Static_assert(TRANSPORT_BOOTLOADER_SIZE_BYTES == UINT32_C(0x00020000),
               "Bootloader reservation must be 128 KiB");
_Static_assert(TRANSPORT_APP_BASE == UINT32_C(0x08020000),
               "Application must link after the Bootloader reservation");
_Static_assert(TRANSPORT_APP_SIZE_BYTES == UINT32_C(0x001A0000),
               "Application must end before the OTA state sectors");
_Static_assert(TRANSPORT_STATE_PRIMARY_BASE == UINT32_C(0x081C0000),
               "Primary OTA state must occupy the first reserved Flash sector");
_Static_assert(TRANSPORT_STATE_SECONDARY_BASE == UINT32_C(0x081E0000),
               "Secondary OTA state must occupy the final Flash sector");
_Static_assert(TRANSPORT_STATE_RECORD_SIZE_BYTES == UINT32_C(0x00020000),
               "Each OTA state record must own one H743 128 KiB Flash sector");
_Static_assert(TRANSPORT_FLASH_SIZE_BYTES == UINT32_C(0x00200000),
               "STM32H743VIT6 internal flash size must be 2 MiB");
_Static_assert(TRANSPORT_AXI_SRAM_BASE == UINT32_C(0x24000000),
               "AXI SRAM base must match the linker memory region");
_Static_assert(TRANSPORT_AXI_SRAM_SIZE_BYTES == UINT32_C(0x00080000),
               "AXI SRAM size must be 512 KiB");
_Static_assert(TRANSPORT_D2_SRAM1_BASE == UINT32_C(0x30000000),
               "DMA buffers must use D2 SRAM1, not DTCM");
_Static_assert(TRANSPORT_D2_SRAM1_SIZE_BYTES == UINT32_C(0x00020000),
               "D2 SRAM1 size must be 128 KiB");
_Static_assert(TRANSPORT_DMA_ALIGNMENT_BYTES == UINT32_C(32),
               "DMA buffers must follow the Cortex-M7 cache-line alignment");
_Static_assert(IDLE_THREAD_STACK_SIZE >= 1024,
               "H743 idle thread must have 1 KiB for RT-Thread housekeeping");

_Static_assert(TRANSPORT_HSE_HZ == UINT32_C(25000000),
               "The pinned OpenMV4 H743 design uses a 25 MHz HSE");
_Static_assert(TRANSPORT_SYSCLK_HZ == UINT32_C(240000000),
               "The first BSP release must run SYSCLK at 240 MHz");
_Static_assert(TRANSPORT_HCLK_HZ == UINT32_C(240000000),
               "The first BSP release must run HCLK at 240 MHz");
_Static_assert(TRANSPORT_PCLK_HZ == UINT32_C(120000000),
               "All APB buses must run at 120 MHz in the first release");
_Static_assert(TRANSPORT_APB_TIMER_HZ == UINT32_C(240000000),
               "APB timers must account for the x2 timer clock rule");

int main(void)
{
    puts("memory layout: PASS");
    return 0;
}
