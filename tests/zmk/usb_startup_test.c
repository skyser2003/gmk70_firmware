/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <zephyr/drivers/usb/usb_dc.h>
#include <zephyr/init.h>

/* The real SYS_INIT invocation supplies this registration when CMake selects
 * the startup source. No hook is called by name or fabricated by the fixture. */
extern const struct startup_init_entry startup_registration __attribute__((weak));
unsigned int                           startup_irq_depth;
void (*fake_usb_isr)(const void *);
static uint8_t      registers[0x30];
static unsigned int now_ms, disconnect_ms, sleeps, connects;
static bool         disconnected;

void wb32_usb_disconnect(void) {
    disconnected  = true;
    disconnect_ms = now_ms;
}
void wb32_usb_clock_enable(void) {
    if (EXPECT_WIRED) {
        /* This is the actual driver's first electrical connect boundary. */
        assert(disconnected && "wired startup must disconnect before attach");
        assert(now_ms - disconnect_ms >= 50 && "D+ must remain low for 50 ms");
    }
    disconnected = false;
    ++connects;
}
void wb32_usb_clock_disable(void) {}

int32_t k_msleep(int32_t milliseconds) {
    assert(startup_irq_depth == 0 && "startup delay must not hold IRQs disabled");
    assert(disconnected && connects == 0);
    assert(milliseconds >= 0);
    ++sleeps;
    now_ms += milliseconds;
    return 0;
}

uint8_t sys_read8(uintptr_t address) {
    assert(address >= 0x40014000UL && address < 0x40014030UL);
    return registers[address - 0x40014000UL];
}
void sys_write8(uint8_t value, uintptr_t address) {
    assert(address >= 0x40014000UL && address < 0x40014030UL);
    registers[address - 0x40014000UL] = value;
}

int main(void) {
    /* Model only the ordered APPLICATION init boundary: the registered hook
     * must finish before service 90 / keyboard 95 / USB enable 96. */
    if (&startup_registration) {
        assert(EXPECT_WIRED && "legacy boards must not register the startup hook");
        assert(startup_registration.level == APPLICATION);
        assert(startup_registration.priority == 89);
        assert(startup_registration.init() == 0);
    }
    assert(usb_dc_attach() == 0);
    assert(connects == 1 && startup_irq_depth == 0 && fake_usb_isr);
    if (EXPECT_WIRED) {
        assert(sleeps == 1 && now_ms == 50);
    } else {
        assert(sleeps == 0 && now_ms == 0 && !&startup_registration);
    }
    puts(EXPECT_WIRED ? "Wired USB startup: stable 50 ms disconnect before real attach passed" : "Legacy USB startup: no hook or delay passed");
    return 0;
}
