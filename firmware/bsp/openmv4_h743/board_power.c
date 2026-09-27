#include "board_power.h"

#include <stm32h7xx.h>

void board_power_cpu_sleep(void)
{
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __ISB();
}
