#pragma once
#include <stdbool.h>
#include <stdint.h>
void    uart_init(uint32_t baud);
bool    uart_available(void);
uint8_t uart_read(void);
void    uart_transmit(const uint8_t *data, uint32_t length);
