/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "split.h"
#include "wired.h"
#include "wired_protocol.h"
#ifdef CONFIG_GMK70_NATIVE_UPDATE_WIRE
#    include "native_update_wire.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/physical_layouts.h>
#include <zmk/split/transport/central.h>
#include <zmk/split/transport/peripheral.h>

/* Encode the native command fields into one board RPC (not its padded C ABI).
 * A queue of two preserves an ordinary press/release pair without another
 * thread stack. Updater and settings work retain the dedicated worker. */
K_MSGQ_DEFINE(native_commands, sizeof(struct zmk_split_transport_central_command), 2, 4);

extern struct zmk_split_transport_central    gmk_native_central;
extern struct zmk_split_transport_peripheral gmk_native_peripheral;

static struct zmk_split_transport_status central_status(void) {
    return (struct zmk_split_transport_status){.available = gmk_wired_is_left(), .enabled = gmk_wired_is_left()};
}
static struct zmk_split_transport_status peripheral_status(void) {
    return (struct zmk_split_transport_status){.available = !gmk_wired_is_left(), .enabled = !gmk_wired_is_left()};
}
static int source_ids(uint8_t *sources) {
    sources[0] = 0;
    return gmk_wired_is_left() ? 1 : 0;
}
static int send_command(uint8_t source, struct zmk_split_transport_central_command command) {
    if (source != 0 || !gmk_wired_is_left() || !gmk_split_available()) return -ENODEV;
    if (command.type != ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_INVOKE_BEHAVIOR) return -ENOTSUP;
    return k_msgq_put(&native_commands, &command, K_NO_WAIT);
}
static int report_event(const struct zmk_split_transport_peripheral_event *event) {
    ARG_UNUSED(event);
    /* Board scanner already publishes the debounced matrix atomically for
     * legacy/new peers. Snapshot delivery repairs lost transitions and clears
     * held keys after the existing 100 ms peer timeout. */
    return !gmk_wired_is_left() && gmk_split_available() ? 0 : -ENODEV;
}
static const struct zmk_split_transport_central_api    central_api    = {.send_command = send_command, .get_available_source_ids = source_ids, .get_status = central_status};
static const struct zmk_split_transport_peripheral_api peripheral_api = {.report_event = report_event, .get_status = peripheral_status};
ZMK_SPLIT_TRANSPORT_CENTRAL_REGISTER(gmk_native_central, &central_api, 0);
ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER(gmk_native_peripheral, &peripheral_api, 0);

static bool remote_position(uint32_t position) {
    const struct zmk_physical_layout *const *layouts;
    size_t                                   count    = zmk_physical_layouts_get_list(&layouts);
    int                                      selected = zmk_physical_layouts_get_selected();
    if (selected < 0 || (size_t)selected >= count) return false;
    for (unsigned row = 5; row < 10; ++row)
        for (unsigned col = 0; col < 9; ++col) {
            int32_t mapped = zmk_matrix_transform_row_column_to_position(layouts[selected]->matrix_transform, row, col);
            if (mapped >= 0 && (uint32_t)mapped == position) return true;
        }
    return false;
}

int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed event);
int __wrap_raise_zmk_position_state_changed(struct zmk_position_state_changed event) {
    if (gmk_wired_is_left() && event.source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL && remote_position(event.position)) {
        struct zmk_split_transport_peripheral_event native = {.type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_KEY_POSITION_EVENT, .data.key_position_event = {.position = event.position, .pressed = event.state}};
        /* Native core re-raises with source 0; that second call goes directly
         * to the native event manager, once, on the original ZMK workqueue. */
        return zmk_split_transport_central_peripheral_event_handler(&gmk_native_central, 0, native);
    }
    return __real_raise_zmk_position_state_changed(event);
}

void gmk_native_split_receive(const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
#ifdef CONFIG_GMK70_NATIVE_UPDATE_WIRE
    if (!gmk_wired_is_left() && gmk_wired_mode() && gmk_native_update_capability(request, response)) return;
#endif
    if (gmk_wired_is_left() || !gmk_wired_mode() || request[0] != 1 || request[30] > 1 || request[31] != gmk_wire_crc8(request, 31) || !memchr(request + 1, '\0', 16)) return;
    struct zmk_split_transport_central_command command = {.type = ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_INVOKE_BEHAVIOR};
    memcpy(command.data.invoke_behavior.behavior_dev, request + 1, 16);
    command.data.invoke_behavior.param1       = sys_get_le32(request + 17);
    command.data.invoke_behavior.param2       = sys_get_le32(request + 21);
    command.data.invoke_behavior.position     = sys_get_le32(request + 25);
    command.data.invoke_behavior.event_source = request[29];
    command.data.invoke_behavior.state        = request[30];
    if (k_msgq_put(&native_commands, &command, K_NO_WAIT) == 0) response[0] = 0x5a;
}

void gmk_native_split_poll(void) {
    struct zmk_split_transport_central_command command;
    if (k_msgq_get(&native_commands, &command, K_NO_WAIT)) return;
    if (!gmk_split_available()) return;
    if (gmk_wired_is_left()) {
        if (command.type != ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_INVOKE_BEHAVIOR) return;
        uint8_t request[32] = {1}, response[32];
        memcpy(request + 1, command.data.invoke_behavior.behavior_dev, 16);
        sys_put_le32(command.data.invoke_behavior.param1, request + 17);
        sys_put_le32(command.data.invoke_behavior.param2, request + 21);
        sys_put_le32(command.data.invoke_behavior.position, request + 25);
        request[29] = command.data.invoke_behavior.event_source;
        request[30] = command.data.invoke_behavior.state;
        request[31] = gmk_wire_crc8(request, 31);
        /* Never retry an ambiguous behavior command (reset included). An old
         * peer returns zero rather than the new acceptance marker. */
        (void)gmk_wired_split_command(request, response);
    } else {
        (void)zmk_split_transport_peripheral_command_handler(&gmk_native_peripheral, command);
    }
}
