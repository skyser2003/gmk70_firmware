// SPDX-License-Identifier: GPL-2.0-or-later
// Actual pinned WS2812 encoder + maintained adapter, with a bounded HAL model.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "ch.h"
#include "ws2812.h"
#include "rgb_idle.h"

static SPI_TypeDef    registers;
SPIDriver             SPIDM2 = {.state = SPI_STOP, .spi = &registers};
static unsigned       starts, stops, sends, acquires, guards;
static bool           clock_on, output_low, output_gpio, block_idle;
static unsigned       mode;
static const uint8_t *borrowed;
static size_t         borrowed_size;
static uint8_t        saved[1040];

void chMtxLock(mutex_t *mutex) {
    assert(!mutex->held);
    mutex->held = true;
}
void chMtxUnlock(mutex_t *mutex) {
    assert(mutex->held);
    mutex->held = false;
}
bool chMtxTryLock(mutex_t *mutex) {
    if (block_idle || mutex->held) return false;
    chMtxLock(mutex);
    return true;
}
void palSetLineMode(unsigned pin, unsigned new_mode) {
    assert(pin == WS2812_DI_PIN);
    mode        = new_mode;
    output_gpio = false;
}
void gpio_write_pin_low(unsigned pin) {
    assert(pin == WS2812_DI_PIN && !clock_on && SPIDM2.state == SPI_STOP);
    output_low = true;
}
void gpio_set_pin_output(unsigned pin) {
    assert(pin == WS2812_DI_PIN && output_low && !clock_on);
    output_gpio = true;
}
void wait_us(uint16_t microseconds) {
    assert(microseconds == WS2812_TRST_US);
    assert(clock_on && !output_gpio && SPIDM2.state == SPI_READY);
    assert(mode == WS2812_SPI_MOSI_PAL_MODE);
    ++guards;
}
void spiAcquireBus(SPIDriver *driver) {
    assert(driver == &SPIDM2);
    assert(++acquires == 1); // Native init owns this bus for its lifetime.
}
void spiStart(SPIDriver *driver, const SPIConfig *config) {
    assert(driver == &SPIDM2 && config && config->divisor == 32);
    assert(driver->state == SPI_STOP || driver->state == SPI_READY);
    driver->config = config;
    driver->state  = SPI_READY;
    clock_on       = true;
    ++starts;
}
void spiStop(SPIDriver *driver) {
    assert(driver == &SPIDM2 && driver->state == SPI_READY && clock_on);
    assert(!(registers.SR & SPI_SR_BUSY));
    driver->state  = SPI_STOP;
    driver->config = NULL; // Real ChibiOS clears it; adapter must preserve it.
    clock_on       = false;
    ++stops;
}
void spiSelect(SPIDriver *driver) {
    assert(driver == &SPIDM2 && driver->state == SPI_READY && clock_on);
}
void spiStartSend(SPIDriver *driver, size_t length, const void *data) {
    assert(driver == &SPIDM2 && driver->state == SPI_READY && clock_on);
    assert(!output_gpio && length <= sizeof(saved));
    borrowed      = data;
    borrowed_size = length;
    memcpy(saved, data, length);
    driver->state = SPI_ACTIVE;
    registers.SR  = SPI_SR_BUSY;
    ++sends;
}
void spiSend(SPIDriver *driver, size_t length, const void *data) {
    (void)driver;
    (void)length;
    (void)data;
    assert(!"GMK70 default output must remain asynchronous");
}
void rccDisableSPIM2(void) {
    assert(SPIDM2.state == SPI_STOP);
    clock_on = false;
}
static void complete(void) {
    assert(SPIDM2.state == SPI_ACTIVE);
    assert(!memcmp(borrowed, saved, borrowed_size));
    SPIDM2.state = SPI_READY;
    registers.SR = 0;
}
static void assert_color(bool red) {
    size_t offset = 4 + (RGB_MATRIX_LED_COUNT - 1) * 12;
    for (unsigned i = 0; i < 12; ++i) {
        bool lit = red ? i < 4 : i >= 4 && i < 8;
        assert(borrowed[offset + i] == (lit ? 0xee : 0x88));
    }
    for (size_t i = 0; i < 4; ++i)
        assert(borrowed[i] == 0);
    for (size_t i = 4 + RGB_MATRIX_LED_COUNT * 12; i < borrowed_size; ++i)
        assert(borrowed[i] == 0);
}
int main(void) {
    gmk70_rgb_idle_poll(true);
    ws2812_flush(); // No peripheral access before initialization.
    assert(!starts && !stops && !sends);
    ws2812_init();
    assert(starts == 1 && stops == 1 && !clock_on && output_gpio && output_low);
    assert(SPIDM2.config == NULL);

    ws2812_set_color_all(0, 0, 0);
    ws2812_set_color(RGB_MATRIX_LED_COUNT - 1, 255, 0, 0);
    ws2812_flush();
    assert(starts == 2 && sends == 1 && clock_on && guards == 1);
    assert_color(true);
    ws2812_set_color(RGB_MATRIX_LED_COUNT - 1, 0, 255, 0);
    ws2812_flush(); // A concurrent flush cannot overwrite the borrowed buffer.
    gmk70_rgb_idle_poll(true);
    gmk70_rgb_idle_resume();
    assert(sends == 1 && stops == 1 && clock_on);
    assert_color(true);

    SPIDM2.state = SPI_READY; // Model final RX preceding shifter-idle.
    gmk70_rgb_idle_poll(true);
    assert(stops == 1 && clock_on);
    registers.SR = 0;
    block_idle   = true;
    gmk70_rgb_idle_poll(true);
    assert(stops == 1 && clock_on);
    block_idle = false;
    gmk70_rgb_idle_poll(true); // Deliver the coalesced final update without a new render.
    assert(sends == 2 && stops == 1 && clock_on);
    assert_color(false);
    complete();
    gmk70_rgb_idle_poll(true);
    assert(stops == 2 && !clock_on && output_gpio && output_low);

    ws2812_flush();
    assert(starts == 3 && sends == 3 && guards == 2);
    assert_color(false);
    complete();
    gmk70_rgb_idle_resume(); // READY still needs its clock until parked.
    assert(clock_on);
    gmk70_rgb_idle_poll(true);
    clock_on = true; // Common full-STOP clock restoration enables SPIM2.
    gmk70_rgb_idle_resume();
    assert(!clock_on && SPIDM2.state == SPI_STOP);

    ws2812_set_color(RGB_MATRIX_LED_COUNT - 1, 255, 0, 0);
    ws2812_flush();
    ws2812_set_color(RGB_MATRIX_LED_COUNT - 1, 0, 255, 0);
    ws2812_flush();
    complete();
    unsigned sleeping_sends = sends;
    gmk70_rgb_idle_poll(false); // Sleep may park pending work, never transmit it.
    assert(!clock_on && sends == sleeping_sends);
    clock_on = true;
    gmk70_rgb_idle_resume();
    gmk70_rgb_idle_poll(false);
    assert(!clock_on && sends == sleeping_sends);
    gmk70_rgb_idle_poll(true);
    assert(clock_on && sends == sleeping_sends + 1);
    assert_color(false);
    complete();
    gmk70_rgb_idle_poll(true);

    for (unsigned i = 0; i < 100; ++i) {
        ws2812_set_color_all(0, 0, 0);
        ws2812_flush();
        for (size_t byte = 4; byte < 4 + RGB_MATRIX_LED_COUNT * 12; ++byte)
            assert(borrowed[byte] == 0x88);
        complete();
        gmk70_rgb_idle_poll(true);
        assert(!clock_on && output_gpio && output_low && SPIDM2.config == NULL);
    }
    assert(acquires == 1 && stops == starts && guards == starts - 1);
    printf("RGB idle (%u LEDs): native async encoding, busy/contended deferral, buffer ownership, restart/reset guard and STOP restore passed\n", RGB_MATRIX_LED_COUNT);
}
