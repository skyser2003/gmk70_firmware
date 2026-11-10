/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#define K_FOREVER (-1)
#define K_MSEC(value) ((int)(value))
struct k_sem {
    unsigned count, maximum;
};
#define K_SEM_DEFINE(name, initial, max) struct k_sem name = {.count = initial, .maximum = max}
void k_sem_give(struct k_sem *sem);
int  k_sem_take(struct k_sem *sem, int timeout);
struct k_thread {
    int      priority;
    unsigned essential;
};
struct k_thread *k_current_get(void);
void             k_thread_priority_set(struct k_thread *thread, int priority);
