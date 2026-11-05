// Copyright 2024 Su (@isuua)
// Copyright (c) 2026 GMK70 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "wb32_stop.h"

/* Unmodified vendor PRE_LP/POST_LP words, moved from lpwr_wb32.c. These
 * executable Thumb sequences remain one maintained copy shared by QMK/ZMK. */
// clang-format off
static const uint32_t pre_lp_code[] = {553863175u, 554459777u, 1208378049u, 4026624001u, 688390415u, 554227969u, 3204472833u, 1198571264u, 1073807360u, 1073808388u};
static const uint32_t post_lp_code[] = {553863177u, 554459777u, 1208509121u, 51443856u, 4026550535u, 1745485839u, 3489677954u, 536895496u, 673389632u, 1198578684u, 1073807360u, 536866816u, 1073808388u};
// clang-format on

static uint32_t read32(uintptr_t address) {
    return *(volatile const uint32_t *)address;
}
static void write32(uint32_t value, uintptr_t address) {
    *(volatile uint32_t *)address = value;
}
static void prepare(void) {
    ((void (*)(void))((uintptr_t)pre_lp_code | 1U))();
}
static void wait_for_interrupt(void) {
    __asm__ volatile("wfi" ::: "memory");
}
static void resume(void) {
    ((void (*)(void))((uintptr_t)post_lp_code | 1U))();
}

void wb32_stop_hardware(void) {
    static const struct wb32_stop_ops ops = {read32, write32, prepare, wait_for_interrupt, resume};
    wb32_stop_enter(&ops);
}
