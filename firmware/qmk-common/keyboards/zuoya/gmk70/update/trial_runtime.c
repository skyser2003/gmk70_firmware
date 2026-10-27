// SPDX-License-Identifier: GPL-2.0-or-later
// Runtime-only watchdog qualification. No reset-vector override or staging jump.
#include "trial_platform.h"
#include "trial_deadline.h"

__attribute__((used, section(".trial_retained"), aligned(8))) volatile gmk_trial_boot_record_t gmk_trial_record;

extern uint32_t             SystemCoreClock;
static gmk_trial_deadline_t deadline;
static bool                 fallback_test;
static bool                 fault_test;
static uint32_t             traced_address, traced_value;
static bool                 traced_write, traced_valid;
static uint32_t             prepared_role;
// Checked by the exception veneers before they alter the ordinary fault path.
volatile uint32_t gmk_trial_probe_active;
enum { PROBE_ACTIVE = 0x47545052 };
__attribute__((noreturn)) static void software_reset(void);
void                                  gmk_trial_runtime_trigger_fault(void);

#ifdef GMK_TRIAL_RUNTIME_TEST
uint32_t                       gmk_trial_runtime_read(uint32_t address);
void                           gmk_trial_runtime_write(uint32_t address, uint32_t value);
void                           gmk_trial_runtime_disable_irq(void);
void                           gmk_trial_runtime_barrier(void);
__attribute__((noreturn)) void gmk_trial_runtime_unhandled(void);
#else
static uint32_t gmk_trial_runtime_read(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static void gmk_trial_runtime_write(uint32_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}
static void gmk_trial_runtime_disable_irq(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}
static void gmk_trial_runtime_barrier(void) {
    __asm__ volatile("dsb\nisb" ::: "memory");
}
#endif

static const gmk_trial_mmio_t io = {.read = gmk_trial_runtime_read, .write = gmk_trial_runtime_write};
enum { RCC_CLRRSTSTAT = 0x40010CC8, RCC_RSTSTAT = 0x40010D00, SCB_AIRCR = 0xE000ED0C };

void gmk_trial_runtime_init(void) {
    // Normal Vial initialization has already established clocks and board role.
    // This path never touches watchdog, role GPIO or DWT registers.
    (void)gmk_trial_boot_complete(&gmk_trial_record, io.read(RCC_RSTSTAT));
    io.write(RCC_CLRRSTSTAT, 1);
    gmk_trial_runtime_barrier();
    fallback_test          = false;
    fault_test             = false;
    gmk_trial_probe_active = 0;
    traced_valid           = false;
    deadline               = (gmk_trial_deadline_t){0};
}

void gmk_trial_runtime_fallback_test(bool enabled) {
    fallback_test = enabled;
}

void gmk_trial_runtime_fault_test(bool enabled) {
    fault_test = enabled;
}

static void checkpoint(uint32_t address, uint32_t value, bool write) {
    if (traced_valid && traced_address == address && traced_value == value && traced_write == write) return;
    if (!gmk_trial_boot_trace_io(&gmk_trial_record, address, value, write)) software_reset();
    traced_address = address;
    traced_value   = value;
    traced_write   = write;
    traced_valid   = true;
}

static uint32_t trace_read(uint32_t address) {
    checkpoint(address, 0, false);
    gmk_trial_runtime_barrier();
    return io.read(address);
}

static void trace_write(uint32_t address, uint32_t value) {
    checkpoint(address, value, true);
    gmk_trial_runtime_barrier();
    io.write(address, value);
    gmk_trial_runtime_barrier();
}

static const gmk_trial_mmio_t traced_io = {.read = trace_read, .write = trace_write};

__attribute__((noreturn)) void gmk_trial_runtime_fault(uint32_t kind, uint32_t pc) {
    if (gmk_trial_probe_active != PROBE_ACTIVE || io.read(RCC_RSTSTAT) != 0) {
#ifdef GMK_TRIAL_RUNTIME_TEST
        gmk_trial_runtime_unhandled();
#else
        extern void _unhandled_exception(void) __attribute__((noreturn));
        _unhandled_exception();
#endif
    }
    gmk_trial_probe_active = 0;
    (void)gmk_trial_boot_trace_fault(&gmk_trial_record, kind, io.read(0xE000ED28), pc);
    software_reset();
}

void gmk_trial_runtime_trace(uint32_t page, uint8_t result[16]) {
    for (unsigned i = 0; i < 16; ++i)
        result[i] = 0;
    uint32_t words[5];
    if (page > 1 || !gmk_trial_boot_trace_report(&gmk_trial_record, words)) return;
    if (page == 0) {
        for (unsigned i = 0; i < 4; ++i)
            gmk_write_u32(result + i * 4, words[i]);
    } else {
        gmk_write_u32(result, words[4]);
    }
}

bool gmk_trial_platform_prepare(uint32_t action, gmk_image_t image, uint32_t role) {
    if (action != GMK_TRIAL_BOOT_PROBE || image.size || image.crc || role != GMK_TRIAL_BOOT_ROLE_LEFT) return false;
    const uint8_t *commit = gmk_flash_view(GMK_COMMIT_BASE);
    for (unsigned i = 0; i < GMK_PAGE_SIZE; ++i)
        if (commit[i] != 0xFF) return false;
    // Verify an independently polled fallback before acknowledging the request.
    if (!gmk_trial_deadline_begin(&deadline, &io, SystemCoreClock)) return false;
    prepared_role = role;
    return gmk_trial_boot_prepare(&gmk_trial_record, action, image, role);
}

__attribute__((noreturn)) static void software_reset(void) {
    gmk_trial_probe_active = 0;
    gmk_trial_runtime_barrier(); // Commit retained SRAM before requesting reset.
    io.write(SCB_AIRCR, 0x05FA0004);
    gmk_trial_runtime_barrier();
    for (;;) {
    }
}

__attribute__((noreturn)) void gmk_trial_platform_reset(void) {
    gmk_trial_runtime_disable_irq();
    gmk_trial_deadline_rebase(&deadline, &io);
    if (!gmk_trial_boot_runtime_enter(&gmk_trial_record, prepared_role)) software_reset();
    if (!gmk_trial_boot_trace_enable(&gmk_trial_record)) software_reset();
    gmk_trial_probe_active = PROBE_ACTIVE;
    io.write(RCC_CLRRSTSTAT, 1);
    gmk_trial_runtime_barrier();
    if (fault_test) gmk_trial_runtime_trigger_fault();
    if (!fallback_test && !gmk_trial_watchdog_start(&traced_io)) software_reset();
    // IRQs stay masked and neither watchdog nor clock configuration is touched
    // again. Actual RCC flags distinguish IWDG proof from software fallback.
    while (!gmk_trial_deadline_expired(&deadline, &io)) {
    }
    software_reset();
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
