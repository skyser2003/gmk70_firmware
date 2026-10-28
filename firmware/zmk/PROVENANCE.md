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
| nanopb | [7307ce399b81ddcb3c3a5dc862c52d4754328d38](https://github.com/zephyrproject-rtos/nanopb/tree/7307ce399b81ddcb3c3a5dc862c52d4754328d38) | Exact pinned Zephyr optional-manifest revision for Studio protobuf encoding. Upstream license retained. |
| Studio messages | [6cb4c283e76209d59c45fbcb218800cd19e9339d](https://github.com/zmkfirmware/zmk-studio-messages/tree/6cb4c283e76209d59c45fbcb218800cd19e9339d) | Exact pinned ZMK manifest revision, consumed directly for ordinary Studio RPC. MIT license retained. |

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

`update/wb32_flash.c` and `update/wb32_flash_lowlevel.h` adapt Westberry's
Apache-2.0 `hal_efl_lld.c` from Vial's independently pinned ChibiOS-Contrib
[`3ac181e4ca5cafddaf8b472baa1d09c2b24c77b6`](https://github.com/qmk/ChibiOS-Contrib/blob/3ac181e4ca5cafddaf8b472baa1d09c2b24c77b6/os/hal/ports/WB32/WB32FQ95xx/hal_efl_lld.c),
read locally from
`dependencies/vial-qmk/lib/chibios-contrib/os/hal/ports/WB32/WB32FQ95xx/hal_efl_lld.c`.
The exact vendor PRE_OP instruction words and three-word RAM operation helper
are retained with Westberry copyright and Apache-2.0 identification. The native
adapter supports exactly one 256-byte page at a time, preserves the vendor
pre-operation calibration and erase/latch/program sequences, verifies readback,
and restores IRQ/FHSI/PCLK/APB2/RNG clock-gate state. It does not write protection
or option bytes. The upstream source remains unchanged and is not linked into
ZMK; no QMK HAL runtime is required.

`update/wb32_watchdog.c` uses the WB32 register contract and initialization
ordering documented in the maintained GPL-2.0-or-later
`firmware/qmk-common/keyboards/zuoya/gmk70/update/trial_watchdog.c` and Westberry
EN_RM2905025_WB32FQ95xx_V01, sections 5.4.9/10, 7.2.12, 7.3.11/16, 11.4.6/7,
18 and 27.4.2. The new adapter retains that GPL identifier. Permanent firmware
establishes the longest IWDG period for both stopped and inherited running
watchdogs; staging qualification neither initializes nor feeds IWDG.
