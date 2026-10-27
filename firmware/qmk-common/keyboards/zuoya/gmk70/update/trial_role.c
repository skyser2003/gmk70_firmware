// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_role.h"

// WB32 reference manual 7.3.7 (p79), 7.3.14 (p82), 8.4 (pp99-104).
// GPIOB is APB1: its input clock AND bus-matrix/peripheral gates are required.
enum {
    RCC_APB1PRE    = 0x40010C1C,
    RCC_APB1ENR    = 0x40010C48,
    APB1_INPUT     = 1U << 7,
    ROLE_CLOCKS    = (1U << 15) | (1U << 6),
    GPIOB_MODER    = 0x40000400,
    GPIOB_PUPDR    = 0x4000040C,
    GPIOB_IDR      = 0x40000410,
    GPIOB_CFGMSK   = 0x40000430,
    ROLE_PIN       = 1U << 9,
    ROLE_MODE_MASK = 3U << 18,
};

bool gmk_trial_role_read(const gmk_trial_mmio_t *io, uint32_t *role) {
    if (!io || !io->read || !io->write || !role) return false;
    uint32_t pre = io->read(RCC_APB1PRE);
    io->write(RCC_APB1PRE, pre | APB1_INPUT);
    if (!(io->read(RCC_APB1PRE) & APB1_INPUT)) {
        io->write(RCC_APB1PRE, pre);
        return false;
    }
    uint32_t clocks = io->read(RCC_APB1ENR);
    io->write(RCC_APB1ENR, clocks | ROLE_CLOCKS);
    bool     ready = (io->read(RCC_APB1ENR) & ROLE_CLOCKS) == ROLE_CLOCKS;
    uint32_t input = 0;
    if (ready) {
        uint32_t mask  = io->read(GPIOB_CFGMSK);
        uint32_t mode  = io->read(GPIOB_MODER);
        uint32_t pulls = io->read(GPIOB_PUPDR);
        io->write(GPIOB_CFGMSK, ~ROLE_PIN);
        io->write(GPIOB_MODER, 0);
        io->write(GPIOB_PUPDR, 0);
        ready = ((io->read(GPIOB_MODER) | io->read(GPIOB_PUPDR)) & ROLE_MODE_MASK) == 0;
        if (ready) {
            for (volatile unsigned remaining = 1024; remaining; --remaining) {
            }
            input = io->read(GPIOB_IDR) & ROLE_PIN;
            for (volatile unsigned remaining = 32; remaining; --remaining) {
            }
            ready = input == (io->read(GPIOB_IDR) & ROLE_PIN);
        }
        // CFGMSK selects B9 only; writing saved packed register values restores
        // that pad without disturbing any unrelated GPIO configuration.
        io->write(GPIOB_MODER, mode);
        io->write(GPIOB_PUPDR, pulls);
        io->write(GPIOB_CFGMSK, mask);
    }
    // No GPIO access is permitted after removing either clock dependency.
    io->write(RCC_APB1ENR, clocks);
    io->write(RCC_APB1PRE, pre);
    if (ready) *role = input ? 0U : 1U;
    return ready;
}
