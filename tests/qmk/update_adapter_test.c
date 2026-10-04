// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "transactions.h"
#include "usb_main.h"
#include "wireless.h"
#include "via.h"
#include "update/qmk_update.h"
#include "update/protocol.h"
#include <assert.h>
#include <stdio.h>

static bool             left, master;
static transport_t      transport;
static uint8_t          mode;
static unsigned         flash_writes, rpc_calls, usb_calls, radio_calls, forwarded, locks, resets;
static uint32_t         now;
static slave_callback_t rpc;
static uint8_t          memory[0x40000];
test_usb_driver_t       USB_DRIVER;

const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(memory));
    return memory + address - GMK_FLASH_BASE;
}
bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
    assert(!locks);
    assert(address >= GMK_STAGE_BASE);
    memcpy(memory + address - GMK_FLASH_BASE, data, GMK_PAGE_SIZE);
    ++flash_writes;
    return true;
}
uint32_t gmk_running_size(void) {
    return 1024;
}
bool is_keyboard_left(void) {
    return left;
}
bool is_keyboard_master(void) {
    return master;
}
transport_t get_transport(void) {
    return transport;
}
uint8_t wireless_get_current_devs(void) {
    return mode;
}
void chSysLock(void) {
    assert(locks++ == 0);
}
void chSysUnlock(void) {
    assert(--locks == 0);
}
uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t time) {
    return now - time;
}
void mcu_reset(void) {
    ++resets;
}
void reset_keyboard(void) {
    assert(false);
}
void wait_ms(uint16_t milliseconds) {
    assert(false);
}
void transaction_register_rpc(int8_t id, slave_callback_t handler) {
    assert(id == GMK_UPDATE_RPC);
    rpc = handler;
}
bool transaction_rpc_exec(int8_t id, uint8_t in_size, const void *in, uint8_t out_size, void *out) {
    assert(id == GMK_UPDATE_RPC && in_size == 32 && out_size == 32);
    ++rpc_calls;
    return false;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++usb_calls;
}
void md_send_raw(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++radio_calls;
}
void raw_hid_receive(uint8_t *data, uint8_t length) {
    ++forwarded;
    assert(!via_command_kb(data, length));
}
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    assert(false);
}
void md_receive_raw_cb(uint8_t *data, uint8_t length);

static void fixture(void) {
    left = master    = true;
    transport        = TRANSPORT_USB;
    mode             = DEVS_USB;
    USB_DRIVER.state = USB_ACTIVE;
    now              = 100;
    flash_writes = rpc_calls = usb_calls = radio_calls = forwarded = locks = resets = 0;
    gmk_update_init();
}

static void begin(uint8_t packet[32], uint8_t target) {
    memset(packet, 0, 32);
    packet[0] = GMK_UPDATE_COMMAND;
    packet[1] = 0x70;
    packet[2] = 1;
    packet[3] = target;
    packet[4] = GMK_BEGIN;
    packet[5] = 12;
    gmk_write_u32(packet + 8, 42);
    gmk_write_u32(packet + 16, 1024);
    gmk_write_u32(packet + 24, 1);
}

int main(void) {
    uint8_t packet[32], response[32];
    fixture();
    for (unsigned i = 0; i < 1000; ++i) {
        now += 1000;
        gmk_update_task();
    }
    assert(!gmk_update_active() && !flash_writes && !rpc_calls && !resets);
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        fixture();
        if (invalid == 0) left = false;
        if (invalid == 1) master = false;
        if (invalid == 2) transport = TRANSPORT_WLS;
        if (invalid == 3) USB_DRIVER.state = 0;
        if (invalid == 4) mode = DEVS_BT1;
        begin(packet, 0);
#ifdef GMK70_DEBUG_USB_BOOTLOADER
        assert(via_command_kb(packet, 32));
#else
        assert(!via_command_kb(packet, 32));
#endif
        gmk_update_task();
        assert(!flash_writes && !rpc_calls && !resets);
    }
    fixture();
    begin(packet, 0);
#ifdef GMK70_DEBUG_USB_BOOTLOADER
    assert(via_command_kb(packet, 32));
    assert(packet[5] == GMK_BUSY && !flash_writes && gmk_update_active());
    gmk_update_task();
    assert(flash_writes == 1 && !resets);
    now += 10001;
    gmk_update_task();
    assert(!gmk_update_active());
#else
    assert(!via_command_kb(packet, 32) && !flash_writes);
#endif
    fixture();
    begin(packet, 0);
    md_receive_raw_cb(packet, 32);
    gmk_update_task();
#ifdef GMK70_DEBUG_USB_BOOTLOADER
    assert(radio_calls == 1 && !forwarded);
#else
    assert(forwarded == 1 && !radio_calls);
#endif
    assert(!flash_writes && !rpc_calls && !resets);
    fixture();
    left = master = false;
    begin(packet, 1);
    rpc(32, packet, 32, response);
    assert(response[5] == GMK_BUSY && !flash_writes);
    // Polling before the worker runs must identify the pending right-side
    // command, not echo a stale left/default response from before boot.
    uint8_t status[32] = {GMK_UPDATE_COMMAND, 0x70, 1, 1, GMK_STATUS};
    rpc(32, status, 32, response);
    assert(response[3] == 1 && response[4] == GMK_BEGIN && response[5] == GMK_BUSY);
    gmk_update_task();
    assert(flash_writes == 1 && !locks);
    for (unsigned role = 0; role < 2; ++role) {
        fixture();
        left   = role == 0;
        master = role == 1;
        begin(packet, 1);
        rpc(32, packet, 32, response);
        gmk_update_task();
        assert(response[5] == GMK_UNAVAILABLE && !flash_writes);
    }
    puts("Updater adapter: no idle traffic, debug-only USB, radio/role gates, asynchronous flash and timeout passed");
}
