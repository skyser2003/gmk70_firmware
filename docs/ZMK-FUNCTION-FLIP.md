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
semantics. Custom mapped outputs own their held modifier contributions
independently: holding Mac 3 then pressing/releasing A keeps GUI held, and
releasing either Mac 3/4 keeps GUI for the other shortcut. Physical modifiers
and the native implicit mask are merged with these contributions in report
copies; native report buffers are not rewritten.

Only custom mapped dispatches bypass the native singleton implicit-modifier
updates. Unaffected native events retain their pinned semantics. Native HID
usage overlap remains unchanged. Clear and dispatch coordinate so a canceled
in-flight press cannot repopulate native HID state. Keyboard clear drops custom
modifier contributions but preserves a consumer target's balancing release;
consumer clear cancels those targets. Consumer mappings publish the required
keyboard modifier report as well. USB boot, six-key projection and NKRO use the
same contribution mask.
The report adapter is an explicit configuration dependency; USB-only Studio
builds retain clear handling when the radio endpoint is disabled.

## Persistence and integration

The one-byte `gmk_flip/state` value uses the existing banked settings backend.
Runtime toggling and allowlisted preference saves work in radio mode, subject
to LEFT role, power and service/storage readiness. Rapid toggles coalesce
after 250 ms; failed saves retry no faster than once per second. Power removal
before an eligible save can lose the latest toggle.

The existing updater worker schedules the save onto existing system work. It
never waits on itself. A shared runtime-save entry rechecks the applicable authorization and power
gates when the backend dispatches the write; ordinary startup behavior-ID
writes retain their pre-enumeration path. Malformed lengths, short reads,
unknown setting names and values other than 0/1 are rejected.

There is no new thread, stack reduction, permanent watchdog, radio protocol,
resident, RIGHT firmware or settings-bank schema change. The stock white flip
indicator is outside this implementation; Studio exposes the assignable action,
without a live mode-status widget. Logo effects and the reserved custom slot remain unimplemented. ZMK's
application-settings reset is separate from QMK EEPROM reset.
