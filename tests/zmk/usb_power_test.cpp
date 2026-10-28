/* SPDX-License-Identifier: Apache-2.0 */
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <zephyr/init.h>
#include "../../firmware/zmk/drivers/gpio/gpio_wb32.c"

/* Follow the authoritative board macro rather than a second pin expectation. */
#define B1 0x11U
#include "../../firmware/qmk-common/keyboards/zuoya/gmk70/config.h"
static constexpr unsigned power_pin = USB_POWER_EN_PIN & 15U;
static_assert(USB_POWER_EN_PIN >> 4U == 1U, "USB pull-up power is GPIOB");

static uint32_t                   registers[13], initial[13];
static unsigned                   writes, low_sequence, output_sequence, clocks;
unsigned                          power_irq_depth;
static wb32_gpio                  port         = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}, {11}, {12}};
static const wb32_gpio_config     config       = {{0xffff}, &port, 6};
extern "C" const device           power_gpio_b = {&config, &wb32_gpio_api, true};
extern "C" const power_init_entry power_registration __attribute__((weak));

power_register::operator uint32_t() const {
    return registers[index];
}
void power_register::operator=(uint32_t value) {
    ++writes;
    if (index == 12) {
        registers[index] = value;
        return;
    }
    if (index == 6) {
        assert(value == BIT(power_pin + 16) && "only preload B1 low; never pulse high");
        registers[5]     = (registers[5] | (value & 0xffff)) & ~(value >> 16);
        low_sequence     = writes;
        registers[index] = value;
        return;
    }
    /* WB32 CFGMSK protects unrelated pins on direct configuration writes. */
    uint32_t mask  = 0;
    unsigned width = (index == 1) ? 1 : 2;
    for (unsigned pin = 0; pin < 16; ++pin) {
        if (!(registers[12] & BIT(pin))) mask |= ((1U << width) - 1U) << (pin * width);
    }
    if (index == 0) {
        assert(low_sequence && !(registers[5] & BIT(power_pin)));
        output_sequence = writes;
    }
    registers[index] = (registers[index] & ~mask) | (value & mask);
}

void sys_set_bits(uintptr_t address, uint32_t bits) {
    assert(address == WB32_RCC_BASE + 0x48 && bits == (BIT(15) | BIT(6)));
    ++clocks;
}

extern "C" {
void (*fake_usb_isr)(const void *);
int            usb_dc_attach(void);
static uint8_t usb_registers[0x30];
uint8_t        sys_read8(uintptr_t address) {
    assert(address >= 0x40014000UL && address < 0x40014030UL);
    return usb_registers[address - 0x40014000UL];
}
void sys_write8(uint8_t value, uintptr_t address) {
    assert(address >= 0x40014000UL && address < 0x40014030UL);
    usb_registers[address - 0x40014000UL] = value;
}
void wb32_usb_clock_enable(void) {
    if (EXPECT_WIRED) {
        assert(((registers[0] >> (power_pin * 2)) & 3) == 1 && "B1 must be output before USB attach");
        assert(!(registers[5] & BIT(power_pin)) && "USB pull-up power must be enabled low");
        assert(!(registers[1] & BIT(power_pin)) && "B1 must be push-pull");
        assert(low_sequence < output_sequence && power_irq_depth == 0);
    }
}
void wb32_usb_clock_disable(void) {}
void wb32_usb_disconnect(void) {
    assert(false && "no D+ disconnect or 50 ms experiment remains");
}
}

int main(void) {
    /* Begin with B1 input/high and distinctive unrelated pins. */
    for (unsigned i = 0; i < 13; ++i)
        registers[i] = 0xa5a5a5a5U;
    registers[0] &= ~(3U << (power_pin * 2));
    registers[5] |= BIT(power_pin);
    registers[12] = 0xffff;
    std::memcpy(initial, registers, sizeof initial);
    assert(wb32_gpio_init(&power_gpio_b) == 0 && clocks == 1);
    if (&power_registration) {
        assert(EXPECT_WIRED && power_registration.level == APPLICATION && power_registration.priority == 89);
        assert(power_registration.init() == 0);
    }
    assert(usb_dc_attach() == 0);
    if (EXPECT_WIRED) {
        assert(writes == 7 && low_sequence && output_sequence > low_sequence);
        for (unsigned index : {0U, 1U, 2U, 3U, 5U, 11U}) {
            const uint32_t mask = (index == 1 || index == 5) ? BIT(power_pin) : (3U << (power_pin * 2));
            assert((registers[index] & ~mask) == (initial[index] & ~mask));
        }
        assert(registers[8] == initial[8] && registers[9] == initial[9]);
    } else {
        assert(!&power_registration && writes == 0);
        assert(std::memcmp(initial, registers, sizeof initial) == 0);
    }
    std::puts(EXPECT_WIRED ? "Wired B1 power: actual GPIO writes preload low/output before USB attach; other pins preserved" : "Legacy B1 power: no initializer or GPIO writes");
}
