/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <drivers/behavior.h>
/* Native metadata is initialized once before behavior devices become ready. */
extern struct behavior_parameter_metadata gmk70_menu_metadata[4];
int                                       gmk70_menu_metadata_init(void);
