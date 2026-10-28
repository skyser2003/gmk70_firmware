/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

/* qmk-common/keyboards/zuoya/gmk70/config.h: USB_POWER_EN_PIN B1.
 * qmk-common/drivers/wireless/transport.c identifies this active-low control
 * as power to the external USB D+ pull-up resistor. */
#define GMK70_USB_POWER_PIN 1

static int usb_power_init(void) {
    const struct device *const port = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    if (!device_is_ready(port)) return -ENODEV;
    /* Preload low before enabling output, matching the maintained board. */
    return gpio_pin_configure(port, GMK70_USB_POWER_PIN, GPIO_OUTPUT_LOW);
}

SYS_INIT(usb_power_init, APPLICATION, 89);
