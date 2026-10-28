/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
struct power_init_entry {
    int (*init)(void);
    int level;
    int priority;
};
#define APPLICATION 3
#define SYS_INIT(fn, level, priority) const struct power_init_entry power_registration = {fn, level, priority}
