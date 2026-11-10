/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <drivers/behavior.h>
#include <zmk/behavior.h>
/* Non-LTO public ABI preserves the settings-load compatibility hook. */
extern __typeof__(zmk_behavior_find_behavior_name_from_local_id) gmk_native_behavior_find_behavior_name_from_local_id;
const char                                                      *zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id) {
    return gmk_native_behavior_find_behavior_name_from_local_id(id);
}
