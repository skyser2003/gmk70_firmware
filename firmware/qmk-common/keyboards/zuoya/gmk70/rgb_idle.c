// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal.h"
#include "ch.h"
#include "gpio.h"
#include "ws2812.h"
#include "wait.h"
#include "rgb_idle.h"

#ifdef WS2812_SPI_USE_CIRCULAR_BUFFER
#    error "GMK70 RGB clock gating requires non-circular SPI transfers"
#endif

static MUTEX_DECL(rgb_lock);
static const SPIConfig *rgb_config;
static bool             rgb_pending;

void __real_ws2812_init(void);
void __real_ws2812_flush(void);

static void park(void) {
    // READY follows the final RX byte. Also wait for the hardware shifter;
    // never abort an active transfer or stop clocks from an ISR callback.
    if (WS2812_SPI_DRIVER.state != SPI_READY || (WS2812_SPI_DRIVER.spi->SR & SPI_SR_BUSY)) return;
    spiStop(&WS2812_SPI_DRIVER);
    gpio_write_pin_low(WS2812_DI_PIN);
    gpio_set_pin_output(WS2812_DI_PIN);
}

void __wrap_ws2812_init(void) {
    chMtxLock(&rgb_lock);
    __real_ws2812_init();
    rgb_config = WS2812_SPI_DRIVER.config;
    park();
    chMtxUnlock(&rgb_lock);
}

static void flush(void) {
    // The native encoder's TX buffer remains borrowed until IRQ completion.
    if (rgb_config && WS2812_SPI_DRIVER.state == SPI_STOP) {
        spiStart(&WS2812_SPI_DRIVER, rgb_config);
        if (WS2812_SPI_DRIVER.state == SPI_READY) {
            spiSelect(&WS2812_SPI_DRIVER);
#ifdef WS2812_EXTERNAL_PULLUP
            palSetLineMode(WS2812_DI_PIN, PAL_MODE_ALTERNATE(WS2812_SPI_MOSI_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
#else
            palSetLineMode(WS2812_DI_PIN, PAL_MODE_ALTERNATE(WS2812_SPI_MOSI_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL);
#endif
            // Allow a mux/clock transition to settle before the encoded frame.
            wait_us(WS2812_TRST_US);
        }
    }
    if (rgb_config && WS2812_SPI_DRIVER.state == SPI_READY && !(WS2812_SPI_DRIVER.spi->SR & SPI_SR_BUSY)) {
        rgb_pending = false;
        __real_ws2812_flush();
    } else if (rgb_config) {
        rgb_pending = true;
    }
}

void __wrap_ws2812_flush(void) {
    chMtxLock(&rgb_lock);
    flush();
    chMtxUnlock(&rgb_lock);
}

void gmk70_rgb_idle_poll(bool allow_flush) {
    if (!rgb_config || !chMtxTryLock(&rgb_lock)) return;
    if (allow_flush && rgb_pending)
        flush();
    else
        park();
    chMtxUnlock(&rgb_lock);
}

void gmk70_rgb_idle_resume(void) {
    // The common STOP restore enables SPIM2. Keep a parked RGB driver parked;
    // the next native flush will restart it. This wake hook must not block.
    if (rgb_config && WS2812_SPI_DRIVER.state == SPI_STOP) rccDisableSPIM2();
}
