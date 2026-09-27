#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "uart3_echo.h"

static void test_echo_returns_the_received_byte(void)
{
    uint8_t echoed = 0U;

    assert(uart3_echo_byte(UINT8_C(0x00), &echoed) == 0);
    assert(echoed == UINT8_C(0x00));
    assert(uart3_echo_byte(UINT8_C(0xA5), &echoed) == 0);
    assert(echoed == UINT8_C(0xA5));
}

int main(void)
{
    test_echo_returns_the_received_byte();
    puts("uart3 echo: PASS");
    return 0;
}
