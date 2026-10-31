/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nkro_boot_native.h"

#ifdef TEST_ADAPTER
#    include "../../firmware/zmk/nkro/nkro.c"
uint32_t radio_test_time;
unsigned radio_test_locked;
bool     gmk_wired_is_left(void) {
    return true;
}
void gmk_radio_release(bool consumer) {
    (void)consumer;
    assert(!radio_test_locked);
}
int zmk_endpoint_send_report(uint16_t usage_page) {
    assert(!radio_test_locked && (usage_page == 7 || usage_page == 12));
    return -ENOTCONN;
}
#endif

static void assert_boot_keys(const zmk_hid_boot_report_t *report, uint8_t first) {
    assert(report->keys[0] == first);
    for (unsigned index = 1; index < HID_BOOT_KEY_LEN; ++index)
        assert(report->keys[index] == 0);
}

static void clear_discards_boot_rollover(void) {
    for (uint32_t usage = 4; usage <= 10; ++usage)
        assert(zmk_hid_keyboard_press(usage) == 0);
    const zmk_hid_boot_report_t *report = zmk_hid_get_boot_report();
    for (unsigned index = 0; index < HID_BOOT_KEY_LEN; ++index)
        assert(report->keys[index] == HID_ERROR_ROLLOVER);
    zmk_hid_keyboard_clear();
    report = zmk_hid_get_boot_report();
#ifdef TEST_ADAPTER
    assert_boot_keys(report, 0);
    assert(zmk_hid_keyboard_press(4) == 0);
    assert_boot_keys(zmk_hid_get_boot_report(), 4);
#else
    // Pinned native clear only resets its bitmap, leaving keys_held == 7.
    for (unsigned index = 0; index < HID_BOOT_KEY_LEN; ++index)
        assert(report->keys[index] == HID_ERROR_ROLLOVER);
#endif
}

static void duplicate_release_recovers_for_next_key(uint32_t duplicate_usage) {
    // Duplicate physical Space or B positions can release a usage before
    // replacing it and later generate another release of an already-empty bit.
    assert(zmk_hid_keyboard_press(duplicate_usage) == 0);
    assert(zmk_hid_keyboard_release(duplicate_usage) == 0);
    assert(zmk_hid_keyboard_press(duplicate_usage) == 0);
    assert(zmk_hid_keyboard_release(duplicate_usage) == 0);
    assert(zmk_hid_keyboard_release(duplicate_usage) == 0);
    const zmk_hid_boot_report_t *report = zmk_hid_get_boot_report();
#ifdef TEST_ADAPTER
    assert_boot_keys(report, 0);
#else
    // Empty deselection underflows the native uint8_t counter to 255.
    for (unsigned index = 0; index < HID_BOOT_KEY_LEN; ++index)
        assert(report->keys[index] == HID_ERROR_ROLLOVER);
#endif
    assert(zmk_hid_keyboard_press(4) == 0);
#ifdef TEST_ADAPTER
    assert_boot_keys(zmk_hid_get_boot_report(), 4);
    assert(zmk_hid_keyboard_release(4) == 0);
    assert_boot_keys(zmk_hid_get_boot_report(), 0);
    assert(!gmk_nkro_set(false));
    assert(zmk_hid_keyboard_press(5) == 0);
    assert_boot_keys(zmk_hid_get_boot_report(), 5);
    assert(!gmk_nkro_set(true));
    assert_boot_keys(zmk_hid_get_boot_report(), 5);
#else
    // The A press wraps 255 to zero, so native boot packing omits the held A.
    assert_boot_keys(zmk_hid_get_boot_report(), 0);
#endif
}

static void bitmap_usages_and_modifiers_reach_boot_report(void) {
    const uint8_t usages[] = {4, 44, 115, 151};
    assert(zmk_hid_keyboard_press(224) == 1);
    assert(zmk_hid_keyboard_press(229) == 1);
    for (unsigned index = 0; index < sizeof(usages); ++index)
        assert(zmk_hid_keyboard_press(usages[index]) == 0);
    const zmk_hid_boot_report_t *report = zmk_hid_get_boot_report();
    assert(report->modifiers == 0x21 && report->_reserved == 0);
    for (unsigned index = 0; index < sizeof(usages); ++index)
        assert(report->keys[index] == usages[index]);
    assert(report->keys[4] == 0 && report->keys[5] == 0);
    for (unsigned index = 0; index < sizeof(usages); ++index)
        assert(zmk_hid_keyboard_release(usages[index]) == 0);
    assert(zmk_hid_keyboard_release(224) == 1);
    assert(zmk_hid_keyboard_release(229) == 1);
    report = zmk_hid_get_boot_report();
    assert_boot_keys(report, 0);
    assert(report->modifiers == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
#ifdef TEST_ADAPTER
    assert(registered_init() == 0 && gmk_nkro_enabled());
#endif
    if (strcmp(argv[1], "clear") == 0)
        clear_discards_boot_rollover();
    else if (strcmp(argv[1], "duplicate") == 0)
        duplicate_release_recovers_for_next_key(44);
    else if (strcmp(argv[1], "duplicate-b") == 0)
        duplicate_release_recovers_for_next_key(5);
    else if (strcmp(argv[1], "bitmap") == 0)
        bitmap_usages_and_modifiers_reach_boot_report();
    else
        assert(false);
    return 0;
}
