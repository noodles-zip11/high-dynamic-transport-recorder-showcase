#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stm32h7xx.h>

#include "board_power.h"

SCB_Type native_scb;

static char native_trace[4];
static size_t native_trace_length;

void native_power_trace(char marker)
{
    if (native_trace_length < sizeof(native_trace))
    {
        native_trace[native_trace_length] = marker;
        ++native_trace_length;
    }
}

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "board power: %s\n", message);
        return 1;
    }

    return 0;
}

int main(void)
{
    native_scb.SCR = UINT32_C(0xFFFFFFFF);
    native_trace_length = 0U;
    memset(native_trace, 0, sizeof(native_trace));

    board_power_cpu_sleep();

    if (expect_true((native_scb.SCR & SCB_SCR_SLEEPDEEP_Msk) == 0U,
                    "SLEEPDEEP must be cleared")
        || expect_true(native_trace_length == 3U, "all barriers must execute")
        || expect_true(memcmp(native_trace, "DWI", 3U) == 0,
                       "barriers must execute as DSB/WFI/ISB"))
    {
        return 1;
    }

    puts("board power: PASS");
    return 0;
}
