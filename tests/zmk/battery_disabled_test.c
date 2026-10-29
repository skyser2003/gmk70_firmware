/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
    struct gmk_battery_status status = {.percentage = 100, .valid = true, .cable = true, .full = true};
    gmk_battery_poll();
    gmk_battery_get(&status);
    assert(!status.valid && !status.cable && !status.full && status.percentage == 0);
    assert(status.age_ms == 0 && status.queries == 0 && status.replies == 0 && status.errors == 0);
    return 0;
}
