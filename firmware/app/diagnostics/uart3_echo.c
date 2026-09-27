#include "uart3_echo.h"

int uart3_echo_byte(uint8_t received, uint8_t *echoed)
{
    if (echoed == 0)
    {
        return -1;
    }
    *echoed = received;
    return 0;
}
