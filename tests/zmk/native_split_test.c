/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <zephyr/sys/util.h>
#include "../../firmware/zmk/wired/split.c"
#include "../../dependencies/zmk/app/src/split/central.c"
#include <zmk/events/sensor_event.h>

int64_t         fake_ms = 20;
static bool     left = true, available = true, host_wired = true;
static int      selected;
static unsigned events, commands, invoked, remote_state, capability_calls;
static unsigned scheduling_notifications;
void            gmk_service_notify(void) {
    ++scheduling_notifications;
}
void gmk_scan_notify(void) {}
#ifdef CONFIG_GMK70_POWER
static unsigned power_calls;
void            gmk_power_receive(const uint8_t request[32], uint8_t response[32]) {
    assert(request[0] == 'P' && request[1] == 'W' && request[2] == 'R');
    ++power_calls;
    response[0] = 0x73;
}
#endif
static uint8_t                           transmitted[32];
static struct zmk_position_state_changed last_event;
static struct zmk_behavior_binding       last_binding;
static char                              last_name[16];

bool gmk_wired_is_left(void) {
    return left;
}
bool gmk_wired_mode(void) {
    return available && host_wired;
}
bool gmk_split_available(void) {
    return available;
}
bool gmk_native_update_capability(const uint8_t request[32], uint8_t response[32]) {
    ++capability_calls;
    if (request[0] != 0x42) return false;
    memset(response, 0xcc, 32);
    return true;
}
bool gmk_wired_split_command(const uint8_t request[32], uint8_t response[32]) {
    ++commands;
    memcpy(transmitted, request, 32);
    memset(response, 0, 32);
    response[0] = 0x5a;
    return true;
}
const struct zmk_split_transport_central *test_first_zmk_split_transport_central(void) {
    return &gmk_native_central;
}
const struct zmk_split_transport_peripheral *test_first_zmk_split_transport_peripheral(void) {
    return &gmk_native_peripheral;
}
int k_msgq_put(struct k_msgq *queue, const void *value, int timeout) {
    assert(timeout == K_NO_WAIT);
    if (queue->count == 2) return -ENOSPC;
    size_t width = sizeof(struct zmk_split_transport_central_command);
    memcpy(queue->bytes + (queue->tail++ % 2) * width, value, width);
    ++queue->count;
    return 0;
}
int k_msgq_get(struct k_msgq *queue, void *value, int timeout) {
    assert(timeout == K_NO_WAIT);
    if (!queue->count) return -EAGAIN;
    size_t width = sizeof(struct zmk_split_transport_central_command);
    memcpy(value, queue->bytes + (queue->head++ % 2) * width, width);
    --queue->count;
    return 0;
}
int zmk_event_manager_raise(zmk_event_t *event) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    assert(position);
    last_event = *position;
    ++events;
    return 0;
}
static const struct zmk_physical_layout        layout;
static const struct zmk_physical_layout *const layouts[] = {&layout};
size_t                                         zmk_physical_layouts_get_list(const struct zmk_physical_layout *const **result) {
    *result = layouts;
    return 1;
}
int zmk_physical_layouts_get_selected(void) {
    return selected;
}
int zmk_physical_layouts_select(uint8_t index) {
    return index ? -EINVAL : 0;
}
int32_t zmk_matrix_transform_row_column_to_position(zmk_matrix_transform_t transform, uint32_t row, uint32_t col) {
    (void)transform;
    return row == 5 && col == 1 ? 8 : -EINVAL;
}
static int invoke(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event, bool pressed) {
    ++invoked;
    last_binding = *binding;
    memcpy(last_name, binding->behavior_dev, sizeof(last_name));
    assert(event.position == 8 && event.timestamp == fake_ms);
    remote_state = pressed;
    return 0;
}
int behavior_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return invoke(binding, event, true);
}
int behavior_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return invoke(binding, event, false);
}

static void positions(void) {
    assert(central_init() == 0);
    struct zmk_position_state_changed event = {.source = UINT8_MAX, .position = 8, .state = true, .timestamp = 1};
    assert(raise_zmk_position_state_changed(event) == 0);
    assert(events == 1 && last_event.source == 0 && last_event.timestamp == fake_ms && last_event.state);
    event.state = false; /* Snapshot timeout release follows the same core. */
    assert(raise_zmk_position_state_changed(event) == 0);
    assert(events == 2 && !last_event.state && last_event.source == 0);
    event.position = 0;
    assert(raise_zmk_position_state_changed(event) == 0);
    assert(events == 3 && last_event.source == UINT8_MAX && last_event.timestamp == 1);
    event.position = 72; /* Studio knob directions stay local. */
    assert(raise_zmk_position_state_changed(event) == 0 && last_event.source == UINT8_MAX);
    left           = false;
    event.position = 8;
    assert(raise_zmk_position_state_changed(event) == 0 && last_event.source == UINT8_MAX);
    assert(!central_status().available && peripheral_status().available);
    left     = true;
    selected = -1;
    assert(raise_zmk_position_state_changed(event) == 0 && last_event.source == UINT8_MAX);
    selected = 0;
}
static void behavior_commands(void) {
    struct zmk_behavior_binding       binding = {.behavior_dev = "test", .param1 = 0x12345678, .param2 = 42};
    struct zmk_behavior_binding_event event   = {.source = 0, .position = 8};
    assert(zmk_split_central_invoke_behavior(0, &binding, event, true) == 0);
    assert(zmk_split_central_invoke_behavior(0, &binding, event, false) == 0);
    assert(zmk_split_central_invoke_behavior(0, &binding, event, true) == -ENOSPC);
    assert(commands == 0);        /* Event callbacks never block on UART. */
    scheduling_notifications = 0; /* The worker consumed the coalesced wake. */
    gmk_native_split_poll();
    assert(scheduling_notifications == 1); /* The queued release gets another pass. */
    assert(commands == 1 && transmitted[0] == 1 && transmitted[30] == 1 && transmitted[31] == gmk_wire_crc8(transmitted, 31));
    uint8_t press[32], release[32], response[32];
    memcpy(press, transmitted, 32);
    gmk_native_split_poll();
    memcpy(release, transmitted, 32);
    assert(commands == 2 && !release[30]);
    scheduling_notifications = 0;
    gmk_native_split_poll();
    assert(commands == 2 && !scheduling_notifications); /* No empty-queue wake loop. */
    gmk_native_split_receive(press, response);
    assert(!response[0]); /* LEFT may not execute a peripheral command. */
    left      = false;
    available = false;
    gmk_native_split_receive(press, response);
    assert(!response[0]);
    available = true;
    gmk_native_split_receive(press, response);
    assert(response[0] == 0x5a);
    gmk_native_split_receive(release, response);
    assert(response[0] == 0x5a);
    gmk_native_split_receive(press, response);
    assert(!response[0] && !invoked);
    gmk_native_split_poll();
    assert(invoked == 1 && remote_state == 1 && !strcmp(last_name, "test"));
    assert(last_binding.param1 == 0x12345678 && last_binding.param2 == 42);
    gmk_native_split_poll();
    assert(invoked == 2 && !remote_state);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        uint8_t malformed[32];
        memcpy(malformed, press, 32);
        if (invalid == 0) malformed[0] = 2;
        if (invalid == 1) malformed[30] = 2;
        if (invalid == 2) malformed[17] ^= 1;
        if (invalid == 3) memset(malformed + 1, 'x', 16);
        if (invalid != 2) malformed[31] = gmk_wire_crc8(malformed, 31);
        gmk_native_split_receive(malformed, response);
        assert(!response[0] && !native_commands.count);
    }
}
int main(void) {
    uint8_t probe[32] = {0x42}, response[32];
    gmk_native_split_receive(probe, response);
    assert(!response[0] && !capability_calls);
    left      = false;
    available = false;
    gmk_native_split_receive(probe, response);
    assert(!response[0] && !capability_calls);
    available = true;
    gmk_native_split_receive(probe, response);
    assert(response[0] == 0xcc && capability_calls == 1 && !native_commands.count);
#ifdef CONFIG_GMK70_POWER
    uint8_t power[32] = {'P', 'W', 'R'};
    host_wired        = false; /* Split power negotiation also serves Bluetooth/dongle. */
    gmk_native_split_receive(power, response);
    assert(response[0] == 0x73 && power_calls == 1 && !native_commands.count);
    available = false;
    gmk_native_split_receive(power, response);
    assert(!response[0] && power_calls == 1);
    available = true;
    left      = true;
    gmk_native_split_receive(power, response);
    assert(!response[0] && power_calls == 1);
    host_wired = true;
#endif
    left = true;
    positions();
    behavior_commands();
    puts("Pinned native split cores: source identity, roles, deferred commands, codec and admission passed");
}
