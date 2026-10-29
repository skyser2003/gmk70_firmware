/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "image.h"
#include "resident_handoff.h"

static uint8_t  boot[8192];
static uint8_t  stage[1024], commit[256];
static uint32_t rcc[0x104 / 4], watchdog[4], cpu_control, cpu_ipsr, boot_magic;
static unsigned detached, waited, locked, writes, jumped;
static int      detach_error;
static bool     stuck_dma;
static jmp_buf  jump;
const uint8_t  *gmk_flash_view(uint32_t address) {
    if (address == GMK_STAGE_BASE) return stage;
    if (address == GMK_COMMIT_BASE) return commit;
    assert(address == GMK_FLASH_BASE);
    return boot;
}
uint32_t sys_read32(uintptr_t address) {
    if (address == GMK_RAM_END - 4) return boot_magic;
    if (address == 0x40010474U) return 0x80;
    /* Westberry wb32fq95xx.h: DMAC1=APB1+0x7c00, DMAC2=APB2+0x7c00.
     * The buses have different bases; treating both as APB2 must fail. */
    if (address == 0x40007fa0U || address == 0x4000ffa0U) return stuck_dma;
    if (address >= 0x40010c00U && address <= 0x40010d00U) return rcc[(address - 0x40010c00U) / 4];
    if (address >= 0x40010800U && address <= 0x4001080cU) return watchdog[(address - 0x40010800U) / 4];
    if (address == 0x40010448U) return 1;
    if (address == 0x4001047cU) return 3;
    assert(0);
    return 0;
}
void sys_write32(uint32_t value, uintptr_t address) {
    assert(locked && detached && waited == 50);
    ++writes;
    if (address == 0x40010800U) {
        assert(value == 0xaaaa && writes > 8);
        return;
    }
    /* Only stop bus masters and core interrupt state. No flash/analog/cache
     * resets, watchdog reprogramming, vector writes or reset requests in C. */
    if (address == 0x40010c40U || address == 0x40010c48U || address == 0x40010c4cU) return;
    if (address == 0x40003804U || address == 0x4000904cU) {
        assert(value == 0);
        return;
    }
    if (address == 0xe000ed24U || address == 0xe000ed28U || address == 0xe000ed2cU || address == 0xe000ed30U) return;
    if (address == 0x40010c90U || address == 0x40010c94U) {
        assert(value == 1 || value == 0);
        return;
    }
    assert((address >= 0xe000e010U && address <= 0xe000e018U) || address == 0xe000e180U || address == 0xe000e184U || address == 0xe000e280U || address == 0xe000e284U || address == 0xe000ed04U);
}
void __disable_irq(void) {
    assert(detached && waited == 50 && !locked);
    locked = 1;
}
int usb_disable(void) {
    assert(!locked && !writes);
    ++detached;
    return detach_error;
}
void wb32_usb_disconnect(void) {
    assert(detached && !locked);
}
void k_msleep(int ms) {
    assert(detached && !locked);
    waited += ms;
}
void k_sched_lock(void) {
    assert(detached && waited == 50 && !locked);
}
uint32_t __get_CONTROL(void) {
    return cpu_control;
}
uint32_t __get_IPSR(void) {
    return cpu_ipsr;
}
void gmk_zmk_resident_branch(uint32_t vectors) {
    assert(vectors == GMK_FLASH_BASE && locked && writes > 8);
    ++jumped;
    longjmp(jump, 1);
}
static void reset(void) {
    memset(rcc, 0, sizeof(rcc));
    memset(watchdog, 0, sizeof(watchdog));
    rcc[0x08 / 4] = 2;
    rcc[0]        = 0x21;
    rcc[1]        = 1;
    rcc[0x1c / 4] = rcc[0x20 / 4] = 0x80;
    rcc[0x3c / 4]                 = 4;
    rcc[0x64 / 4]                 = 1;
    watchdog[1]                   = 6;
    watchdog[2]                   = 4095;
    cpu_control = cpu_ipsr = boot_magic = detached = waited = locked = writes = jumped = 0;
    detach_error                                                                       = 0;
    stuck_dma                                                                          = false;
    memset(stage, 0, sizeof(stage));
    gmk_write_u32(stage, 0x20006000);
    gmk_write_u32(stage + 4, GMK_APP_BASE + 0x201);
    memcpy(stage + sizeof(stage) - 16, "GMK70ZMKBOOT\001\000\000\000", 16);
    gmk_manifest_encode(commit, (gmk_image_t){sizeof(stage), gmk_crc32(stage, sizeof(stage))});
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    memset(boot, 0xff, sizeof(boot));
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        size_t size = fread(boot, 1, sizeof(boot), f);
        assert(size >= 64 && fclose(f) == 0);
    } else {
        /* Synthetic admission data, never executable firmware. The CRC suffix
         * exercises the actual production CRC pin without an ignored build
         * prerequisite. A separate opt-in run uses the real built resident. */
        gmk_write_u32(boot, 0x20006ff8);
        gmk_write_u32(boot + 4, 0x080000a5);
        gmk_write_u32(boot + sizeof(boot) - 4, 0x94a15d29);
    }
    assert(gmk_crc32(boot, sizeof(boot)) == 1770733249U);
    reset();
    boot[100] ^= 1;
    assert(!gmk_zmk_resident_handoff() && !detached && !writes);
    boot[100] ^= 1;
    reset();
    stage[sizeof(stage) - 16] ^= 1;
    gmk_manifest_encode(commit, (gmk_image_t){sizeof(stage), gmk_crc32(stage, sizeof(stage))});
    assert(!gmk_zmk_resident_handoff() && !detached && !writes);
    reset();
    stage[20] ^= 1;
    assert(!gmk_zmk_resident_handoff() && !detached && !writes);
    reset();
    commit[0] ^= 1;
    assert(!gmk_zmk_resident_handoff() && !detached && !writes);
    for (unsigned failure = 0; failure < 10; ++failure) {
        reset();
        if (failure == 0) cpu_ipsr = 3;
        if (failure == 1) cpu_control = 1;
        if (failure == 2) watchdog[1] = 0;
        if (failure == 3) watchdog[2] = 2999;
        if (failure == 4) watchdog[3] = 1;
        if (failure == 5) rcc[0x08 / 4] = 0;
        if (failure == 6) rcc[0x18 / 4] = 1;
        if (failure == 7) rcc[0x3c / 4] = 0;
        if (failure == 8) rcc[0x64 / 4] = 0;
        if (failure == 9) boot_magic = 0xdeadbeefU;
        assert(!gmk_zmk_resident_handoff() && !detached && !writes);
    }
    reset();
    detach_error = -1;
    assert(!gmk_zmk_resident_handoff() && detached == 1 && !locked && !writes);
    reset();
    stuck_dma = true;
    assert(!gmk_zmk_resident_handoff() && detached == 1 && locked && !jumped);
    reset();
    if (!setjmp(jump)) {
        (void)gmk_zmk_resident_handoff();
        assert(0);
    }
    assert(jumped == 1 && detached == 1 && waited == 50);
    puts("Resident handoff admission/order tests passed; silicon behavior untested");
}
