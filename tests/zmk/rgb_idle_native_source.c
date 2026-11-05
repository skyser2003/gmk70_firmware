/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../dependencies/zmk/app/src/rgb_underglow.c"
_Static_assert(sizeof(struct rgb_underglow_state) == 10, "Pinned native RGB image size changed");
_Static_assert(offsetof(struct rgb_underglow_state, on) == 8, "Pinned native RGB on flag moved");
