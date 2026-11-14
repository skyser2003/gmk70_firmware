/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../qmk-common/keyboards/zuoya/gmk70/update/image.h"

/* Native split framing and the resident/settings format use the same IEEE
 * CRC32. Share its existing implementation without retaining a second table.
 * This public --wrap thunk and Zephyr's definition stay outside LTO. */
uint32_t __wrap_crc32_ieee(const uint8_t *data, size_t size) {
    return gmk_crc32(data, size);
}
