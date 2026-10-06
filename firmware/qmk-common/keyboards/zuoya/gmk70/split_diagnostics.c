// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_diagnostics.h"
#include "ch.h"
#include "timer.h"
#include <string.h>
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
#    include "sleep_log.h"
#endif

enum { EVENT_CAPACITY = 128, PEER_MAGIC = 0xD1, SLOW_MS = 256 };

typedef struct {
    uint32_t sequence;
    uint32_t time;
    uint8_t  event, a, b, c;
} split_diag_record_t;

// All access is under short, IRQ-state-preserving locks. The split worker may
// run while the main thread is blocked; it must still be able to report that.
static struct {
    split_diag_record_t records[EVENT_CAPACITY];
    uint32_t            total, loop_at;
    uint32_t            rpc_attempts, rpc_failures, rpc_ok_at, peer_at;
    uint8_t             rpc_command, tx_control, rx_control;
    uint8_t             peer[4];
    bool                loop_seen, rpc_active, rpc_seen, rpc_ok, peer_seen;
    split_diag_sample_t last_sample;
    uint32_t            accepted_idle_ms;
    uint8_t             sleep_reason, reason_context;
    bool                accepted_manual, sleep_saved, failure_pending;
} history;

static void put_u32(uint8_t *data, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i)
        data[i] = value >> (24 - 8 * i);
}

static uint32_t get_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

// Caller holds the lock. No formatting, allocation, flash or UART output.
static void record_event(split_diag_event_t event, uint8_t a, uint8_t b, uint8_t c, uint32_t now) {
    const uint32_t sequence                          = ++history.total;
    history.records[(sequence - 1) % EVENT_CAPACITY] = (split_diag_record_t){sequence, now, event, a, b, c};
}

void split_diagnostics_event(split_diag_event_t event, uint8_t a, uint8_t b, uint8_t c) {
    const syssts_t lock = chSysGetStatusAndLockX();
    if (event == SPLIT_DIAG_SLEEP_REASON) {
        history.sleep_reason   = a;
        history.reason_context = b;
    } else if (event == SPLIT_DIAG_HOST && !a) {
        history.sleep_reason   = 4; // Radio host suspend.
        history.reason_context = 0;
    } else if (event == SPLIT_DIAG_STATE && b == 1) {
        history.sleep_saved = false;
    } else if (event == SPLIT_DIAG_STATE && b == 0) {
        history.sleep_reason = history.reason_context = 0;
    }
    record_event(event, a, b, c, timer_read32());
    chSysRestoreStatusX(lock);
}

void split_diagnostics_sleep_accepted(bool manual, uint32_t idle_ms, uint8_t device) {
    const syssts_t lock      = chSysGetStatusAndLockX();
    history.accepted_idle_ms = idle_ms;
    history.accepted_manual  = manual;
    if (!manual) history.sleep_reason = history.reason_context = 0;
    uint32_t seconds = idle_ms / 1000;
    record_event(SPLIT_DIAG_TIMEOUT, manual, seconds > UINT8_MAX ? UINT8_MAX : seconds, device, timer_read32());
    chSysRestoreStatusX(lock);
}

#ifdef GMK70_PERSISTENT_DIAGNOSTICS
static void put_le32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (8 * i);
}
void split_diagnostics_checkpoint(uint8_t trigger) {
    uint8_t page[SLEEP_LOG_PAGE];
    memset(page, 0xFF, sizeof(page));
    const syssts_t lock = chSysGetStatusAndLockX();
    if ((trigger == SLEEP_LOG_BEFORE_STOP && history.sleep_saved) || (trigger == SLEEP_LOG_AFTER_WAKE && !history.sleep_saved)) {
        chSysRestoreStatusX(lock);
        return;
    }
    const split_diag_sample_t sample = split_diagnostics_sample();
    if (trigger == SLEEP_LOG_BEFORE_STOP) history.sleep_saved = true;
    if (trigger == SLEEP_LOG_AFTER_WAKE) history.sleep_saved = false;
    put_le32(page + 12, timer_read32());
    put_le32(page + 16, history.total);
    put_le32(page + 20, sample.timeout);
    put_le32(page + 24, history.accepted_idle_ms);
    page[28] = sample.battery;
    page[29] = sample.sleep_selector;
    page[30] = sample.status;
    page[31] = sample.device;
    page[32] = sample.radio;
    page[33] = sample.wake;
    page[34] = sample.pins;
    page[35] = sample.left;
    page[36] = history.sleep_reason;
    page[37] = trigger;
    page[38] = history.total < SLEEP_LOG_EVENTS ? history.total : SLEEP_LOG_EVENTS;
    page[39] = 12;
    memcpy(page + 40, history.peer, 3);
    page[43] = history.peer_seen;
    put_le32(page + 44, history.rpc_attempts);
    put_le32(page + 48, history.rpc_failures);
    page[52] = history.reason_context;
    page[53] = history.accepted_manual;
    page[54] = sample.connected;
    page[55] = 0;
    for (unsigned i = 0; i < page[38]; ++i) {
        uint32_t                   sequence = history.total - page[38] + 1 + i;
        const split_diag_record_t *record   = &history.records[(sequence - 1) % EVENT_CAPACITY];
        uint8_t                   *entry    = page + 56 + i * 12;
        put_le32(entry, record->sequence);
        put_le32(entry + 4, record->time);
        entry[8]  = record->event;
        entry[9]  = record->a;
        entry[10] = record->b;
        entry[11] = record->c;
    }
    chSysRestoreStatusX(lock);
    // Never perform flash I/O in an IRQ, split callback, or recorder lock.
    sleep_log_append(page);
}
#endif

static uint8_t age_units(uint32_t age) {
    return age / 16 > UINT8_MAX ? UINT8_MAX : age / 16;
}

static bool control_command(uint8_t command) {
    return command == 0xAA || command == 0xBB || command == 0xCC || command == 0xDD;
}

void split_diagnostics_loop(void) {
    const syssts_t            lock   = chSysGetStatusAndLockX();
    const uint32_t            now    = timer_read32();
    const split_diag_sample_t sample = split_diagnostics_sample();
    if (history.loop_seen && now - history.loop_at >= SLOW_MS) {
        record_event(SPLIT_DIAG_LOOP_GAP, age_units(now - history.loop_at), sample.status, sample.wake, now);
    }
    if (!history.loop_seen || sample.status != history.last_sample.status || sample.device != history.last_sample.device || sample.radio != history.last_sample.radio || sample.pins != history.last_sample.pins) {
        record_event(SPLIT_DIAG_LOCAL, sample.status, sample.device, sample.radio, now);
    }
    if (!history.loop_seen || sample.connected != history.last_sample.connected) {
        record_event(SPLIT_DIAG_LINK, sample.connected, sample.status, 0, now);
    }
    history.last_sample = sample;
    history.loop_seen   = true;
    history.loop_at     = now;
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
    const bool save_failure = history.failure_pending && sample.device != 0;
    if (save_failure) history.failure_pending = false;
#endif
    chSysRestoreStatusX(lock);
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
    if (save_failure) split_diagnostics_checkpoint(SLEEP_LOG_PEER_FAILURE);
#endif
}

void split_diagnostics_rpc_begin(uint8_t command) {
    const syssts_t lock = chSysGetStatusAndLockX();
    ++history.rpc_attempts;
    history.rpc_command = command;
    history.rpc_active  = true;
    if (control_command(command) && command != history.tx_control) {
        record_event(SPLIT_DIAG_CONTROL_TX, command, 0, 0, timer_read32());
        history.tx_control = command;
    }
    chSysRestoreStatusX(lock);
}

void split_diagnostics_rpc_end(uint8_t command, bool success, const uint8_t *peer) {
    const syssts_t lock = chSysGetStatusAndLockX();
    const uint32_t now  = timer_read32();
    history.rpc_active  = false;
    if (success)
        history.rpc_ok_at = now;
    else
        ++history.rpc_failures;
    // Repeated failed retries must not erase the event that led to the failure.
    if (!history.rpc_seen || history.rpc_ok != success) record_event(SPLIT_DIAG_RPC_RESULT, command, success, 0, now);
    history.rpc_seen = true;
    if (history.rpc_ok && !success) history.failure_pending = true;
    if (success) history.failure_pending = false;
    history.rpc_ok = success;
    if (success && peer != NULL && peer[0] == PEER_MAGIC) {
        // Record state changes or a stalled main loop, not every healthy sample.
        if (!history.peer_seen || peer[1] != history.peer[1] || (peer[2] >= SLOW_MS / 16) != (history.peer[2] >= SLOW_MS / 16)) {
            record_event(SPLIT_DIAG_PEER, peer[1], peer[2], peer[3], now);
        }
        memcpy(history.peer, peer, sizeof(history.peer));
        history.peer_at   = now;
        history.peer_seen = true;
    }
    chSysRestoreStatusX(lock);
}

void split_diagnostics_received(uint8_t command) {
    const syssts_t lock = chSysGetStatusAndLockX();
    if (control_command(command) && command != history.rx_control) {
        record_event(SPLIT_DIAG_CONTROL_RX, command, 0, 0, timer_read32());
        history.rx_control = command;
    }
    chSysRestoreStatusX(lock);
}

void split_diagnostics_reply(uint8_t body[4]) {
    const syssts_t lock = chSysGetStatusAndLockX();
    const uint32_t now  = timer_read32();
    body[0]             = PEER_MAGIC;
    body[1]             = split_diagnostics_sample().status;
    body[2]             = history.loop_seen ? age_units(now - history.loop_at) : UINT8_MAX;
    body[3]             = 0; // Reserved: asynchronous SPI is not instrumented.
    chSysRestoreStatusX(lock);
}

bool split_diagnostics_command(uint8_t *data, uint8_t length) {
    // Read-only VIA getter 02 81 page. Only page 3 accepts an event sequence.
    if (length != 32 || data[0] != 2 || data[1] != 0x81 || data[2] > 3) return false;
    for (uint8_t i = data[2] == 3 ? 7 : 3; i < length; ++i)
        if (data[i]) return false;
    const uint32_t sequence = data[2] == 3 ? get_u32(data + 3) : 0;
    const syssts_t lock     = chSysGetStatusAndLockX();
    const uint32_t now      = timer_read32();
    memset(data + 3, 0, length - 3);
    data[3] = 'G';
    data[4] = 'S';
    data[5] = 2; // Baseline rollback: mode-reply telemetry; no SPI instrumentation.
    switch (data[2]) {
        case 0: {
            const split_diag_sample_t sample = split_diagnostics_sample();
            put_u32(data + 6, now);
            put_u32(data + 10, history.total);
            data[14] = EVENT_CAPACITY;
            data[15] = sample.status;
            data[16] = sample.device;
            data[17] = sample.radio;
            data[18] = sample.wake;
            data[19] = sample.pins;
            data[20] = sample.left;
            data[21] = sample.connected;
            put_u32(data + 22, history.loop_seen ? now - history.loop_at : UINT32_MAX);
            break;
        }
        case 1:
            put_u32(data + 6, history.rpc_attempts);
            put_u32(data + 10, history.rpc_failures);
            put_u32(data + 14, history.rpc_ok_at);
            data[30] = history.rpc_command;
            data[31] = history.rpc_active;
            break;
        case 2:
            put_u32(data + 6, history.peer_at);
            memcpy(data + 10, history.peer, sizeof(history.peer));
            data[14] = history.peer_seen;
            break;
        case 3: {
            const split_diag_record_t record = history.records[(sequence - 1) % EVENT_CAPACITY];
            put_u32(data + 6, sequence);
            if (sequence && record.sequence == sequence) {
                put_u32(data + 10, record.time);
                data[14] = record.event;
                data[15] = record.a;
                data[16] = record.b;
                data[17] = record.c;
                data[18] = 1;
            }
            break;
        }
    }
    chSysRestoreStatusX(lock);
    return true;
}
