// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "sleep_log.h"
#include "flash_spi.h"
#include "spi_master.h"
#include <assert.h>
#include <stdio.h>

static uint32_t id;
static unsigned cursor, calls, depth;
void            sleep_log_test_settings_io(void);
void            chMtxLock(mutex_t *mutex) {
    assert(!mutex->locked);
    mutex->locked = true;
    ++depth;
}
void chMtxUnlock(mutex_t *mutex) {
    assert(mutex->locked && depth == 1);
    mutex->locked = false;
    --depth;
}
void           flash_init(void) {}
flash_status_t flash_is_busy(void) {
    return FLASH_STATUS_SUCCESS;
}
bool spi_start(uint16_t pin, bool lsb, uint8_t mode, uint16_t divisor) {
    assert(pin == 12 && !lsb && mode == 0 && divisor == 8);
    cursor = 0;
    return true;
}
spi_status_t spi_write(uint8_t byte) {
    assert(byte == 0x9F);
    return 0;
}
spi_status_t spi_read(void) {
    return (id >> (8 * (2 - cursor++))) & 255;
}
void spi_stop(void) {
    assert(cursor == 3);
}
flash_status_t flash_read_range(uint32_t address, void *data, size_t size) {
    (void)address;
    (void)data;
    (void)size;
    assert(depth == 1);
    ++calls;
    return 0;
}
flash_status_t flash_write_range(uint32_t address, const void *data, size_t size) {
    (void)address;
    (void)data;
    (void)size;
    assert(depth == 1);
    ++calls;
    return 0;
}
flash_status_t flash_erase_sector(uint32_t address) {
    (void)address;
    assert(depth == 1);
    ++calls;
    return 0;
}
flash_status_t flash_erase_block(uint32_t address) {
    (void)address;
    assert(depth == 1);
    ++calls;
    return 0;
}
int main(void) {
    const uint32_t accepted[] = {0xC22013, 0xEF4013, 0xC84017, 0x856018, 0x5E3213};
    const uint32_t rejected[] = {0, 0xFFFFFF, 0xEF4010, 0xEFAA21, 0xC22019, 0x014013, 0x5E3210, 0x5E3212, 0x5E3214, 0x5E4013, 0x5E6013, 0xC23213};
    for (unsigned i = 0; i < sizeof(accepted) / sizeof(*accepted); ++i) {
        id = accepted[i];
        assert(sleep_log_storage_init() && sleep_log_storage_id() == id);
    }
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        id = rejected[i];
        assert(!sleep_log_storage_init());
    }
    uint8_t data[16];
    assert(!sleep_log_storage_read(SLEEP_LOG_BASE - 1, data, 1));
    assert(!sleep_log_storage_write(SLEEP_LOG_BASE + SLEEP_LOG_SIZE - 1, data, 2));
    assert(!sleep_log_storage_erase(SLEEP_LOG_BASE + 1));
    assert(!calls);
    assert(sleep_log_storage_read(SLEEP_LOG_BASE, data, sizeof(data)));
    assert(sleep_log_storage_write(SLEEP_LOG_BASE, data, sizeof(data)));
    assert(sleep_log_storage_erase(SLEEP_LOG_BASE));
    assert(sleep_log_storage_erase(SLEEP_LOG_BASE + 4096));
    assert(calls == 4 && !depth);
    // Settings operations acquire the same lock as the journal operations.
    sleep_log_test_settings_io();
    assert(calls == 7 && !depth);
    puts("PASS: NOR identity/capacity, isolated journal bounds and shared settings lock");
}
