#include "bootloader_hal.h"
#include "memory_layout.h"
#include "stm32h7xx.h"

void bootloader_jump_to_application(const bootloader_application_vectors_t *vectors)
{
    void (*application_reset_handler)(void);
    uint32_t index;

    if (vectors == NULL)
    {
        bootloader_recovery_wait();
    }
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }
    SCB->VTOR = TRANSPORT_OTA_APPLICATION_BASE;
    __DSB();
    __ISB();
    __set_MSP(vectors->initial_msp);
    application_reset_handler = (void (*)(void))(uintptr_t)vectors->reset_handler;
    application_reset_handler();
    bootloader_recovery_wait();
}
