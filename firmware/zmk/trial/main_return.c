// SPDX-License-Identifier: Apache-2.0
#include <cmsis_core.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

int __real_main(void);

/* Success proves actual ZMK main and the chosen matrix initialized. Do not feed IWDG. */
int __wrap_main(void) {
    int result = __real_main();
    if (result != 0 || !device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_kscan)))) {
        return result;
    }
    k_sleep(K_MSEC(500));
    __DSB();
    NVIC_SystemReset();
    return result;
}
