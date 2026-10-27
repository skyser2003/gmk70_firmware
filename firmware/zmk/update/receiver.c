/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "receiver.h"
#include "wired.h"
#if defined(CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY) && !defined(CONFIG_GMK70_STAGING_QUALIFICATION)
#    include "trial.h"
#    define PEER_TRIAL_RELAY
#endif
#include <string.h>
#include <zephyr/kernel.h>

/* Only the dedicated worker touches update. Callbacks see immutable cached
 * packets under lock, including while flash/CRC/relay work runs without it. */
static struct k_spinlock    lock;
static gmk_update_t         update;
static uint8_t              queued[32], last[3][32], remote_health_request[32], remote_health[32];
static bool                 pending, queued_usb, remote_health_valid, committed;
static uint32_t             last_activity, reset_at;
static gmk_zmk_update_env_t health;

static bool health_packet(const uint8_t *request) {
    if (request[0] != 0x7f || request[1] != 0x72 || request[2] != 1 || request[3] > 1 || request[4] != 0x20 || request[5] || request[6] || request[7] || !gmk_read_u32(request + 8)) return false;
    for (unsigned i = 12; i < 32; ++i)
        if (request[i]) return false;
    return true;
}

static bool trial_packet(const uint8_t *request) {
#ifdef PEER_TRIAL_RELAY
    return gmk_trial_packet_valid(request);
#else
    (void)request;
    return false;
#endif
}

static unsigned cache_index(const uint8_t *request) {
    return request[1] == 0x71 ? 2 : request[3];
}

static bool readonly(const uint8_t *request) {
#ifdef PEER_TRIAL_RELAY
    if (request[1] == 0x71 && (request[4] == GMK_TRIAL_RESULT || request[4] == GMK_TRIAL_CAPS || request[4] == GMK_TRIAL_TRACE)) return true;
#endif
    return request[1] == 0x72 || request[4] == GMK_STATUS || request[4] == GMK_INFO;
}

static bool allowed(const uint8_t *request, bool usb, const gmk_zmk_update_env_t *env) {
    if (request[1] == 0x71 && (!usb || request[3] != 1)) return false;
    if (!env->wired) return false;
    if (usb) {
        if (!env->left || !env->usb_configured) return false;
    } else if (env->left || request[3] != 1) {
        return false;
    }
    if (readonly(request)) return true;
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    /* The qualification image executes in staging: never permit peer writes or
     * writes into its own running image, even if the peer is an older Vial. */
    return usb && request[3] == 0 && (request[4] == GMK_BEGIN || request[4] == GMK_ABORT);
#else
    return env->watchdog_ready;
#endif
}

static void reply(const uint8_t *request, uint8_t *response, gmk_update_result_t result, const gmk_zmk_update_env_t *env) {
    memset(response, 0, 32);
    memcpy(response, request, 5);
    memcpy(response + 8, request + 8, 8);
    response[5] = result;
    response[7] = (env->left ? 1 : 0) | (env->left && env->usb_configured ? 2 : 0);
    gmk_write_u32(response + 24, request[2]);
}

static void health_reply(const uint8_t *request, uint8_t *response, const gmk_zmk_update_env_t *env) {
    reply(request, response, GMK_OK, env);
    gmk_write_u32(response + 12, 0x4b4d5a47);
    gmk_write_u32(response + 16, health.uptime_ms);
    gmk_write_u32(response + 20, health.feed_calls);
    uint32_t flags = (health.watchdog_ready ? 1U : 0U) | (env->wired ? 2U : 0U) | (health.peer ? 4U : 0U);
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    flags |= 8U;
#endif
    gmk_write_u32(response + 24, flags);
    gmk_write_u32(response + 28, health.matrix_events);
}

void gmk_zmk_receiver_init(const gmk_zmk_update_env_t *env) {
    update  = (gmk_update_t){0};
    pending = remote_health_valid = committed = false;
    last_activity                             = env->uptime_ms;
    health                                    = *env;
    for (unsigned target = 0; target < 3; ++target) {
        uint8_t request[32] = {0x7f, target == 2 ? 0x71 : 0x70, target == 2 ? 2 : 1, target == 0 ? 0 : 1, GMK_STATUS};
        gmk_update_response(&update, request, GMK_OK, last[target]);
        gmk_write_u32(last[target] + 24, request[2]);
    }
}

void gmk_zmk_receiver_receive(const uint8_t request[32], uint8_t response[32], bool usb, const gmk_zmk_update_env_t *env) {
    bool is_health = health_packet(request);
    bool is_trial  = trial_packet(request);
    if (!is_health && !is_trial && !gmk_packet_valid(request)) {
        reply(request, response, GMK_BAD_REQUEST, env);
        return;
    }
    if ((!is_health && !is_trial && request[4] > GMK_INFO) || !allowed(request, usb, env)) {
        reply(request, response, GMK_UNAVAILABLE, env);
        return;
    }
    bool             remote = usb && request[3] == 1;
    unsigned         index  = cache_index(request);
    k_spinlock_key_t key    = k_spin_lock(&lock);
    if (is_health && !remote) {
        health_reply(request, response, env);
    } else if (pending) {
        /* An unrelated request is never misidentified as the pending command.
         * STATUS polls the in-flight command only for that same target. */
        const uint8_t *busy = request;
        if (!is_health && request[4] == GMK_STATUS && queued[1] == request[1] && queued[3] == request[3]) busy = queued;
        reply(busy, response, GMK_BUSY, env);
    } else if (!is_health && request[4] == GMK_STATUS) {
        memcpy(response, last[index], 32);
        if (remote && response[5] == GMK_BUSY) {
            memcpy(queued, request, 32);
            queued_usb = usb;
            pending    = true;
        }
    } else if (remote && is_health && remote_health_valid && memcmp(request, remote_health_request, 32) == 0) {
        memcpy(response, remote_health, 32);

    } else {
        memcpy(queued, request, 32);
        queued_usb = usb;
        pending    = true;
        reply(request, response, GMK_BUSY, env);
    }
    k_spin_unlock(&lock, key);
}

void gmk_zmk_receiver_step(const gmk_zmk_update_env_t *env) {
    uint8_t          request[32], response[32];
    bool             usb;
    k_spinlock_key_t key = k_spin_lock(&lock);
    health               = *env;
    if (committed && (uint32_t)(env->uptime_ms - reset_at) >= 300) {
        k_spin_unlock(&lock, key);
        gmk_zmk_update_reset();
        return;
    }
    /* Read-only traffic and rejected packets do not extend an upload lease. */
    if (!committed && update.state != GMK_IDLE && (uint32_t)(env->uptime_ms - last_activity) > 10000) {
        update                   = (gmk_update_t){0};
        const uint8_t status[32] = {0x7f, 0x70, 1, !env->left, GMK_STATUS};
        gmk_update_response(&update, status, GMK_OK, last[!env->left]);
    }
    if (!pending) {
        k_spin_unlock(&lock, key);
        return;
    }
    memcpy(request, queued, 32);
    usb = queued_usb;
    k_spin_unlock(&lock, key);
    bool                remote    = usb && request[3] == 1;
    bool                is_health = request[1] == 0x72;
    gmk_update_result_t result    = GMK_UNAVAILABLE;
    if (!allowed(request, usb, env)) {
        reply(request, response, result, env);
    } else if (remote) {
        if (!gmk_wired_update(request, response)) reply(request, response, result, env);
    } else {
        result = gmk_update_process(&update, request);
        gmk_update_response(&update, request, result, response);
        response[7] = (env->left ? 1 : 0) | (env->left && env->usb_configured ? 2 : 0);
        if (request[4] == GMK_INFO && result == GMK_OK) {
            uint32_t size = gmk_running_size();
            gmk_write_u32(response + 16, size);
            /* Keep the ABI1 field layout; identify the actual running image. */
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
            const uint32_t running_base = GMK_STAGE_BASE;
#else
            const uint32_t running_base = GMK_APP_BASE;
#endif
            gmk_write_u32(response + 20, gmk_crc32(gmk_flash_view(running_base), size));
            gmk_write_u32(response + 28, gmk_crc32(gmk_flash_view(GMK_FLASH_BASE), GMK_BOOT_SIZE));
        }
        if (result == GMK_OK && !readonly(request)) last_activity = k_uptime_get_32();
    }
    key = k_spin_lock(&lock);
    if (remote && is_health) {
        memcpy(remote_health, response, 32);
        memcpy(remote_health_request, request, 32);
        /* BUSY requires another peer query; health never changes normal ACKs. */
        remote_health_valid = response[5] != GMK_BUSY;
    } else {
        unsigned index = cache_index(request);
        memcpy(last[index], response, 32);
    }
    if (update.state == GMK_COMMITTED && !committed) {
        committed = true;
        reset_at  = k_uptime_get_32();
    }
    pending = false;
    k_spin_unlock(&lock, key);
}
