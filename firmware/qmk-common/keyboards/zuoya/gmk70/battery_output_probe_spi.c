// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal.h"
#include "ws2812.h"
#include "config.h"
#include "timer.h"
#include "battery_output_probe.h"

_Static_assert(WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_RGB && sizeof(ws2812_led_t) == 3 && WS2812_LED_COUNT == 77, "Review diagnostic SPI decoder after changing the LED format");

void __real_spiSend(SPIDriver *driver, size_t length, const void *data);

void __wrap_spiSend(SPIDriver *driver, size_t length, const void *data) {
    if (driver == &WS2812_SPI_DRIVER) {
        battery_output_probe_transfer(data, length, driver->state == SPI_READY, driver->xfer.tx_len, driver->xfer.rx_len, timer_read32());
    }
    __real_spiSend(driver, length, data);
}
