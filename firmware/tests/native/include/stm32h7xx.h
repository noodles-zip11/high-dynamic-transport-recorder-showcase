#ifndef STM32H7XX_H
#define STM32H7XX_H

#include <stdint.h>

typedef struct
{
    volatile uint32_t SCR;
} SCB_Type;

extern SCB_Type native_scb;
extern void native_power_trace(char marker);

#define SCB (&native_scb)
#define SCB_SCR_SLEEPDEEP_Msk (UINT32_C(1) << 2)

static inline void __DSB(void)
{
    native_power_trace('D');
}

static inline void __WFI(void)
{
    native_power_trace('W');
}

static inline void __ISB(void)
{
    native_power_trace('I');
}

#endif
