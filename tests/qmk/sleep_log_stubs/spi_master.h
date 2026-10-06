// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int16_t spi_status_t;
bool            spi_start(uint16_t pin, bool lsb, uint8_t mode, uint16_t divisor);
spi_status_t    spi_write(uint8_t byte);
spi_status_t    spi_read(void);
void            spi_stop(void);
