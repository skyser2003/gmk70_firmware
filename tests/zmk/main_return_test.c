// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <zephyr/device.h>

struct device test_kscan;
static int    step, result;
static bool   ready;
int           __wrap_main(void);
int           __real_main(void) {
    assert(step++ == 0);
    return result;
}
bool device_is_ready(const struct device *device) {
    assert(step++ == 1);
    assert(device == &test_kscan);
    return ready;
}
int k_sleep(int milliseconds) {
    assert(step++ == 2);
    assert(milliseconds == 500);
    return 0;
}
void test_dsb(void) {
    assert(step++ == 3);
}
void test_reset(void) {
    assert(step++ == 4);
}
int main(void) {
    result = -5;
    ready  = true;
    step   = 0;
    assert(__wrap_main() == -5);
    assert(step == 1);
    result = 0;
    ready  = false;
    step   = 0;
    assert(__wrap_main() == 0);
    assert(step == 2);
    ready = true;
    step  = 0;
    assert(__wrap_main() == 0);
    assert(step == 5);
    puts("ZMK post-main reset gates and ordering passed");
}
