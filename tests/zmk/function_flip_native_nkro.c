/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/nkro/nkro.c"
int function_flip_native_nkro_init(void) {
    return registered_init();
}
