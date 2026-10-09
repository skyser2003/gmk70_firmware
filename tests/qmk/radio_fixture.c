// SPDX-License-Identifier: GPL-2.0-or-later
#include "radio_fixture.h"
#include "raw_hid.h"

radio_fixture_t radio_fixture = {.master = true, .state = MD_STATE_CONNECTED, .transport = TRANSPORT_USB};

bool is_keyboard_master(void) {
    return radio_fixture.master;
}
uint8_t *md_getp_state(void) {
    return &radio_fixture.state;
}
uint8_t *md_getp_indicator(void) {
    return &radio_fixture.indicator;
}
bool md_reports_inhibited(void) {
    return radio_fixture.state == MD_STATE_PAIRING;
}
void set_transport(transport_t transport) {
    radio_fixture.transport = transport;
}
transport_t get_transport(void) {
    return radio_fixture.transport;
}
void md_devs_change(uint8_t device, bool reset) {
    radio_fixture.selected_device = device;
    radio_fixture.pairing         = reset;
}
void md_send_kb(uint8_t *data) {
    memcpy(radio_fixture.keyboard, data, sizeof(radio_fixture.keyboard));
}
void md_send_nkro(uint8_t *data) {
    memcpy(radio_fixture.nkro, data, sizeof(radio_fixture.nkro));
}
void md_send_mouse(uint8_t *data) {
    memcpy(radio_fixture.mouse, data, sizeof(radio_fixture.mouse));
}
void md_send_consumer(uint8_t *data) {
    memcpy(&radio_fixture.consumer, data, sizeof(radio_fixture.consumer));
}
void md_send_system(uint8_t *data) {
    radio_fixture.system = data[0];
}
void md_send_raw(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    radio_fixture.radio_raw_count++;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    radio_fixture.usb_raw_count++;
}
void raw_hid_receive(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    radio_fixture.receive_raw_count++;
}
void                  last_matrix_activity_trigger(void) {}
void                  md_init(void) {}
void                  md_main_task(void) {}
void                  lpwr_task(void) {}
void                  lpwr_activity_trigger(void) {}
void                  usb_remote_wakeup(void) {}
static lpwr_state_t   power_state;
static lpwr_prepare_t power_class;
lpwr_state_t          lpwr_get_state(void) {
    return power_state;
}
void lpwr_set_state(lpwr_state_t value) {
    power_state = value;
}
lpwr_prepare_t lpwr_get_prepare_class(void) {
    return power_class;
}
void lpwr_set_prepare_class(lpwr_prepare_t value) {
    power_class = value;
}
bool md_inquire_bat(void) {
    return true;
}
uint32_t sync_timer_read32(void) {
    return 0;
}
uint32_t sync_timer_elapsed32(uint32_t last) {
    return 0u - last;
}
