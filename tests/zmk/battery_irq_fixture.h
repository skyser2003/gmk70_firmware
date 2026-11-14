/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "battery_fixture.h"
void (*fake_uart_isr)(const void *);
static unsigned notifications;
void            gmk_service_notify(void) {
    ++notifications;
}
void gmk_scan_notify(void) {}
int  k_msgq_get(struct k_msgq *q, void *out, int timeout) {
    (void)timeout;
    if (!q->count) return -EAGAIN;
    *(uint8_t *)out = q->bytes[q->head++ % 128];
    --q->count;
    return 0;
}
int k_msgq_put(struct k_msgq *q, const void *in, int timeout) {
    (void)timeout;
    if (q->count == 128) return -ENOSPC;
    q->bytes[q->tail++ % 128] = *(const uint8_t *)in;
    ++q->count;
    return 0;
}
void k_msgq_purge(struct k_msgq *q) {
    q->count = q->head = q->tail = 0;
}
