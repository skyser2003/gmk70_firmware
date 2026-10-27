/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "receiver.h"
#include "wired.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* Model the pinned Zephyr mutex's highest-priority ownership handoff at the
 * production service/stats boundary, not UART electrical or silicon timing. */
enum { RESPONDER = 4, SCANNER = 5, UPDATER = 6, SLEPT = 1, STARVED = 2 };
static jmp_buf  leave_worker;
static unsigned owner, frame_completions, scanner_completions, partial_waits;
static bool     scanner_waiting, updater_waiting, queued_next_id, traffic;
static uint32_t now;
static unsigned feeds, writes, resets;
static bool     left_role, wired_mode = true, usb_ready = true;
static unsigned change_during_stats, change_after_admission;
static bool     usb_queued;
static uint8_t  usb_request[32], usb_response[32], flash[0x40000], payload[1024];
static void (*wire_handler)(const uint8_t[32], uint8_t[32]);
unsigned receiver_locked;

static unsigned highest_waiter(bool responder_waiting) {
    if (responder_waiting) return RESPONDER;
    if (scanner_waiting) return SCANNER;
    return updater_waiting ? UPDATER : 0;
}
static void partial_frame_cycle(void) {
    assert(owner == RESPONDER && scanner_waiting);
    ++partial_waits;
    now += 19;             /* Each partial transaction is shorter than the 20 ms budget. */
    queued_next_id = true; /* Next ID arrives behind the completed payload. */
    ++frame_completions;
    owner = highest_waiter(false);
    assert(owner == SCANNER);
    scanner_waiting = false;
    /* Responder runs first, consumes queued ID, and waits on scanner's lock. */
    assert(queued_next_id);
    queued_next_id = false;
    owner          = highest_waiter(true);
    assert(owner == RESPONDER);
    ++scanner_completions;
    ++now;
    scanner_waiting = true; /* Scanner sleeps 1 ms, requeues during next frame. */
}
static void mutate_gates(unsigned change) {
    if (change == 1) usb_ready = false;
    if (change == 2) wired_mode = false;
}
void gmk_wired_get_stats(struct gmk_wired_stats *stats) {
    mutate_gates(change_during_stats);
    if (traffic) {
        updater_waiting = true;
        for (unsigned frame = 0; frame < 1700; ++frame)
            partial_frame_cycle();
        longjmp(leave_worker, STARVED);
    }
    *stats = (struct gmk_wired_stats){.peer = true};
}
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats) {
    mutate_gates(change_during_stats);
    if (traffic) return false; /* No enqueue, ownership transfer or waiting. */
    *stats = (struct gmk_wired_stats){.peer = true};
    return true;
}
bool gmk_zmk_watchdog_init(void) {
    return true;
}
void gmk_zmk_watchdog_feed(void) {
    ++feeds;
}
bool gmk_wired_is_left(void) {
    return left_role;
}
bool gmk_wired_mode(void) {
    return wired_mode;
}
bool zmk_usb_is_hid_ready(void) {
    return usb_ready;
}
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32])) {
    wire_handler = handler;
}
bool gmk_wired_update(const uint8_t in[32], uint8_t out[32]) {
    (void)in;
    (void)out;
    assert(false);
    return false;
}
int gmk_zmk_raw_hid_init(void) {
    return 0;
}
void gmk_zmk_raw_hid_poll(const gmk_zmk_update_env_t *env) {
    if (usb_queued) {
        usb_queued = false;
        gmk_zmk_receiver_receive(usb_request, usb_response, true, env);
        mutate_gates(change_after_admission);
    }
}
void k_thread_start(void *thread) {
    (void)thread;
}
void k_sleep(int ms) {
    assert(ms == 1);
    if (traffic)
        partial_frame_cycle();
    else
        ++now;
    longjmp(leave_worker, SLEPT);
}
uint32_t k_uptime_get_32(void) {
    return now;
}
void sys_reboot(int type) {
    assert(type == 0);
    ++resets;
}
const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(flash));
    return flash + address - GMK_FLASH_BASE;
}
uint32_t gmk_running_size(void) {
    return sizeof(payload);
}
bool gmk_flash_page(uint32_t address, const uint8_t data[256]) {
    assert(!receiver_locked);
    assert(address == GMK_COMMIT_BASE || (address >= GMK_STAGE_BASE && address < GMK_STAGE_BASE + GMK_APP_SIZE));
    memcpy(flash + address - GMK_FLASH_BASE, data, 256);
    ++writes;
    return true;
}
#include "service.c"
static void run_iteration(void) {
    int result = setjmp(leave_worker);
    if (!result) worker(NULL, NULL, NULL);
    assert(result == SLEPT); /* Old blocking stats path reaches STARVED. */
}
static void setup(void) {
    now = feeds = writes = resets = frame_completions = scanner_completions = partial_waits = 0;
    traffic = updater_waiting = queued_next_id = usb_queued = false;
    wired_mode = usb_ready = true;
    left_role              = false;
    change_during_stats = change_after_admission = 0;
    owner                                        = RESPONDER;
    scanner_waiting                              = true;
    memset(flash, 0xff, sizeof(flash));
    memset(payload, 0xa5, sizeof(payload));
    gmk_write_u32(payload, 0x20006000);
    gmk_write_u32(payload + 4, GMK_APP_BASE + 9);
    assert(watchdog_init() == 0 && service_init() == 0 && wire_handler);
}
static void packet(uint8_t request[32], unsigned op, unsigned target) {
    memset(request, 0, 32);
    request[0] = 0x7f;
    request[1] = 0x70;
    request[2] = 1;
    request[3] = target;
    request[4] = op;
    request[8] = 0x12;
    if (op == GMK_BEGIN) {
        request[5] = 12;
        gmk_write_u32(request + 16, sizeof(payload));
        gmk_write_u32(request + 20, gmk_crc32(payload, sizeof(payload)));
        request[24] = 1;
    }
}
static void deliver(uint8_t request[32]) {
    uint8_t response[32];
    wire_handler(request, response);
    assert(response[5] == GMK_BUSY);
    run_iteration();
}
static void scheduling(void) {
    setup();
    traffic = true;
    uint8_t request[32];
    packet(request, GMK_BEGIN, 1);
    deliver(request);
    assert(writes == 1 && feeds == 1 && frame_completions == 1 && scanner_completions == 1 && !updater_waiting);
    /* Real common receiver commits while this service repeatedly bypasses the
     * contended telemetry lock; all UART/scan frame handoffs keep progressing. */
    for (unsigned offset = 0; offset < sizeof(payload); offset += 16) {
        packet(request, GMK_DATA, 1);
        request[5] = 16;
        gmk_write_u32(request + 12, offset);
        memcpy(request + 16, payload + offset, 16);
        deliver(request);
    }
    packet(request, GMK_SEAL, 1);
    deliver(request);
    packet(request, GMK_INSTALL, 1);
    deliver(request);
    assert(!resets);
    uint32_t published       = now - 20;
    unsigned feeds_at_commit = feeds;
    while (now - published < 300) {
        run_iteration();
        assert(!resets);
    }
    run_iteration();
    assert(resets == 1 && feeds > feeds_at_commit && partial_waits == frame_completions && scanner_completions == frame_completions);
    uint8_t health_request[32] = {0x7f, 0x72, 1, 1, 0x20, 0, 0, 0, 0x22}, response[32];
    wire_handler(health_request, response);
    assert(response[5] == GMK_OK && !(gmk_read_u32(response + 24) & 4)); /* busy stats means unknown, not falsely fresh */
}
static void gates_changed(void) {
    for (unsigned boundary = 0; boundary < 2; ++boundary)
        for (unsigned gate = 1; gate <= 2; ++gate) {
            setup();
            left_role = true;
            packet(usb_request, GMK_BEGIN, 0);
            usb_queued = true;
            if (boundary == 0)
                change_during_stats = gate;
            else
                change_after_admission = gate;
            run_iteration();
            assert(writes == 0 && feeds == 1);
            if (boundary == 0)
                assert(usb_response[5] == GMK_UNAVAILABLE);
            else
                assert(usb_response[5] == GMK_BUSY);
        }
}
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "gates") == 0)
        gates_changed();
    else
        scheduling();
    puts("ZMK service scheduling/gate boundary regression passed");
}
