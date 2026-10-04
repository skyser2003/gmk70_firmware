// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the pinned upstream driver with the restored board configuration.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "ws2812.h"

SPIDriver       SPIDM2;
static uint8_t  packet[1040];
static unsigned sends;

void palSetLineMode(unsigned pin, unsigned mode) {}
void spiAcquireBus(SPIDriver *driver) {
    assert(driver == &SPIDM2);
}
void spiStart(SPIDriver *driver, const SPIConfig *config) {
    assert(driver == &SPIDM2);
    assert(config->divisor == 32);
    driver->state = SPI_READY;
}
void spiSelect(SPIDriver *driver) {
    assert(driver == &SPIDM2);
}
void spiStartSend(SPIDriver *driver, size_t length, const void *data) {
    assert(driver == &SPIDM2);
    assert(length == sizeof(packet));
    memcpy(packet, data, length);
    ++sends;
}
void spiSend(SPIDriver *driver, size_t length, const void *data) {
    assert(!"The baseline LED path must remain asynchronous");
}

int main(void) {
    ws2812_init();
    ws2812_set_color_all(0, 0, 0);
    ws2812_set_color(33, 255, 0, 0);
    ws2812_flush();
    assert(sends == 1);
    const uint8_t red_grb[] = {0x88, 0x88, 0x88, 0x88, 0xEE, 0xEE, 0xEE, 0xEE, 0x88, 0x88, 0x88, 0x88};
    assert(memcmp(packet + 4 + 33 * 12, red_grb, sizeof(red_grb)) == 0);
    for (size_t i = 0; i < 4; ++i)
        assert(packet[i] == 0);
    for (size_t i = 4 + 77 * 12; i < sizeof(packet); ++i)
        assert(packet[i] == 0);

    ws2812_set_color(33, 0, 255, 0);
    ws2812_flush();
    assert(sends == 2);
    const uint8_t green_grb[] = {0xEE, 0xEE, 0xEE, 0xEE, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};
    assert(memcmp(packet + 4 + 33 * 12, green_grb, sizeof(green_grb)) == 0);
    ws2812_set_color(33, 0, 0, 0);
    ws2812_flush();
    assert(sends == 3);
    for (size_t i = 4; i < 4 + 77 * 12; ++i)
        assert(packet[i] == 0x88);
    puts("Baseline asynchronous GRB LED output tests passed");
    return 0;
}
