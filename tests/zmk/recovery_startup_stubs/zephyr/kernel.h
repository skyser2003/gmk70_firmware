/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct k_work {
    void (*handler)(struct k_work *);
};
#define K_WORK_DEFINE(name, callback) static struct k_work name = {.handler = callback}
int      k_work_submit(struct k_work *work);
uint32_t k_uptime_get_32(void);
bool     k_is_in_isr(void);
