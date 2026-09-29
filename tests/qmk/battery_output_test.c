// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "ws2812.h"
#include "battery_indicator.h"
#include "battery_output_probe.h"

SPIDriver       SPIDM2;
static uint32_t now;
static unsigned sends;
static unsigned sync_sends, async_sends;
static uint8_t  packet[1040];

uint32_t timer_read32(void) {
    return now;
}
void spiAcquireBus(SPIDriver *driver) {
    assert(driver == &SPIDM2);
}
void spiStart(SPIDriver *driver, const SPIConfig *config) {
    assert(driver == &SPIDM2 && config->divisor == 32);
    driver->state = SPI_READY;
}
void spiSelect(SPIDriver *driver) {
    assert(driver == &SPIDM2);
}
void palSetLineMode(unsigned pin, unsigned mode) {
    (void)pin;
    (void)mode;
}

static void record_packet(size_t length, const void *data) {
    assert(length == sizeof(packet));
    memcpy(packet, data, length);
    ++sends;
}
void spiStartSend(SPIDriver *driver, size_t length, const void *data) {
    ++async_sends;
    record_packet(length, data);
}
void spiSend(SPIDriver *driver, size_t length, const void *data) {
    ++sync_sends;
    record_packet(length, data);
}
void __wrap_spiSend(SPIDriver *driver, size_t length, const void *data);

static void set_left_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 36) ws2812_set_color(index, r, g, b);
}
static void set_left_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 36; ++i)
        set_left_color(i, r, g, b);
}
static const battery_indicator_renderer_t renderer = {set_left_color, set_left_all};

static void frame(bool held, bool left, uint32_t time) {
    now = time;
    ws2812_set_color_all(12, 23, 34);
    if (held) battery_indicator_render(battery_indicator_state(false, true, false, true, false, 100), &renderer);
    battery_output_probe_frame(held, left);
    ws2812_flush(); // Compile and run the real pinned upstream SPI encoder.
}

static void test_charging_output_and_retained_observation(void) {
    ws2812_init();
    frame(true, true, 100);
    battery_output_sample_t sample = battery_output_probe_sample(0);
    assert(sample.frames == 1 && sample.busy_starts == 0 && sample.packet_size == 1040 && sample.valid);
    assert(sample.led33[0] == 80 && sample.led33[1] == 0 && sample.led33[2] == 0);
    assert(sample.led5[0] == 12 && sample.led5[1] == 23 && sample.led5[2] == 34);
    // Physical factory RGB order: red must occupy the first four bytes.
    const uint8_t expected_red[12] = {0x8e, 0x8e, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};
    assert(memcmp(packet + 4 + 33 * 12, expected_red, sizeof(expected_red)) == 0);
    for (unsigned i = 0; i < 4; ++i)
        assert(packet[i] == 0);
    for (unsigned i = 928; i < sizeof(packet); ++i)
        assert(packet[i] == 0);

    // A long hold must keep the charging overlay, with no timed test patterns.
    for (uint32_t time = 3100; time <= 12100; time += 3000) {
        frame(true, true, time);
        sample = battery_output_probe_sample(0);
        assert(sample.sampled_at == time && sample.uniform_leds == 35);
        assert(sample.led33[0] == 80 && sample.led33[1] == 0 && sample.led33[2] == 0);
        assert(sample.led5[0] == 12 && sample.led5[1] == 23 && sample.led5[2] == 34);
    }
    frame(false, true, 12200);
    assert(battery_output_probe_sample(0).sampled_at == 12100); // Retained on release.
    assert(memcmp(packet + 4 + 33 * 12, expected_red, sizeof(expected_red)) != 0);
    frame(true, true, 12300);
    assert(battery_output_probe_sample(0).sampled_at == 12300);
    frame(true, false, 16000);
    assert(battery_output_probe_sample(0).sampled_at == 12300); // No right probe.
    frame(true, true, UINT32_MAX - 1000);
    frame(true, true, 1999);
    assert(battery_output_probe_sample(0).sampled_at == 1999);
    for (uint8_t phase = 1; phase <= 3; ++phase)
        assert(battery_output_probe_sample(phase).frames == 0); // Retired pages.
}

static void test_observation_does_not_change_transfer(void) {
    SPIDM2.state       = SPI_ACTIVE;
    SPIDM2.xfer.tx_len = 99;
    SPIDM2.xfer.rx_len = 103;
    frame(true, true, 2000);
    battery_output_sample_t sample = battery_output_probe_sample(0);
    assert(sample.busy_starts == 1 && sample.previous_tx_remaining == 99 && sample.previous_rx_remaining == 103);
    uint8_t saved[sizeof(packet)];
    memcpy(saved, packet, sizeof(saved));
    unsigned  before = sends;
    SPIDriver other  = {0};
    __wrap_spiSend(&other, sizeof(saved), saved);
    assert(sends == before + 1 && memcmp(packet, saved, sizeof(packet)) == 0);
    assert(battery_output_probe_sample(0).frames == sample.frames);
    battery_output_probe_transfer(saved, 10, true, 0, 0, now);
    assert(!battery_output_probe_sample(0).valid);
    saved[4 + 33 * 12] = 0x17;
    battery_output_probe_transfer(saved, sizeof(saved), true, 0, 0, now);
    assert(!battery_output_probe_sample(0).valid);
    assert(battery_output_probe_sample(4).frames == 0);
}

int main(void) {
    test_charging_output_and_retained_observation();
    assert(sync_sends == sends && async_sends == 0);
    test_observation_does_not_change_transfer();
    puts("Battery output probe and real QMK SPI encoding passed");
}
