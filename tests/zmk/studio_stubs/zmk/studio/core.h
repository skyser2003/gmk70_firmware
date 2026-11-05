#pragma once
enum zmk_studio_core_lock_state { ZMK_STUDIO_CORE_LOCK_STATE_LOCKED, ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED };
struct zmk_studio_core_lock_state_changed {
    enum zmk_studio_core_lock_state state;
};
int raise_zmk_studio_core_lock_state_changed(struct zmk_studio_core_lock_state_changed event);
