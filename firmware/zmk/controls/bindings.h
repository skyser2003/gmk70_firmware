/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zmk/behavior.h>
/* Historical names and IDs remain valid when loading old native settings. */
void        gmk_controls_note_legacy_id(uint16_t id, const uint8_t *name, size_t size);
const char *gmk_controls_legacy_name(uint16_t id);
int         gmk_controls_normalize_binding(struct zmk_behavior_binding *binding);
void        gmk_controls_migrate_keymap(void);

void gmk_controls_prepare_legacy_load(void);

bool gmk_controls_bindings_ready(void);
