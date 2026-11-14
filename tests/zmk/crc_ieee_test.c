/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <zephyr/sys/crc.h>
uint32_t __wrap_crc32_ieee(const uint8_t *, size_t);
int      main(void) {
    assert(__wrap_crc32_ieee(NULL, 0) == 0);
    assert(__wrap_crc32_ieee((const uint8_t *)"123456789", 9) == 0xcbf43926U);
    uint8_t bytes[257];
    for (unsigned seed = 0; seed < 256; ++seed) {
        for (unsigned i = 0; i < sizeof(bytes); ++i)
            bytes[i] = (uint8_t)(seed ^ (i * 17U));
        for (size_t size = 0; size <= sizeof(bytes); ++size) {
            uint32_t expected = crc32_ieee(bytes, size);
            assert(__wrap_crc32_ieee(bytes, size) == expected);
            size_t half = size / 2;
            assert(crc32_ieee_update(crc32_ieee(bytes, half), bytes + half, size - half) == expected);
        }
    }
    puts("CRC32: shared resident/settings implementation matches pinned native split-frame CRC for 66048 byte vectors");
}
