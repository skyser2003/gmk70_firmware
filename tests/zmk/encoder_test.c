/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "encoder.h"
static void turn(struct gmk_encoder *e, bool cw) {
    const uint8_t positive[4] = {2, 0, 1, 3}, negative[4] = {1, 0, 2, 3};
    for (unsigned i = 0; i < 4; ++i)
        gmk_encoder_sample(e, cw ? positive[i] : negative[i]);
}
int main(void) {
    struct gmk_encoder e;
    gmk_encoder_init(&e, 3, 4);
    turn(&e, true);
    turn(&e, false);
    assert(gmk_encoder_take(&e) == 1 && gmk_encoder_take(&e) == -1 && !gmk_encoder_take(&e));
    gmk_encoder_sample(&e, 2);
    gmk_encoder_sample(&e, 3);
    assert(!gmk_encoder_take(&e));
    gmk_encoder_sample(&e, 0);
    gmk_encoder_sample(&e, 3);
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
    puts("encoder Gray code/bounce/reversal/overflow/queue order tests passed");
}
