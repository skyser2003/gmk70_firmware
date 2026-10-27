# GMK70 ZMK integration provenance

The authoritative dependency revisions are in
[`dependencies/sources.lock.json`](../../dependencies/sources.lock.json).
`setup-zmk` explicitly fetches those exact commits; `build-zmk` only verifies and
uses existing clean checkouts. Dependencies are never patched by this port.

| Input | Exact source | Use |
| --- | --- | --- |
| ZMK | [4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85](https://github.com/zmkfirmware/zmk/tree/4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85) | Actual `app/`, keymap, matrix scanning, HID and USB application logic. MIT licenses remain in the checkout. |
| ZMK manifest | [app/west.yml](https://github.com/zmkfirmware/zmk/blob/4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85/app/west.yml) | Selects Zephyr `v4.1.0+zmk-fixes` and the LVGL commit below. |
| Zephyr | [10ba6d0cb38bc3d258775d27982f707599320085](https://github.com/zmkfirmware/zephyr/tree/10ba6d0cb38bc3d258775d27982f707599320085) | Cortex-M3 startup, kernel, SysTick, linker and legacy USB stack. Apache-2.0 and individual upstream licenses remain in the checkout. |
| Zephyr manifest | [west.yml](https://github.com/zmkfirmware/zephyr/blob/10ba6d0cb38bc3d258775d27982f707599320085/west.yml) | Selects CMSIS `d1b8b20b6278615b00e136374540eb1c00dcabe7`. |
| CMSIS | [d1b8b20b6278615b00e136374540eb1c00dcabe7](https://github.com/zephyrproject-rtos/cmsis/tree/d1b8b20b6278615b00e136374540eb1c00dcabe7) | ARM core definitions. Apache-2.0 license retained. |
| LVGL | [f1db87ee98f1810328a8419572fa42a3b5f352ae](https://github.com/zmkfirmware/lvgl/tree/f1db87ee98f1810328a8419572fa42a3b5f352ae) | Required Kconfig definitions referenced unconditionally by pinned ZMK; display code remains disabled. MIT license retained. |

`soc/westberry/wb32fq95/soc.c` adapts the clock and USB electrical sequences
from Westberry's Apache-2.0
[`WB32FQ95xx/hal_lld.c`](https://github.com/qmk/ChibiOS-Contrib/blob/5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767/os/hal/ports/WB32/WB32FQ95xx/hal_lld.c).
The GPIO register layout, RCC/cache bits, USB PHY address and IRQ number come
from the same pinned vendor-authored
[`wb32fq95xx.h`](https://github.com/qmk/ChibiOS-Contrib/blob/5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767/os/common/ext/CMSIS/WB32/WB32FQ95xx/wb32fq95xx.h).
The adapted file retains Westberry's copyright and Apache-2.0 identifier.
The new GPIO API implementation is maintained locally under the same license.
The upstream files remain unchanged in `dependencies/qmk/lib/chibios-contrib/`;
they are reference inputs, not linked into ZMK.

Pin assignments and left matrix positions are board facts recorded in the
maintained GMK70 `keyboard.json` and the feasibility document. The left trial
keymap is a new MIT ZMK keymap. It uses ordinary keys, a mute key, and no custom
radio or stock macro behavior. No QMK source tree is copied into this module.

The USB driver documents its controller semantics and vendor references in its
source header.
This local port does not claim eligibility for upstream ZMK's clean-room
contribution process.
