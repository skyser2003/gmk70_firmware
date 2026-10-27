/*
 * Copyright (C) 2021...2022 Westberry Technology (ChangZhou) Corp., Ltd
 * Copyright (c) 2026 GMK70 contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * RAM operation helper and PRE_OP instructions from the unmodified pinned
 * WB32FQ95xx hal_efl_lld.c. See firmware/zmk/PROVENANCE.md. These instruction
 * words are vendor code, not a new implementation of the opaque calibration.
 */
#pragma once
#include <stdint.h>
#include <cmsis_core.h>

static inline uint32_t gmk_wb32_irq_lock(void) {
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

static inline void gmk_wb32_irq_restore(uint32_t state) {
    __set_PRIMASK(state);
}

static inline void gmk_wb32_flash_preop(void) {
    static const uint32_t pre_op_code[] = {0x4FF0E92D, 0x21034832, 0x210C6281, 0xF8DF62C1, 0x2100C0C4, 0x1000F8CC, 0xF44F4608, 0x1C40767A, 0xDBFC42B0, 0xF8CC2201, 0x20002000, 0x42B01C40, 0x4829DBFC, 0xF0436803, 0x60030380, 0x302C4826, 0xF4436803, 0x60036320, 0x46104691, 0x323C4A22, 0x468A6010, 0x49214608, 0x48216008, 0x0340F8D0, 0x25004F1E, 0x5107F3C0, 0x3BFFF04F, 0x22001F3F, 0x4610465C, 0xEA5F683B, 0xD10678C0, 0xD10142A3, 0xE0002401, 0x44222400, 0x1C40461C, 0xDBF12814, 0xD91B2A02, 0xD9012910, 0xE0003910, 0x480D2100, 0x68021F00, 0x627FF022, 0x5201EA42, 0xF8CC6002, 0x2000A000, 0x42B01C40, 0xF8CCDBFC, 0x20009000, 0x42B01C40, 0x1C6DDBFC, 0xDBD02D05, 0x8FF0E8BD, 0x40010000, 0x40010438, 0x40010C20, 0x4000B804, 0x1FFFF000};
    ((void (*)(void))((uintptr_t)pre_op_code | 1U))();
}

static inline void gmk_wb32_flash_ram_operation(void) {
    /* Volatile automatic storage guarantees the busy loop executes in SRAM.
     * No flash-resident callee or MMIO wrapper runs while FMC is busy. */
    volatile uint32_t ram_code[] = {0xBF006008, 0x06006808, 0x4770D4FC};
    __DSB();
    __ISB();
    ((void (*)(uint32_t, uint32_t))((uintptr_t)ram_code | 1U))(0x00800080U, 0x40017800U);
}
