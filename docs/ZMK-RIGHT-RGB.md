# Right-half RGB restoration

The dark right LEDs were caused by migration build settings: `peripheral.conf`
disabled both native RGB underglow and the WB32 LED driver, while `studio.conf`
set `GMK70_RGB_LOCAL_ONLY`, suppressing all peer frames. The right scanner and
updater still worked because their code does not depend on lighting.

Source `0fcf8f2` enables the existing driver in the peripheral and turns off
local-only isolation in Studio. The normal canonical LEFT and RIGHT builds now
use the existing 77-pixel virtual strip, 36 local and 41 right pixels. The left
renders and sends bounded transaction-11 chunks; the right displays complete
CRC-checked frames outside the UART lock. No driver, protocol, keymap, resident,
settings-layout, radio or watchdog change accompanies this correction.

Build with `make build-zmk-wired-studio-updater` for LEFT and
`make build-zmk-wired-peripheral` for RIGHT. Both are explicitly unlocked
development builds. The 32% physical brightness ceiling and native ZMK lighting
controls remain as described in [Mac layers and lighting](ZMK-MAC-LIGHTING.md).
Both halves require the new profile: a lighting-capable right waits for left
frames while joined, and a local-only left never sends those frames.

## Offline verification

`make test test-zmk` passed: 88 repository tests, 7 migration tests, native
lighting/frame/DMA/updater/settings fixtures and 14 ZMK Python tests. Both ARM
builds passed linked-image checks. The frozen configurations enable underglow
and the WB32 RGB driver and disable local-only isolation. Both contain the
lighting/DMA functions and no permanent-watchdog or outgoing-handoff functions.

| Half | Canonical variant | Bytes | CRC32 | SHA256 |
| --- | --- | --- | --- | --- |
| LEFT | `wired-studio-updater` | 70,672 | `1286125312` | `004b120307f3d093b1d964aca3de57dee7c897df46a9e97fd57b824c48cdd424` |
| RIGHT | `wired-peripheral` | 43,752 | `2368050678` | `65cf7a47ee83dd4f635e9aa14517277dc5c2268bb10bcf54c57b9a371787f4cf` |

## Installation evidence

Both exact new firmware identities have returned automatically after one INSTALL
per half, RIGHT first via LEFT USB and then LEFT. RIGHT's transition passed
70.109 seconds of both-half health with zero retries/reconnects before the LEFT
installation. No physical reconnect, right-only USB or recovery pads were needed.
Final both-half health passed 70.234 seconds with zero transport/relay retries
and reconnects; both watchdog feed counts remained zero. Both resident CRCs are
still `1770733249`. Studio reconnected, and all 288 bindings and layer names match
the fresh pre-install snapshot exactly, including the user's newer edits. No
keymap write was made. The normal Studio app was reopened without debugging and
displays all four layers. The user confirmed that right RGB works and Fn+X
controls both halves together. This confirms the visible lighting fix as well as
the independent firmware/Studio checks. See [sanitized validation](validation/2026-10-29-zmk-right-rgb/README.md).

A fresh saved four-layer Studio snapshot was captured after the user finished
their edits. The separate device operator freezes both packages, this snapshot,
its own source and the imported updater/health code. Each INSTALL has an exclusive
marker and was sent once; acknowledgment was followed by independent exact-image
readback and uninterrupted health observation.
