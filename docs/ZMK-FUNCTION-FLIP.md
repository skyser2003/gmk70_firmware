# GMK70 function/media flip

**GMK70 Function / Media Flip** is a parameterless, assignable ZMK Studio
behavior implementing stock `custom(26)` / `KC_FILP` (`0x7E1A`). Pressing it
toggles the mode; releasing it has no action. Fresh settings start with flip off.
The factory keymap is unchanged. The saved-layout migration restores only the
original flip position, preserving all other 287 bindings
and all four layer names/order.

With flip off, keys pass through unchanged. With it on, the emitted keycodes
follow the maintained QMK board's tables:

| Source | Windows mode | Mac mode |
| --- | --- | --- |
| `1`, `2` | F1, F2 | Display brightness down, up |
| `3`, `4` | F3, F4 | GUI+Tab, GUI+E |
| `5`, `6` | F5, F6 | Keyboard RGB brightness down, up |
| `7`, `8`, `9` | F7, F8, F9 | Previous, play/pause, next |
| `0`, `-`, `=` | F10, F11, F12 | Mute, volume down, volume up |
| F1–F4 | Unchanged | Media select, volume down, volume up, mute |
| F5–F8 | Unchanged | Stop, previous, play/pause, next |
| F9–F12 | Unchanged | Mail, browser home, calculator, search |

These are HID outputs; the host decides how application shortcuts behave. Mac
mode follows the existing compiled Mac layer IDs 2/3, independently of layer
names/order. Mac 5/6 change keyboard RGB brightness only on press and emit no
host key. The mode applies after key bindings are resolved, for both USB and
radio output; it does not rewrite the saved number-row bindings.

Each affected source retains its press-time mode and implicit modifiers until
release, so changing flip or OS layers while holding a key releases its original
target. A repeated source press releases its previous target first. Unrelated
keys, including Enter, Escape, Backspace, Tab and Space, pass through unchanged.
Arbitrary live binding replacement while a key is held retains upstream ZMK
semantics. Native handling of overlapping modifiers/usages is unchanged.

## Persistence and integration

The one-byte `gmk_flip/state` value uses the existing banked settings backend.
Runtime toggling works in radio mode; saving waits for LEFT, the physical wired
position, configured USB and service/storage readiness. Rapid toggles coalesce
after 250 ms; failed saves retry no faster than once per second. Power removal
before an eligible save can lose the latest toggle.

The existing updater worker schedules the save onto existing system work. It
never waits on itself. A shared runtime-save entry rechecks the USB requirement
when the backend actually dispatches the write; ordinary startup behavior-ID
writes retain their pre-enumeration path. Malformed lengths, short reads,
unknown setting names and values other than 0/1 are rejected.

There is no new thread, stack reduction, permanent watchdog, radio protocol,
resident, RIGHT firmware or settings-bank schema change. The stock white flip
indicator is outside this implementation; Studio exposes the assignable action,
without a live mode-status widget. Logo effects, the reserved custom slot and
QMK settings reset remain unimplemented.

## Build and verification

Source `e4e9c7bef4af8c1fa28bbe9adf582a88376935a3` builds the normal
`wired-studio-updater` application at 79,612 bytes, CRC32 `799253422`, SHA256
`e20a01fd51ff53dbf1b7280e050d7b2c571c933308a618a7e14e7865ed5b8fa1`.
Static RAM is 27,576 of 27,648 bytes with existing thread stacks retained.
Compiler stack-frame checks cover the wrapper, mapping, save and worker paths;
actual hardware stack watermarks remain unmeasured. Existing upstream KSCAN
deprecation and RWX linker warnings remain.

`make test test-zmk check-tools` passes the dedicated native sanitizer tests,
repository/migration/protocol/image suites, Ruff and strict mypy. Dedicated tests
exercise all stock mappings, unrelated HID gaps, held mode/OS changes, duplicate
presses, brightness, invalid settings, deferred/coalesced saves, failed writes
and USB loss between queueing and backend dispatch. Studio metadata tests cover
the zero-parameter behavior and sparse import preserving the other 287 bindings.
Linked-image checks verify actual calls from native keycode emission to the
wrapper and from the existing worker to the persistence poll.

The canonical central, peripheral and both affected qualification variants
compile. Build/test/conversion commands do not operate the keyboard. Device
installation and validation are recorded separately in the
[sanitized hardware results](validation/2026-10-30-zmk-function-flip/README.md).
