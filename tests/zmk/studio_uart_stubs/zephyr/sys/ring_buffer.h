#pragma once
#include <stdint.h>
struct ring_buf {
    uint32_t size, count;
    uint8_t  data[512];
};
uint32_t               ring_buf_put_claim(struct ring_buf *, uint8_t **, uint32_t);
int                    ring_buf_put_finish(struct ring_buf *, uint32_t);
static inline uint32_t ring_buf_size_get(struct ring_buf *b) {
    return b->count;
}
static inline uint32_t ring_buf_capacity_get(struct ring_buf *b) {
    return b->size;
}
static inline uint32_t ring_buf_get_claim(struct ring_buf *b, uint8_t **p, uint32_t n) {
    (void)b;
    (void)p;
    (void)n;
    return 0;
}
static inline int ring_buf_get_finish(struct ring_buf *b, uint32_t n) {
    (void)b;
    (void)n;
    return 0;
}
