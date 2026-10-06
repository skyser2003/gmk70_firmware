// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "sleep_log.h"
#include "flash_spi.h"
#include "wear_leveling_flash_spi_config.h"
#include "spi_master.h"

_Static_assert(EXTERNAL_FLASH_PAGE_SIZE == SLEEP_LOG_PAGE && EXTERNAL_FLASH_SECTOR_SIZE == SLEEP_LOG_SECTOR, "Unsupported sleep-log flash geometry");
_Static_assert(SLEEP_LOG_BASE >= (WEAR_LEVELING_EXTERNAL_FLASH_BLOCK_OFFSET + WEAR_LEVELING_EXTERNAL_FLASH_BLOCK_COUNT) * EXTERNAL_FLASH_BLOCK_SIZE, "Sleep log overlaps settings erase block");
_Static_assert(SLEEP_LOG_BASE + SLEEP_LOG_SIZE < EXTERNAL_FLASH_SIZE, "Sleep log exceeds configured flash");
static uint32_t jedec;
static mutex_t  flash_mutex = __MUTEX_DATA(flash_mutex);

// The split worker can update saved mode while the main thread saves a log.
// Serialize whole NOR operations, including their WREN/program/busy sequence.
flash_status_t __real_flash_read_range(uint32_t address, void *data, size_t size);
flash_status_t __real_flash_write_range(uint32_t address, const void *data, size_t size);
flash_status_t __real_flash_erase_sector(uint32_t address);
flash_status_t __real_flash_erase_block(uint32_t address);
#define FLASH_WRAP_RANGE(name, qualifier)                                               \
    flash_status_t __wrap_##name(uint32_t address, qualifier void *data, size_t size) { \
        chMtxLock(&flash_mutex);                                                        \
        flash_status_t result = __real_##name(address, data, size);                     \
        chMtxUnlock(&flash_mutex);                                                      \
        return result;                                                                  \
    }
FLASH_WRAP_RANGE(flash_read_range, )
FLASH_WRAP_RANGE(flash_write_range, const)
#define FLASH_WRAP_ERASE(name)                          \
    flash_status_t __wrap_##name(uint32_t address) {    \
        chMtxLock(&flash_mutex);                        \
        flash_status_t result = __real_##name(address); \
        chMtxUnlock(&flash_mutex);                      \
        return result;                                  \
    }
FLASH_WRAP_ERASE(flash_erase_sector)
FLASH_WRAP_ERASE(flash_erase_block)

bool sleep_log_storage_init(void) {
    jedec = 0;
    flash_init();
    if (flash_is_busy() != FLASH_STATUS_SUCCESS || !spi_start(EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN, EXTERNAL_FLASH_SPI_LSBFIRST, EXTERNAL_FLASH_SPI_MODE, EXTERNAL_FLASH_SPI_CLOCK_DIVISOR)) return false;
    bool ok = spi_write(0x9F) >= 0;
    for (unsigned i = 0; i < 3; ++i) {
        spi_status_t byte = spi_read();
        if (byte < 0) ok = false;
        jedec = (jedec << 8) | ((uint8_t)byte);
    }
    spi_stop();
    uint8_t manufacturer = jedec >> 16, type = jedec >> 8, density = jedec;
    // JEDEC density is log2(bytes). Reject absent/unknown and too-small chips.
    // All accepted manufacturers use the existing driver's 0x20 sector erase.
    bool known = manufacturer == 0xC2 || manufacturer == 0xEF || manufacturer == 0xC8 || manufacturer == 0x85 || manufacturer == 0x1C || manufacturer == 0x20 || manufacturer == 0x9D;
    bool nor   = type == 0x20 || type == 0x40 || type == 0x60 || type == 0x30;
    return ok && known && nor && density >= 17 && density <= 24 && (1UL << density) > SLEEP_LOG_BASE + SLEEP_LOG_SIZE;
}
uint32_t sleep_log_storage_id(void) {
    return jedec;
}
static bool bounds(uint32_t address, size_t size) {
    return address >= SLEEP_LOG_BASE && address < SLEEP_LOG_BASE + SLEEP_LOG_SIZE && size <= SLEEP_LOG_BASE + SLEEP_LOG_SIZE - address;
}
bool sleep_log_storage_read(uint32_t address, void *data, size_t size) {
    return bounds(address, size) && flash_read_range(address, data, size) == FLASH_STATUS_SUCCESS;
}
bool sleep_log_storage_write(uint32_t address, const void *data, size_t size) {
    return bounds(address, size) && flash_write_range(address, data, size) == FLASH_STATUS_SUCCESS;
}
bool sleep_log_storage_erase(uint32_t address) {
    return bounds(address, SLEEP_LOG_SECTOR) && !(address % SLEEP_LOG_SECTOR) && flash_erase_sector(address) == FLASH_STATUS_SUCCESS;
}
