/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "metadata.h"
#include "metadata_rows.h"
#include <dt-bindings/zmk/gmk70_controls.h>
#include "../radio/core.h"
#include "../lighting/battery_view.h"
#include <zephyr/init.h>

/* Enum rows repeat pointer/value/type/zero padding. Initialize them in BSS
 * from compact name tables, retaining flash space for the fixed updater slot.
 * No allocation, new thread or settings write is involved. */
#define GMK_NAME(value, text) text,
static char *const control_names[] = {GMK70_BASE_CONTROL_OPTIONS(GMK_NAME) GMK70_EXTRA_CONTROL_OPTIONS(GMK_NAME)};
static char *const sleep_names[]   = {GMK70_SLEEP_OPTIONS(GMK_NAME)};
static char *const radio_names[]   = {GMK70_RADIO_OPTIONS(GMK_NAME)};
static char *const battery_names[] = {GMK70_BATTERY_OPTIONS(GMK_NAME)};
#undef GMK_NAME
static struct behavior_parameter_value_metadata control_values[16], sleep_values[8], radio_values[7], battery_values[2];
static struct behavior_parameter_metadata_set   sets[4];
struct behavior_parameter_metadata              gmk70_menu_metadata[4];
static const struct group {
    struct behavior_parameter_value_metadata *values;
    char *const                              *names;
    uint8_t                                   count, first;
} groups[] = {
    {control_values, control_names, 16, 0},
    {sleep_values, sleep_names, 8, 0},
    {radio_values, radio_names, 7, GMK_HOST_USB},
    {battery_values, battery_names, 2, GMK_BATTERY_LEVEL},
};
BUILD_ASSERT(GMK70_RECOVERY_SETTINGS_RESET == 15 && GMK_HOST_DONGLE == 6 && GMK_BATTERY_CHARGING == GMK_BATTERY_LEVEL + 1);
BUILD_ASSERT(CONFIG_KERNEL_INIT_PRIORITY_DEFAULT > 30);
int gmk70_menu_metadata_init(void) {
    for (unsigned group = 0; group < ARRAY_SIZE(groups); ++group) {
        const struct group *g = &groups[group];
        for (uint8_t i = 0; i < g->count; ++i) {
            g->values[i].display_name = g->names[i];
            g->values[i].value        = (uint32_t)i + g->first;
            g->values[i].type         = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE;
        }
        sets[group].param1_values           = g->values;
        sets[group].param1_values_len       = g->count;
        gmk70_menu_metadata[group].sets     = &sets[group];
        gmk70_menu_metadata[group].sets_len = 1;
    }
    return 0;
}
SYS_INIT(gmk70_menu_metadata_init, POST_KERNEL, 30);
