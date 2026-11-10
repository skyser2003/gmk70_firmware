# GMK70 NKRO

VIA, Vial and ZMK default to NKRO enabled for new or reset settings. A saved off
choice is respected. Windows and Mac defaults both bind Fn+V to the toggle.
Existing saved keymaps are not rewritten when firmware is built or installed.

## Controls and persistence

VIA/Vial use native `NK_TOGG`, `NK_ON` and `NK_OFF` keycodes. These persist through
the framework's existing keymap configuration. The stock live VIA command stays
live-only. Startup no longer forces a saved off choice back on.

ZMK Studio exposes these actions under **GMK70 controls**, with descriptive
NKRO Toggle, On (more than six keys) and Off (six-key compatibility) choices.
Custom DTS bindings use `&gmk_controls GMK70_NKRO_TOGGLE`, `GMK70_NKRO_ON`
or `GMK70_NKRO_OFF` with `dt-bindings/zmk/gmk70_controls.h` included. Existing
direct `gmk_nkro` bindings migrate in RAM. It retains the one-byte
`gmk_nkro/state` key in the existing settings store; bank formats, saved layers
and unrelated settings are unchanged. NKRO and function/media flip share the
same deferred boolean-setting helper: changes are coalesced for 250 ms, failed
writes are retried at most once per second, and storage uses the existing
left/wired/USB/service gates. Radio-mode changes stay live until wired USB makes
persistence available. Non-Studio ZMK profiles have runtime toggles and default
back to enabled after reboot because they do not mount the Studio settings store.

## Reports and limits

ZMK uses the native extended NKRO bitmap. Turning NKRO off projects that state
to at most six ordinary keys while retaining modifiers and the actual held
state; turning it on restores all still-held keys. Its USB descriptor stays
fixed, so a toggle does not require USB re-enumeration. This six-key mode is not
a descriptor change to a legacy array report. Hosts requesting USB boot
protocol receive six-key boot reports, including genuine overflow indication.

The stock Bluetooth/dongle module has a 112-bit overflow bitmap and six ordinary
key slots. The shared encoder reserves ordinary slots for higher usages where
needed, so all lower usages plus up to six higher usages can coexist. This
hardware format cannot represent a seventh simultaneous higher usage. USB NKRO
is independent of that radio limit.

Mode changes clear stale outgoing reports. The radio queue keeps each keyboard
report and its overflow bitmap together, and reserves keyboard, overflow and
consumer releases ahead of host controls. ZMK then republishes current keyboard
and consumer state. QMK releases volatile held flip outputs before changing
report formats, including modifier-only media shortcuts and USB boot fallback.

## Validation boundary

Dedicated native regressions exercise default/persisted settings, both function
layers, more than six keys, high usages, holes, modifiers, queue interruption,
mode transitions, storage gates and boot-report recovery. Build/test commands
never write to a keyboard. Hardware typing and RF delivery remain separate
acceptance checks for these images.
