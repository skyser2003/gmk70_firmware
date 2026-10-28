/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
struct startup_init_entry {
    int (*init)(void);
    int level;
    int priority;
};
#define APPLICATION 3
#define SYS_INIT(fn, init_level, init_priority) const struct startup_init_entry startup_registration = {fn, init_level, init_priority}
