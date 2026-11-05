/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/cbprintf.h>

int gmk_native_sprintf(char *destination, const char *format, ...);
int gmk_native_snprintf(char *destination, size_t size, const char *format, ...);

struct output {
    char   bytes[64];
    size_t length;
};
static int emit(int character, void *context) {
    struct output *output = context;
    assert(output->length < sizeof(output->bytes) - 1);
    output->bytes[output->length++] = (char)character;
    output->bytes[output->length]   = '\0';
    return 0;
}
static void native_settings_names_and_integer_ranges(void) {
    char name[32];
    assert(gmk_native_sprintf(name, "behavior/local_id/%d", (int)UINT16_MAX) == 23);
    assert(!strcmp(name, "behavior/local_id/65535"));
    assert(gmk_native_sprintf(name, "keymap/l/%d/%d", 15, 73) == 14);
    assert(!strcmp(name, "keymap/l/15/73"));
    assert(gmk_native_sprintf(name, "keymap/l_n/%d", 15) == 13);
    assert(!strcmp(name, "keymap/l_n/15"));
    assert(gmk_native_sprintf(name, "%d", INT_MIN) == 11);
    assert(!strcmp(name, "-2147483648"));
    assert(gmk_native_sprintf(name, "%u", UINT_MAX) == 10);
    assert(!strcmp(name, "4294967295"));
}
static void native_device_copy_termination_and_would_write_count(void) {
    struct {
        uint8_t before;
        char    name[32];
        uint8_t after;
    } output           = {.before = 0x5a, .after = 0xa5};
    const char *device = "a_device_name_that_is_longer_than_32_bytes";
    int         count  = gmk_native_snprintf(output.name, sizeof(output.name), "%s", device);
    assert(count == (int)strlen(device));
    assert(!memcmp(output.name, device, 31) && output.name[31] == '\0');
    assert(output.before == 0x5a && output.after == 0xa5);
    output.name[0] = 'X';
    assert(gmk_native_snprintf(output.name, 0, "%s", "USB") == 3 && output.name[0] == 'X');
    assert(gmk_native_snprintf(NULL, 0, "%s", "USB") == 3);
    assert(gmk_native_snprintf(output.name, 1, "%s", "USB") == 3 && output.name[0] == '\0');
    assert(gmk_native_snprintf(output.name, sizeof(output.name), "%s", "gmk_controls") == 12);
    assert(!strcmp(output.name, "gmk_controls"));
}
static void native_endpoint_strings_and_callback_counts(void) {
    char name[8];
    assert(gmk_native_snprintf(name, sizeof(name), "None") == 4 && !strcmp(name, "None"));
    assert(gmk_native_snprintf(name, sizeof(name), "USB") == 3 && !strcmp(name, "USB"));
    assert(gmk_native_snprintf(name, sizeof(name), "Invalid") == 7 && !strcmp(name, "Invalid"));
    assert(gmk_native_snprintf(name, sizeof(name), "BLE:%d", 255) == 7 && !strcmp(name, "BLE:255"));
    struct output output   = {0};
    const char   *expected = "behavior/local_id/65535:mouse_move";
    int           count    = cbprintf(emit, &output, "behavior/local_id/%d:%s", (int)UINT16_MAX, "mouse_move");
    assert(count == (int)strlen(expected) && output.length == strlen(expected));
    assert(!strcmp(output.bytes, expected));
}
int main(void) {
    _Static_assert(sizeof(int) == 4, "Fixture expects the board's 32-bit integer argument domain");
    native_settings_names_and_integer_ranges();
    native_device_copy_termination_and_would_write_count();
    native_endpoint_strings_and_callback_counts();
    puts("Native nano formatting: settings/device names, integer bounds, truncation and return counts passed");
    return 0;
}
