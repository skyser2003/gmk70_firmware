/* SPDX-License-Identifier: GPL-2.0-or-later
 * RAM-only experimental payload. No flash API and no client code changes.
 * Link only into an actual, verified owned allocation. Build is not installation.
 */
#include <stddef.h>
#include <stdint.h>
#include "stock_abi.h"

_Static_assert(sizeof(gatt_attribute_t) == 28, "Stock attribute ABI");
_Static_assert(sizeof(gatt_service_t) == 12, "Stock service ABI");
_Static_assert(sizeof(gatt_ntf_t) == 12, "Stock by-value notification ABI");
_Static_assert(sizeof(gatt_msg_t) == 16, "Stock message ABI");
_Static_assert(offsetof(gatt_msg_t, param.msg.p_msg_data) == 12, "Stock payload ABI");

struct trial_state_t {
    uint32_t status;
    uint32_t forwarded;
    uint32_t notified;
    uint32_t dropped;
    uint8_t  service;
    uint8_t  connection;
    uint8_t  subscribed;
    uint8_t  reserved; /* Keep the trial statistics layout stable. */
    uint8_t  input[32];
};
volatile struct trial_state_t trial_state = {.service = 0xFF, .connection = 0xFF};

static const uint8_t hid_uuid[]   = {0x12, 0x18};
static const uint8_t hid_info[]   = {0x11, 0x01, 0x00, 0x02};
static const uint8_t report_map[] = {0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01, 0x09, 0x62, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x20, 0x75, 0x08, 0x81, 0x02, 0x09, 0x63, 0x95, 0x20, 0x75, 0x08, 0x91, 0x02, 0xC0};
static const uint8_t input_ref[] = {0, 1}, output_ref[] = {0, 2};
static const uint8_t report_protocol = 1;
uint16_t             vial_gatt(gatt_msg_t *msg);

#define ATT(u, properties, maximum, data) {{2, {(u) & 255, (u) >> 8}}, (properties), (maximum), (uint8_t *)(data)}
#define DECL ATT(0x2803, GATT_PROP_READ, 0, 0)
static const gatt_attribute_t attributes[]       = {ATT(0x2800, GATT_PROP_READ, 2, hid_uuid), DECL, ATT(0x2A4A, GATT_PROP_READ | GATT_PROP_AUTHEN, 4, 0), DECL, ATT(0x2A4C, GATT_PROP_WRITE | GATT_PROP_AUTHEN, 1, 0), DECL, ATT(0x2A4B, GATT_PROP_READ, sizeof(report_map), 0), DECL, ATT(0x2A4D, GATT_PROP_READ | GATT_PROP_NOTI, 32, 0), ATT(0x2902, GATT_PROP_READ | GATT_PROP_WRITE, 2, 0), ATT(0x2908, GATT_PROP_READ, 2, 0), DECL, ATT(0x2A4D, GATT_PROP_READ | GATT_PROP_WRITE, 32, 0), ATT(0x2908, GATT_PROP_READ, 2, 0), DECL, ATT(0x2A4E, GATT_PROP_READ | GATT_PROP_WRITE_CMD, 1, 0)};
static gatt_service_t         service_definition = {attributes, 16, vial_gatt};

static uint32_t lock_irqs(void) {
    uint32_t old;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(old)::"memory");
    return old;
}
static void unlock_irqs(uint32_t old) {
    __asm volatile("msr primask, %0" ::"r"(old) : "memory");
}
static uint32_t capacity(uint32_t size) {
    return ((uint32_t (*)(uint32_t, uint32_t))0xA64Du)(size, 3);
}
static uint16_t copy_reply(uint8_t *destination, const uint8_t *source, uint16_t size) {
    for (uint16_t i = 0; i < size; ++i)
        destination[i] = source[i];
    return size;
}

void radio_install(void) {
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    if (ipsr || trial_state.status || trial_state.service != 0xFF) return;
    uint32_t previous = lock_irqs();
    if (capacity(8192)) {
        trial_state.service = ((uint8_t (*)(gatt_service_t *))0x1000C571u)(&service_definition);
        trial_state.status  = trial_state.service == 0xFF ? 3 : 1; /* queued, not confirmed */
    } else {
        trial_state.status = 2;
    }
    unlock_irqs(previous);
}

uint16_t vial_gatt(gatt_msg_t *msg) {
    uint8_t *data   = msg->param.msg.p_msg_data;
    uint16_t length = msg->param.msg.msg_len;
    if (msg->msg_evt == GATTC_MSG_READ_REQ) {
        switch (msg->att_idx) {
            case 2:
                return copy_reply(data, hid_info, sizeof(hid_info));
            case 6:
                return copy_reply(data, report_map, sizeof(report_map));
            case 8:
                return copy_reply(data, (const uint8_t *)trial_state.input, 32);
            case 9:
                data[0] = trial_state.subscribed;
                data[1] = 0;
                return 2;
            case 10:
                return copy_reply(data, input_ref, 2);
            case 12:
                for (uint8_t i = 0; i < 32; ++i)
                    data[i] = 0;
                return 32;
            case 13:
                return copy_reply(data, output_ref, 2);
            case 15:
                return copy_reply(data, &report_protocol, 1);
            default:
                return 0;
        }
    }
    if (msg->msg_evt == GATTC_MSG_WRITE_REQ) {
        if (msg->att_idx == 9 && length == 2 && data[0] <= 1 && data[1] == 0) {
            trial_state.subscribed = data[0];
            trial_state.connection = msg->conn_idx;
        } else if (msg->att_idx == 12 && length == 32 && trial_state.subscribed && msg->conn_idx == trial_state.connection && trial_state.service != 0xFF) {
            ++trial_state.forwarded;
            ((void (*)(uint8_t *, uint32_t))0x10011C6Du)(data, 32);
        }
    } else if (msg->msg_evt == GATTC_MSG_LINK_LOST || msg->msg_evt == GATTC_MSG_LINK_CREATE) {
        trial_state.subscribed = 0;
        trial_state.connection = 0xFF;
    }
    return 0;
}

/* This is a raw response stream, not a transaction layer. Forward every
 * correctly framed stock response while subscribed, including late duplicates.
 * A one-bit pending flag loses replies when requests overlap. Only one
 * configuration client may use the shared FFF0/UART/HID route at a time.
 */
void radio_reply(const uint8_t *frame) {
    if (frame[0] != 0xAF || frame[1] != 0x61 || frame[2] != 32 || !trial_state.subscribed || trial_state.service == 0xFF) return;
    for (uint8_t i = 0; i < 32; ++i)
        trial_state.input[i] = frame[i + 3];
    gatt_ntf_t notification = {trial_state.connection, trial_state.service, 8, (uint8_t *)trial_state.input, 32};
    uint32_t   previous     = lock_irqs();
    if (capacity(256)) {
        ((void (*)(gatt_ntf_t))0x1000CC4Du)(notification);
        ++trial_state.notified;
    } else {
        ++trial_state.dropped;
    }
    unlock_irqs(previous);
}

/* Equivalent to gatt_svc_changed_req in the pinned vendor SDK. That function
 * was removed from the captured application by dead stripping. The captured
 * ke_msg_alloc/send veneers and this eight-byte SDK message ABI are explicit.
 * Invoke separately, after verifying actual service registration through GATT.
 */
void radio_refresh(void) {
    uint8_t connection = *(volatile uint8_t *)0x11003CD8u;
    if (connection >= 8 || trial_state.service == 0xFF || trial_state.status != 1) return;
    uint32_t previous = lock_irqs();
    if (capacity(256)) {
        uint8_t *message = ((uint8_t *(*)(uint16_t, uint16_t, uint16_t, uint16_t))0x10002299u)(0xC11, (uint16_t)(6 | (connection << 8)), 3, 8);
        message[0]       = 0x14;
        message[1]       = 0;
        message[2]       = 1;
        message[3]       = 0;
        message[4]       = 1;
        message[5]       = 0;
        message[6]       = 0xFF;
        message[7]       = 0xFF;
        ((void (*)(void *))0x100022ADu)(message);
    }
    unlock_irqs(previous);
}
