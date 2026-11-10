/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/keymap.h>
/* Not wrapped: keep the real native input entry inspectable under LTO. */
extern __typeof__(zmk_keymap_position_state_changed) zmk_keymap_position_state_changed __attribute__((noipa));
