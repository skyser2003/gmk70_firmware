/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "metadata.h"
#include "metadata_rows.h"
#include <dt-bindings/zmk/gmk70_controls.h>
#include "../radio/core.h"
#include "../lighting/battery_view.h"
#include <zephyr/init.h>
#include <string.h>

/* Enum rows repeat pointer/value/type/zero padding. Initialize them in BSS
 * from compact name tables, retaining flash space for the fixed updater slot.
 * No allocation, new thread or settings write is involved. */
#define GMK_NAME(value, text) text "\0"
static const char names[] = GMK70_BASE_CONTROL_OPTIONS(GMK_NAME) GMK70_EXTRA_CONTROL_OPTIONS(GMK_NAME) GMK70_SLEEP_OPTIONS(GMK_NAME) GMK70_RADIO_OPTIONS(GMK_NAME) GMK70_BATTERY_OPTIONS(GMK_NAME);
#undef GMK_NAME
static struct behavior_parameter_value_metadata values[33];
static struct behavior_parameter_metadata_set   sets[4];
struct behavior_parameter_metadata              gmk70_menu_metadata[4];
static const uint8_t                            counts[] = {16, 8, 7, 2};
BUILD_ASSERT(GMK70_RECOVERY_SETTINGS_RESET == 15 && GMK_HOST_USB == 0 && GMK_HOST_DONGLE == 6 && GMK_BATTERY_LEVEL == 0 && GMK_BATTERY_CHARGING == 1);
BUILD_ASSERT(CONFIG_KERNEL_INIT_PRIORITY_DEFAULT > 30);
int gmk70_menu_metadata_init(void) {
    struct behavior_parameter_value_metadata *row  = values;
    const char                               *name = names;
    for (unsigned group = 0; group < ARRAY_SIZE(counts); ++group) {
        uint8_t count = counts[group];
        for (uint8_t i = 0; i < count; ++i) {
            /* Native metadata's char* describes read-only display text. */
            row[i].display_name = (char *)name;
            name += strlen(name) + 1;
            row[i].value = i;
            row[i].type  = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE;
        }
        sets[group].param1_values           = row;
        sets[group].param1_values_len       = count;
        gmk70_menu_metadata[group].sets     = &sets[group];
        gmk70_menu_metadata[group].sets_len = 1;
        row += count;
    }
    return 0;
}
SYS_INIT(gmk70_menu_metadata_init, POST_KERNEL, 30);
