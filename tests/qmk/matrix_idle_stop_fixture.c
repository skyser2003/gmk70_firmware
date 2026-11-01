// SPDX-License-Identifier: GPL-2.0-or-later
// Keep this native boundary in a separate translation unit, as in firmware.
void lpwr_matrix_idle_stop(void) __attribute__((weak));
void lpwr_matrix_idle_stop(void) {}
void lpwr_stop_prepare(void) {
    lpwr_matrix_idle_stop();
}
