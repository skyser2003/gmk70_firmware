// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_v15_fixture.h"
stock_v15_fixture_t stock_v15_fixture;
uint8_t             gmk70_stock_sleep_get(void) {
    return stock_v15_fixture.sleep;
}
void gmk70_stock_sleep_set(uint8_t value) {
    stock_v15_fixture.sleep = value;
}
uint8_t gmk70_stock_nkro_get(void) {
    return stock_v15_fixture.nkro;
}
void gmk70_stock_nkro_set(uint8_t value) {
    stock_v15_fixture.nkro = value;
}
uint8_t gmk70_stock_brightness_get(void) {
    return stock_v15_fixture.brightness;
}
void gmk70_stock_brightness_set(uint8_t value) {
    stock_v15_fixture.brightness = value;
}
void gmk70_stock_activity(void) {
    ++stock_v15_fixture.activity;
}
void gmk70_stock_request_reset(void) {
    ++stock_v15_fixture.reset;
}
void gmk70_stock_request_bootloader(void) {
    ++stock_v15_fixture.boot;
}
