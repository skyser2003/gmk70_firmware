/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal functional ABI bindings for the captured FR800x application.
 * Layouts/constants were checked against captured instructions and the pinned
 * SDK header noted in README.md. No vendor implementation is linked or copied.
 */
#pragma once
#include <stdint.h>

enum {
    GATT_PROP_READ        = 1u << 1,
    GATT_PROP_WRITE_CMD   = 1u << 2,
    GATT_PROP_NOTI        = 1u << 4,
    GATT_PROP_WRITE       = 1u << 8,
    GATT_PROP_AUTHEN      = 1u << 9,
    GATTC_MSG_READ_REQ    = 0,
    GATTC_MSG_WRITE_REQ   = 1,
    GATTC_MSG_LINK_CREATE = 7,
    GATTC_MSG_LINK_LOST   = 8,
};

typedef struct {
    uint8_t size;
    uint8_t p_uuid[16];
} gatt_uuid_t;

typedef struct {
    gatt_uuid_t uuid;
    uint16_t    prop;
    uint16_t    max_size;
    uint8_t    *p_data;
} gatt_attribute_t;

typedef struct {
    uint8_t  msg_evt;
    uint8_t  conn_idx;
    uint8_t  svc_id;
    uint16_t att_idx;
    uint16_t handle;
    union {
        struct {
            uint16_t msg_len;
            uint8_t *p_msg_data;
        } msg;
        struct {
            uint8_t operation;
            uint8_t status;
            void   *arg;
        } op;
    } param;
} gatt_msg_t;

typedef struct {
    const gatt_attribute_t *p_att_tb;
    uint8_t                 att_nb;
    uint16_t (*gatt_msg_handler)(gatt_msg_t *);
} gatt_service_t;

typedef struct {
    uint8_t  conidx;
    uint8_t  svc_id;
    uint8_t  att_idx;
    uint8_t *p_data;
    uint16_t data_len;
} gatt_ntf_t;
