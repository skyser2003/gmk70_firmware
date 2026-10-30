# Persistent GMK70 function/media flip

Implement only stock custom slot 26 (`0x7E1A`), using the maintained QMK
handler as the mapping contract. Existing Fn/Windows/Mac layers remain intact.
The user previously requested autonomous execution; use the existing development
branch and inline implementation without additional design questions.

Expose a parameterless **GMK70 Function / Media Flip** Studio behavior. Default
off; a press toggles the board-wide mode, release does nothing. Off passes all
keys unchanged. On maps number-row HID usages 1..0/minus/equal to F1..F12 in
Windows mode. Mac mode follows the stock number-row and F1..F12 media mappings,
including GUI+Tab/E and keyboard-light brightness on 5/6. Mac is indicated by
active compiled layer IDs 2/3, independently of names/order.

Use a local wrapper around `raise_zmk_keycode_state_changed`, ahead of native
HID processing and the existing USB/radio dispatch. A small pure core records
the mapping context and implicit modifiers for each of the 24 affected source
usages. Releases use their press context across mode/OS changes; repeated source
presses release the earlier target first. Modifiers, timestamp and unrelated
usages pass through. No imported sources change.

The existing updater worker schedules a short system-work settings save; it
never calls the blocking settings API itself. A one-byte versionless boolean
under `gmk_flip/state` uses the current banked backend. Runtime changes work in
radio mode; persistence waits for LEFT wired USB readiness, coalesces rapid
toggles and retries failed writes at bounded intervals. Invalid settings fail
without changing state. No watchdog, thread or stack-size change.
The backend's shared runtime-save entry marks the queued job as USB-required
and rechecks that flag at actual worker dispatch. Ordinary startup behavior-ID
writes retain their existing pre-enumeration path.

Alternatives: toggling the whole Fn layer changes unrelated keys and differs
from stock; replacing every saved number binding breaks custom layouts and adds
metadata/state. The event wrapper preserves saved bindings and routes both USB
and radio identically. In-flight holds across arbitrary live keymap replacement
retain upstream semantics; this feature specifically guarantees flip/OS changes.

Translate `0x7E1A` to `gmk_flip`, restore only that single source-export binding
against a fresh clean Studio backup and preserve the other 287 bindings. Do not
add lighting effects, reset behavior, reserved-slot behavior or RIGHT changes.
Build/test commands stay separate from device operations. Before any installed
update, freeze inputs, qualify both identities/residents/health, request one LEFT
INSTALL and independently observe the exact image and saved settings. Leave flip
off initially and keep the current map recoverable privately.
