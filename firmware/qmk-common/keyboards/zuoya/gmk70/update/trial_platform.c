// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_platform.h"
#include "trial.h"
#include "trial_role.h"
#include "trial_watchdog.h"

// No initialized mutable globals or framework calls: early_boot precedes CRT0.
__attribute__((used, section(".trial_retained"), aligned(8))) volatile gmk_trial_boot_record_t gmk_trial_record;
_Static_assert(sizeof(gmk_trial_record) == 64, "Trial linker record must be 64 bytes");

enum {
    RCC_CLRRSTSTAT    = 0x40010CC8,
    RCC_RSTSTAT       = 0x40010D00,
    SYSTICK_CTRL      = 0xE000E010,
    SYSTICK_LOAD      = 0xE000E014,
    SYSTICK_VAL       = 0xE000E018,
    NVIC_ICER         = 0xE000E180,
    NVIC_ICPR         = 0xE000E280,
    SCB_ICSR          = 0xE000ED04,
    SCB_AIRCR         = 0xE000ED0C,
    SCB_SHCSR         = 0xE000ED24,
    SCB_CFSR          = 0xE000ED28,
    SCB_HFSR          = 0xE000ED2C,
    SCB_DFSR          = 0xE000ED30,
    SCB_PENDING_CLEAR = (1U << 27) | (1U << 25),
};

static uint32_t read_register(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static void write_register(uint32_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static void barrier(void) {
    __asm__ volatile("dsb\n"
                     "isb\n" ::
                         : "memory");
}

static const gmk_trial_mmio_t io = {.read = read_register, .write = write_register};

// Serialize the portable sampler with normal framework GPIO/clock operations.
// Before CRT, PRIMASK is already set by the strong Reset_Handler.
static bool actual_role(uint32_t *role) {
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n"
                     "cpsid i\n"
                     : "=r"(primask)::"memory");
    bool ready = gmk_trial_role_read(&io, role);
    barrier();
    __asm__ volatile("msr primask, %0" : : "r"(primask) : "memory");
    return ready;
}

static bool commit_erased(void) {
    const uint8_t *commit = gmk_flash_view(GMK_COMMIT_BASE);
    for (unsigned i = 0; i < GMK_PAGE_SIZE; ++i)
        if (commit[i] != 0xFF) return false;
    return true;
}

static bool staged_image_valid(gmk_image_t image) {
    if (!gmk_image_bounds(image.size)) return false;
    const uint8_t *staged = gmk_flash_view(GMK_STAGE_BASE);
    return gmk_trial_vectors(staged, image.size) && gmk_crc32(staged, image.size) == image.crc;
}

bool gmk_trial_platform_prepare(uint32_t action, gmk_image_t image, uint32_t role) {
    uint32_t hardware_role;
    if (!actual_role(&hardware_role) || role != hardware_role || !commit_erased()) return false;
    if (action == GMK_TRIAL_BOOT_RUN && !staged_image_valid(image)) return false;
    return gmk_trial_boot_prepare(&gmk_trial_record, action, image, role);
}

__attribute__((noreturn)) void gmk_trial_platform_reset(void) {
    __asm__ volatile("cpsid i" ::: "memory");
    write_register(RCC_CLRRSTSTAT, 1);
    barrier();
    write_register(SCB_AIRCR, 0x05FA0004);
    barrier();
    for (;;)
        __asm__ volatile("nop");
}

void gmk_trial_platform_result(uint8_t result[16]) {
    for (unsigned i = 0; i < 16; ++i)
        result[i] = 0;
    gmk_trial_boot_result_t report;
    if (!gmk_trial_boot_report(&gmk_trial_record, &report)) return;
    gmk_write_u32(result, report.action | (report.phase << 8));
    gmk_write_u32(result + 4, report.reset_flags);
    gmk_write_u32(result + 8, report.image.size);
    gmk_write_u32(result + 12, report.image.crc);
}

static void clear_core_state(void) {
    write_register(SYSTICK_CTRL, 0);
    write_register(SYSTICK_LOAD, 0);
    write_register(SYSTICK_VAL, 0);
    // The pinned WB32FQ95 has 40 external vectors, hence two NVIC banks.
    for (unsigned bank = 0; bank < 2; ++bank) {
        write_register(NVIC_ICER + bank * 4, UINT32_MAX);
        write_register(NVIC_ICPR + bank * 4, UINT32_MAX);
    }
    write_register(SCB_ICSR, SCB_PENDING_CLEAR);
    write_register(SCB_SHCSR, 0);
    write_register(SCB_CFSR, UINT32_MAX);
    write_register(SCB_HFSR, UINT32_MAX);
    write_register(SCB_DFSR, 0x1F);
    barrier();
}

__attribute__((noreturn)) static void await_watchdog(void) {
    __asm__ volatile("cpsid i" ::: "memory");
    for (;;)
        __asm__ volatile("nop");
}

__attribute__((noreturn)) void gmk_trial_platform_handoff(uint32_t vectors);

void gmk_trial_early_boot(void) {
    uint32_t reset_flags = read_register(RCC_RSTSTAT);
    write_register(RCC_CLRRSTSTAT, 1);
    barrier();
    gmk_trial_boot_request_t request;
    // Admission consumes the checked intent before it may sample hardware.
    // Ordinary and watchdog-return boots perform no GPIO bus accesses.
    if (!gmk_trial_boot_admit(&gmk_trial_record, reset_flags, actual_role, &request)) {
        (void)gmk_trial_boot_complete(&gmk_trial_record, reset_flags);
        return;
    }
    // Validation cannot arm a watchdog or alter flash. A rejected intent is
    // already consumed and remains a reportable REJECTED result.
    if (!commit_erased() || (request.action == GMK_TRIAL_BOOT_RUN && !staged_image_valid(request.image))) return;
    clear_core_state();
    if (!gmk_trial_watchdog_start(&io)) return;
    // From the start key onward, every failure waits for IWDG without feeding.
    if (!gmk_trial_boot_enter(&gmk_trial_record) || request.action == GMK_TRIAL_BOOT_PROBE) await_watchdog();
    gmk_trial_platform_handoff(GMK_STAGE_BASE);
}
