// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "transactions.h"
#include "usb_main.h"
#include "wireless.h"
#include "via.h"
#include "update/qmk_update.h"
#include "update/protocol.h"
#include "update/eeprom_query.h"
#include <assert.h>
#include <stdio.h>

static bool             left, master;
static transport_t      transport;
static uint8_t          mode;
static unsigned         flash_writes, rpc_calls, usb_calls, radio_calls, forwarded, locks, resets;
static unsigned         eeprom_reads;
static bool             relay;
static uint32_t         now;
static slave_callback_t rpc;
static uint8_t          memory[0x40000];
static uint8_t          logical_eeprom[4096];
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
static unsigned log_reads;
uint32_t        sleep_log_status(void) {
    return 0xC2201303;
}
bool sleep_log_read(uint32_t offset, void *data, size_t size) {
    assert(!locks && offset < 8192 && size <= 8192 - offset);
    ++log_reads;
    memset(data, 0xFF, size);
    return true;
}
#endif
test_usb_driver_t USB_DRIVER;

void eeprom_read_block(void *destination, const void *source, size_t count) {
    uintptr_t offset = (uintptr_t)source;
    assert(!locks && offset < sizeof(logical_eeprom) && count <= sizeof(logical_eeprom) - offset);
    memcpy(destination, logical_eeprom + offset, count);
    ++eeprom_reads;
}

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
    if (!relay) return false;
    // Exercise the actual USB bridge and registered peer callback. The fixture
    // models one receiver's state; switch roles only at the RPC boundary.
    bool old_left = left, old_master = master;
    left = master = false;
    rpc(in_size, in, out_size, out);
    left   = old_left;
    master = old_master;
    return true;
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
    eeprom_reads                                                                    = 0;
    relay                                                                           = false;
    for (unsigned i = 0; i < sizeof(logical_eeprom); ++i)
        logical_eeprom[i] = (uint8_t)(i * 13 + i / 256);
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

static void query(uint8_t packet[32], uint8_t target, uint8_t op, uint32_t token, uint32_t offset, uint8_t count) {
    memset(packet, 0, 32);
    packet[0] = GMK_UPDATE_COMMAND;
    packet[1] = 0x70;
    packet[2] = 1;
    packet[3] = target;
    packet[4] = op;
    packet[5] = count;
    gmk_write_u32(packet + 8, token);
    gmk_write_u32(packet + 12, offset);
}

static void test_eeprom_peer(void) {
    fixture();
    left = master = false;
    uint8_t packet[32], response[32];
    uint8_t status[32] = {GMK_UPDATE_COMMAND, 0x70, 1, 1, GMK_STATUS};
    query(packet, 1, GMK_EEPROM_INFO, 100, 0, 0);
    rpc(32, packet, 32, response);
    assert(response[5] == GMK_BUSY && !eeprom_reads);
    rpc(32, status, 32, response);
    assert(response[4] == GMK_EEPROM_INFO && response[5] == GMK_BUSY && gmk_read_u32(response + 8) == 100);
    gmk_update_task();
    rpc(32, status, 32, response);
    assert(response[5] == GMK_OK && response[6] == 0 && response[7] == 0);
    assert(gmk_read_u32(response + 8) == 100 && gmk_read_u32(response + 12) == 0);
    assert(gmk_read_u32(response + 16) == 4096 && gmk_read_u32(response + 20) == gmk_crc32(logical_eeprom, 4096));
    assert(gmk_read_u32(response + 24) == GMK_EEPROM_ABI && gmk_read_u32(response + 28) == GMK_EEPROM_LAYOUT);
    for (unsigned offset = 0; offset < sizeof(logical_eeprom); offset += 16) {
        unsigned before = eeprom_reads;
        query(packet, 1, GMK_EEPROM_READ, 101 + offset, offset, 16);
        rpc(32, packet, 32, response);
        assert(response[5] == GMK_BUSY && eeprom_reads == before);
        gmk_update_task();
        rpc(32, status, 32, response);
        assert(response[5] == GMK_OK && response[6] == 16 && response[7] == 0);
        assert(gmk_read_u32(response + 8) == 101 + offset && gmk_read_u32(response + 12) == offset);
        assert(memcmp(response + 16, logical_eeprom + offset, 16) == 0);
    }
    assert(!flash_writes && !resets && !locks && !gmk_update_active());
    unsigned before = eeprom_reads;
    for (unsigned i = 0; i < 1000; ++i)
        gmk_update_task();
    assert(eeprom_reads == before);
}

static void test_eeprom_validation(void) {
    fixture();
    uint8_t        packet[32], response[32];
    const uint32_t invalid_offsets[] = {4096, 4095, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        query(packet, 1, GMK_EEPROM_READ, 1, invalid_offsets[i], 2);
        gmk_eeprom_query(packet, GMK_OK, response);
        assert(response[5] == GMK_BAD_OFFSET && !eeprom_reads);
    }
    for (unsigned field = 0; field < 25; ++field) {
        query(packet, 1, GMK_EEPROM_READ, 1, 0, 16);
        if (field == 0) packet[0] = 0;
        if (field == 1) packet[1] = 0;
        if (field == 2) packet[2] = 2;
        if (field == 3) packet[3] = 2;
        if (field == 4) packet[4] = 255;
        if (field == 5) packet[5] = 0;
        if (field == 6) packet[5] = 17;
        if (field == 7) packet[6] = 1;
        if (field == 8) packet[7] = 1;
        if (field >= 9) packet[16 + field - 9] = 1;
        assert(!gmk_packet_valid(packet));
        gmk_eeprom_query(packet, GMK_OK, response);
        assert(response[5] == GMK_BAD_REQUEST && !eeprom_reads);
    }
    query(packet, 1, GMK_EEPROM_INFO, 0, 0, 0);
    assert(!gmk_packet_valid(packet));
    query(packet, 1, GMK_EEPROM_INFO, 1, 1, 0);
    assert(!gmk_packet_valid(packet));
    query(packet, 1, GMK_EEPROM_INFO, 1, 0, 1);
    assert(!gmk_packet_valid(packet));
    query(packet, 1, GMK_EEPROM_READ, 1, 4095, 1);
    gmk_eeprom_query(packet, GMK_OK, response);
    assert(response[5] == GMK_OK && response[6] == 1 && response[16] == logical_eeprom[4095]);
    for (unsigned i = 17; i < 32; ++i)
        assert(response[i] == 0);
    assert(!flash_writes && !resets);
}

static void test_eeprom_update_and_radio_gates(void) {
    fixture();
    left = master = false;
    uint8_t packet[32], response[32];
    uint8_t status[32] = {GMK_UPDATE_COMMAND, 0x70, 1, 1, GMK_STATUS};
    begin(packet, 1);
    rpc(32, packet, 32, response);
    gmk_update_task();
    assert(flash_writes == 1);
    now += 10001;
    query(packet, 1, GMK_EEPROM_INFO, 5, 0, 0);
    rpc(32, packet, 32, response);
    gmk_update_task();
    rpc(32, status, 32, response);
    assert(response[5] == GMK_BAD_STATE && gmk_read_u32(response + 8) == 5 && !eeprom_reads && flash_writes == 1);
    gmk_update_task();
    assert(!gmk_update_active()); // The rejected read did not extend the update.
    mode = DEVS_BT1;
    query(packet, 1, GMK_EEPROM_READ, 6, 774, 16);
    rpc(32, packet, 32, response);
    gmk_update_task();
    rpc(32, status, 32, response);
    assert(response[5] == GMK_UNAVAILABLE && !eeprom_reads && !resets);
    fixture();
    query(packet, 1, GMK_EEPROM_READ, 7, 774, 16);
    md_receive_raw_cb(packet, 32);
    gmk_update_task();
    assert(!eeprom_reads && !flash_writes && !rpc_calls);
#ifdef GMK70_DEBUG_USB_BOOTLOADER
    assert(!forwarded && radio_calls == 1);
#endif
}

static void test_eeprom_usb(void) {
    fixture();
    uint8_t packet[32];
    query(packet, 0, GMK_EEPROM_READ, 50, 38, 16);
#ifdef GMK70_DEBUG_USB_BOOTLOADER
    assert(via_command_kb(packet, 32) && packet[5] == GMK_BUSY && !eeprom_reads);
    gmk_update_task();
    query(packet, 0, GMK_STATUS, 0, 0, 0);
    assert(via_command_kb(packet, 32) && packet[5] == GMK_OK && packet[7] == 3);
    assert(memcmp(packet + 16, logical_eeprom + 38, 16) == 0);
    fixture();
    query(packet, 1, GMK_EEPROM_INFO, 51, 0, 0);
    assert(via_command_kb(packet, 32) && packet[5] == GMK_UNAVAILABLE && rpc_calls == 1 && !eeprom_reads);
    relay = true;
    query(packet, 1, GMK_EEPROM_READ, 52, 4080, 16);
    assert(via_command_kb(packet, 32) && packet[5] == GMK_BUSY && !eeprom_reads);
    left = master = false;
    gmk_update_task();
    left = master = true;
    query(packet, 1, GMK_STATUS, 0, 0, 0);
    assert(via_command_kb(packet, 32) && packet[5] == GMK_OK && packet[7] == 0);
    assert(gmk_read_u32(packet + 8) == 52 && memcmp(packet + 16, logical_eeprom + 4080, 16) == 0);
#else
    assert(!via_command_kb(packet, 32));
    gmk_update_task();
    assert(!eeprom_reads && !rpc_calls);
#endif
    assert(!flash_writes && !resets);
}

static void test_persistent_log_service(void) {
    fixture();
    left = master = false;
    uint8_t packet[32], response[32];
    uint8_t status[32] = {GMK_UPDATE_COMMAND, 0x70, 1, 1, GMK_STATUS};
    query(packet, 1, GMK_SLEEP_LOG_INFO, 100, 0, 0);
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
    log_reads = 0;
#endif
    rpc(32, packet, 32, response);
    assert(response[5] == GMK_BUSY && !flash_writes);
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
    assert(!log_reads); // Callback only queues; NOR reads run in housekeeping.
#endif
    gmk_update_task();
    rpc(32, status, 32, response);
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
    assert(response[5] == GMK_OK && log_reads == 512 && gmk_read_u32(response + 16) == 8192);
    query(packet, 1, GMK_SLEEP_LOG_READ, 101, 8176, 16);
    rpc(32, packet, 32, response);
    assert(log_reads == 512);
    gmk_update_task();
    rpc(32, status, 32, response);
    assert(response[5] == GMK_OK && response[6] == 16 && log_reads == 513);
    mode = DEVS_BT1;
    query(packet, 1, GMK_SLEEP_LOG_INFO, 102, 0, 0);
    rpc(32, packet, 32, response);
    gmk_update_task();
    rpc(32, status, 32, response);
    assert(response[5] == GMK_UNAVAILABLE && log_reads == 513);
    for (unsigned i = 0; i < 1000; ++i)
        gmk_update_task();
    assert(log_reads == 513);
#else
    assert(response[5] == GMK_UNAVAILABLE);
#endif
    assert(!flash_writes && !resets && !eeprom_reads);
}

int main(void) {
    uint8_t packet[32], response[32];
    fixture();
    for (unsigned i = 0; i < 1000; ++i) {
        now += 1000;
        gmk_update_task();
    }
    assert(!gmk_update_active() && !flash_writes && !rpc_calls && !resets && !eeprom_reads);
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
    test_eeprom_peer();
    test_eeprom_validation();
    test_eeprom_update_and_radio_gates();
    test_eeprom_usb();
    test_persistent_log_service();
    puts("Updater adapter: USB/peer EEPROM reads, complete logical storage, CRC, bounds, stale-token echo, update/radio/role gates and no idle traffic passed");
}
