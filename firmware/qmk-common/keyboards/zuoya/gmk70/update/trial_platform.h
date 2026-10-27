// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "trial_boot.h"

// The linker reserves the first 64 SRAM bytes in trial updater builds only.
extern volatile gmk_trial_boot_record_t gmk_trial_record;

bool                           gmk_trial_platform_prepare(uint32_t action, gmk_image_t image, uint32_t role);
__attribute__((noreturn)) void gmk_trial_platform_reset(void);
// Four little-endian words: action | phase << 8, reset flags, image size, CRC.
void gmk_trial_platform_result(uint8_t result[16]);

#ifdef GMK70_TRIAL_RUNTIME
// Called after normal CRT/framework initialization, never from Reset_Handler.
void gmk_trial_runtime_init(void);
void gmk_trial_runtime_fallback_test(bool enabled);
#endif

// Called only by the trial build's strong Reset_Handler, before the C runtime.
void gmk_trial_early_boot(void);
