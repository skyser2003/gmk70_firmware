/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <zephyr/drivers/sensor.h>
#include "../../dependencies/zmk/app/module/drivers/sensor/ec11/ec11.c"
#include "encoder.h"

static unsigned     pins = 3;
static unsigned     configured;
const struct device fake_port = {0};
int                 gpio_pin_get_dt(const struct gpio_dt_spec *pin) {
    return (pins >> pin->pin) & 1U;
}
int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags) {
    assert(pin->port == &fake_port && flags == GPIO_INPUT);
    ++configured;
    return 0;
}
static struct ec11_data         native_data;
static const struct ec11_config native_config = {
    .a          = {.port = &fake_port, .pin = 0},
    .b          = {.port = &fake_port, .pin = 1},
    .resolution = 1,
};
static const struct device native = {.data = &native_data, .config = &native_config, .api = &ec11_driver_api};

static void sample(struct gmk_encoder *queue, unsigned state) {
    pins = state;
    assert(ec11_driver_api.sample_fetch(&native, SENSOR_CHAN_ALL) == 0);
    struct sensor_value value;
    assert(ec11_driver_api.channel_get(&native, SENSOR_CHAN_ROTATION, &value) == 0);
    gmk_encoder_sample(queue, value.val1);
}
static void turn(struct gmk_encoder *queue, bool cw) {
    const unsigned clockwise[] = {2, 0, 1, 3}, counterclockwise[] = {1, 0, 2, 3};
    for (unsigned i = 0; i < 4; ++i)
        sample(queue, cw ? clockwise[i] : counterclockwise[i]);
}
int main(void) {
    assert(ec11_init(&native) == 0 && configured == 2);
    struct gmk_encoder queue;
    gmk_encoder_init(&queue, 4);
    turn(&queue, true);
    turn(&queue, false);
    assert(gmk_encoder_take(&queue) == 1 && gmk_encoder_take(&queue) == -1);
    assert(!gmk_encoder_take(&queue));
    sample(&queue, 2);
    sample(&queue, 3); /* Contact reversal produces no complete detent. */
    assert(!gmk_encoder_take(&queue));
    for (unsigned i = 0; i < GMK_ENCODER_QUEUE + 10; ++i)
        turn(&queue, (i & 1U) == 0);
    assert(queue.count == GMK_ENCODER_QUEUE);
    for (unsigned i = 0; i < GMK_ENCODER_QUEUE; ++i)
        assert(gmk_encoder_take(&queue) == ((i & 1U) ? -1 : 1));
    turn(&queue, true);
    gmk_encoder_clear(&queue);
    assert(!gmk_encoder_take(&queue));
    for (unsigned i = 0; i < 1000; ++i) {
        turn(&queue, true);
        assert(gmk_encoder_take(&queue) == 1);
    }
    /* Adopt upstream's invalid-transition policy: a two-bit jump contributes
     * zero ticks, preserving the partial detent rather than resetting it. */
    const unsigned ambiguous[] = {2, 1, 3, 2, 0};
    for (unsigned i = 0; i < sizeof(ambiguous) / sizeof(ambiguous[0]); ++i) {
        sample(&queue, ambiguous[i]);
        assert(gmk_encoder_take(&queue) == (i == 4 ? 1 : 0));
    }
    puts("Pinned native EC11: direction, reversal, FIFO, overflow and clear passed");
}
