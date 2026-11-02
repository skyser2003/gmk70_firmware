/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "encoder.h"
static void turn(struct gmk_encoder *e, bool cw) {
    for (unsigned i = 0; i < 4; ++i)
        gmk_encoder_sample(e, cw ? -1 : 1);
}
int main(void) {
    struct gmk_encoder e;
    gmk_encoder_init(&e, 4);
    turn(&e, true);
    turn(&e, false);
    assert(gmk_encoder_take(&e) == 1 && gmk_encoder_take(&e) == -1 && !gmk_encoder_take(&e));
    gmk_encoder_sample(&e, 1);
    gmk_encoder_sample(&e, -1);
    assert(!gmk_encoder_take(&e));
    gmk_encoder_sample(&e, INT32_MAX);
    gmk_encoder_sample(&e, INT32_MIN);
    assert(!gmk_encoder_take(&e) && !e.pulses);
    for (unsigned i = 0; i < GMK_ENCODER_QUEUE + 10; ++i)
        turn(&e, (i & 1) == 0);
    assert(e.count == GMK_ENCODER_QUEUE);
    for (unsigned i = 0; i < GMK_ENCODER_QUEUE; ++i)
        assert(gmk_encoder_take(&e) == ((i & 1) ? -1 : 1));
    assert(!gmk_encoder_take(&e));
    turn(&e, true);
    gmk_encoder_clear(&e);
    assert(!gmk_encoder_take(&e));
    for (unsigned i = 0; i < 1000; ++i) {
        turn(&e, true);
        assert(gmk_encoder_take(&e) == 1);
    }
    puts("encoder native tick/reversal/overflow/queue order tests passed");
}
