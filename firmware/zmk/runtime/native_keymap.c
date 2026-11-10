/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zmk/behavior.h>
#include <zmk/keymap.h>
/* Non-LTO public ABI thunks; all keymap logic remains in pinned ZMK. */
extern __typeof__(zmk_keymap_set_layer_binding_at_idx) gmk_native_keymap_set_layer_binding_at_idx;
int                                                    zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t index, const struct zmk_behavior_binding binding) {
    return gmk_native_keymap_set_layer_binding_at_idx(layer, index, binding);
}
extern __typeof__(zmk_keymap_discard_changes) gmk_native_keymap_discard_changes;
int                                           zmk_keymap_discard_changes(void) {
    return gmk_native_keymap_discard_changes();
}
extern __typeof__(zmk_keymap_reset_settings) gmk_native_keymap_reset_settings;
int                                          zmk_keymap_reset_settings(void) {
    return gmk_native_keymap_reset_settings();
}
extern __typeof__(zmk_keymap_add_layer) gmk_native_keymap_add_layer;
int                                     zmk_keymap_add_layer(void) {
    return gmk_native_keymap_add_layer();
}
extern __typeof__(zmk_keymap_remove_layer) gmk_native_keymap_remove_layer;
int                                        zmk_keymap_remove_layer(zmk_keymap_layer_index_t index) {
    return gmk_native_keymap_remove_layer(index);
}
extern __typeof__(zmk_keymap_move_layer) gmk_native_keymap_move_layer;
int                                      zmk_keymap_move_layer(zmk_keymap_layer_index_t start, zmk_keymap_layer_index_t destination) {
    return gmk_native_keymap_move_layer(start, destination);
}
extern __typeof__(zmk_keymap_restore_layer) gmk_native_keymap_restore_layer;
int                                         zmk_keymap_restore_layer(zmk_keymap_layer_id_t id, zmk_keymap_layer_index_t index) {
    return gmk_native_keymap_restore_layer(id, index);
}
