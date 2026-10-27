// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_watchdog.h"

// Westberry EN_RM2905025_WB32FQ95xx_V01: sections 5.4.9/10 (p65),
// 7.2.12 (p76), 7.3.11/16 (pp80/83), 11.4.6/7 (p117), 18 (pp255-257),
// and 27.4.2 (pp387-388). DBGMCU_CR survives software and watchdog resets.
enum {
    IWDG_BUS_ENABLE   = 4,
    IWDG_CLOCK_ENABLE = 1,
    IWDG_DEBUG_FREEZE = 0x100,
    LSI_ENABLE_READY  = 1,
    IWDG_UPDATING     = 3,
    IWDG_PRESCALER    = 3,
    IWDG_RELOAD       = 2999,
};

static bool wait_bits(const gmk_trial_mmio_t *io, uint32_t address, uint32_t mask, uint32_t expected) {
    // A finite read budget is an engineering bound, not a vendor timing limit.
    // No timer, interrupts, initialized globals, or framework clock are needed.
    for (uint32_t remaining = GMK_TRIAL_WATCHDOG_POLL_LIMIT; remaining; --remaining) {
        if ((io->read(address) & mask) == expected) return true;
    }
    return false;
}

static bool update_bits(const gmk_trial_mmio_t *io, uint32_t address, uint32_t clear, uint32_t set) {
    io->write(address, (io->read(address) & ~clear) | set);
    return (io->read(address) & (clear | set)) == set;
}

bool gmk_trial_watchdog_start(const gmk_trial_mmio_t *io) {
    if (!io || !io->read || !io->write) return false;
    if (!update_bits(io, GMK_TRIAL_WATCHDOG_DEBUG_CR, IWDG_DEBUG_FREEZE, 0) || !update_bits(io, GMK_TRIAL_WATCHDOG_AHBENR0, 0, IWDG_BUS_ENABLE) || !update_bits(io, GMK_TRIAL_WATCHDOG_CLKENR, 0, IWDG_CLOCK_ENABLE)) return false;

    io->write(GMK_TRIAL_WATCHDOG_ANAKEY1, 3);
    io->write(GMK_TRIAL_WATCHDOG_ANAKEY2, 12);
    io->write(GMK_TRIAL_WATCHDOG_LSIENR, io->read(GMK_TRIAL_WATCHDOG_LSIENR) | LSI_ENABLE_READY);
    io->write(GMK_TRIAL_WATCHDOG_ANAKEY1, 0);
    io->write(GMK_TRIAL_WATCHDOG_ANAKEY2, 0);
    if (!(io->read(GMK_TRIAL_WATCHDOG_LSIENR) & LSI_ENABLE_READY) || !wait_bits(io, GMK_TRIAL_WATCHDOG_LSISR, LSI_ENABLE_READY, LSI_ENABLE_READY) || !wait_bits(io, GMK_TRIAL_WATCHDOG_SR, IWDG_UPDATING, 0)) return false;

    io->write(GMK_TRIAL_WATCHDOG_KR, 0x5555);
    io->write(GMK_TRIAL_WATCHDOG_PR, IWDG_PRESCALER);
    io->write(GMK_TRIAL_WATCHDOG_RLR, IWDG_RELOAD);
    if (!wait_bits(io, GMK_TRIAL_WATCHDOG_SR, IWDG_UPDATING, 0) || (io->read(GMK_TRIAL_WATCHDOG_PR) & 7U) != IWDG_PRESCALER || (io->read(GMK_TRIAL_WATCHDOG_RLR) & 0xFFFU) != IWDG_RELOAD) {
        io->write(GMK_TRIAL_WATCHDOG_KR, 0); // Relock without reloading or starting.
        return false;
    }

    // At nominal LSI=32kHz, /32 * (2999+1) = 3 seconds. The actual deadline
    // requires hardware measurement. Configure and reload before starting so
    // every failure above can return to Vial with no running trial watchdog.
    io->write(GMK_TRIAL_WATCHDOG_KR, 0xAAAA);
    io->write(GMK_TRIAL_WATCHDOG_KR, 0xCCCC);
    return true;
}

bool gmk_trial_watchdog_recover(const gmk_trial_mmio_t *io) {
    if (!io || !io->read || !io->write) return false;
    if (!update_bits(io, GMK_TRIAL_WATCHDOG_AHBENR0, 0, IWDG_BUS_ENABLE) || !update_bits(io, GMK_TRIAL_WATCHDOG_CLKENR, 0, IWDG_CLOCK_ENABLE) || !wait_bits(io, GMK_TRIAL_WATCHDOG_SR, IWDG_UPDATING, 0)) return false;

    // The inherited watchdog already owns its oscillator. Do not touch analog,
    // LSI or debug controls, and never issue the irreversible start key here.
    // /256 * (4095+1) gives the longest valid period (nominally 32.768s).
    io->write(GMK_TRIAL_WATCHDOG_KR, 0x5555);
    io->write(GMK_TRIAL_WATCHDOG_PR, 6);
    io->write(GMK_TRIAL_WATCHDOG_RLR, 4095);
    if (!wait_bits(io, GMK_TRIAL_WATCHDOG_SR, IWDG_UPDATING, 0) || (io->read(GMK_TRIAL_WATCHDOG_PR) & 7U) != 6 || (io->read(GMK_TRIAL_WATCHDOG_RLR) & 0xFFFU) != 4095) {
        io->write(GMK_TRIAL_WATCHDOG_KR, 0);
        return false;
    }
    return gmk_trial_watchdog_feed(io);
}

bool gmk_trial_watchdog_feed(const gmk_trial_mmio_t *io) {
    if (!io || !io->write) return false;
    io->write(GMK_TRIAL_WATCHDOG_KR, 0xAAAA);
    return true;
}
