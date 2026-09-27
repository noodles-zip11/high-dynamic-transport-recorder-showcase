#ifndef TRANSPORT_RECORDER_UART3_ECHO_H
#define TRANSPORT_RECORDER_UART3_ECHO_H

#include <stdint.h>

int uart3_echo_byte(uint8_t received, uint8_t *echoed);

#endif
