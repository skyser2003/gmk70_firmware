# GMK70 basic feature parity

Native ZMK implementations and the existing GMK70 hardware/protocol provide
these board features. See [battery behavior](ZMK-BATTERY-LAYOUT.md) for radio
sampler recovery and stale-reading limits.

## Required behavior

- Native inactivity tracking must shut down ordinary RGB and enter a real
  battery-saving hardware state. Both halves must wake for a key, knob, host
  switch, cable insertion or peer activity without right-only recovery.
- Reuse native ZMK sleep/activity and Zephyr power interfaces. WB32 register
  sequences and wake wiring come from the maintained QMK/vendor integration.
  Ordinary sleep requires released keys, no pending radio/control/storage
  work, and no active updater. Qualified critical battery power bypasses held
  keys and may flush only the existing allowlisted runtime preferences first.
  Native software power-off may reboot after wake;
  preserve preferences first, and keep the resident and recovery reservations.
- Use the radio firmware's existing Bluetooth/2.4 GHz sleep-enable commands.
  Battery queries and peer maintenance must not keep sleeping hardware awake.
- Provide automatic low-battery warning at 15% and the existing QMK critical
  policy after a fresh 0% reading persists for 60 seconds without cable power.
  Stale/unknown readings must never cause a critical shutdown.
- Forward the existing hardware's charger/full state to the stock radio firmware
  so its shared keyboard/Bluetooth battery estimate can update during charging.
- Enable native HID indicator state and reuse it for Caps Lock. Add the board
  pixel adapter and radio notification bridge. Show stock host/pairing/OS/lock
  feedback without overwriting saved RGB state or held battery indicators.
- Add persistent Windows-key lock and Windows/Mac selection through native HID
  report projection, native layer APIs and the existing settings subsystem.
  Preserve held press/release ownership and saved layer bindings.
- Persist the last selected Bluetooth slot. The external module retains bonds;
  do not erase or replace its firmware. Native settings provide persistence.
- Support native runtime preference/RGB saves on battery while idle and safe.
  This user-requested extension applies only to board runtime settings. Studio
  keymap writes, firmware updates and raw storage operations keep their existing
  wired/USB/role/storage authorization gates.
- Enable native mouse emulation and carry mouse reports through the existing
  stock radio protocol. Reuse native keyboard/consumer power keycodes; add only
  genuinely missing radio report transport, not another keyboard HID engine.
- Implement WB32 USB remote wake through the Zephyr controller API used by
  native ZMK. Honor host authorization and USB resume timing.
- Keep an active USB host connected. The saved USB-sleep preference permits a
  reversible shallow WFI only after host suspend; USB clocks/state stay alive
  for native remote wake. Battery sleep uses the existing WB32 STOP sequence.
- Reuse native reset/bootloader behaviors and existing factory bootloader magic.
  Add deliberate startup recovery and native settings-reset integration for the
  application-owned settings bank. Never erase unrelated QMK/Vial data, the
  resident, firmware staging or retained reservations.

## Constraints and qualification

One identical universal application, runtime PB9 roles, no permanent watchdog,
unchanged pinned dependencies/resident, saved layouts and 74-position geometry.
Native functionality must fit the 28,544-byte admitted RAM region. Prefer synchronous native input and shared workers/storage; do not lower
stack reservations without measured or defensible compiled call-chain evidence.
Record all RAM, stack, application-bound and resident checks before device writes.

Tests must execute pinned native code where it supplies the behavior, with real
board logic and simulated registers where hardware is required. Verify wake races,
held keys, stale battery data, storage contention/power loss, split disconnection,
radio frame ownership and unauthorized USB wake. Device operations remain separate
from build/test commands. Hardware or endurance claims require actual evidence.

The scope is the audited basic firmware features. Native effect catalogs and
Studio's advanced-editor limitations remain upstream capabilities; this does
not create a separate GUI or replace ZMK's framework.

## Integration and operation

Studio exposes native mouse buttons, movement and scrolling, native reset and
bootloader actions, and a native soft-off action requiring a three-second hold.
The `GMK70 controls` behavior combines the board controls, function/media flip,
NKRO and guarded recovery actions in one native Studio dropdown:

| Option | Effect |
| --- | --- |
| GUI lock: toggle / block / allow Win/Command keys | Toggle, enable or disable the GUI-key lock. |
| Windows mode: normal Alt/GUI | Select the saved Windows mode and its default modifier layout. |
| Mac mode: swap Alt/GUI | Select Mac mode and its default modifier layout. |
| Windows/Mac mode: toggle | Switch between those two modes. |
| USB idle sleep: toggle permission | Toggle the saved permission for idle sleep in USB mode. |
| USB idle sleep: allow saved timeout | Permit sleep after the saved timeout when the USB host is suspended. An active USB host stays connected. |
| USB idle sleep: keep USB awake | Disable USB idle sleep; wireless sleep keeps its own policy. |
| Flip: numbers to F-keys/Mac media | Toggle the [number-row/function/media translations](ZMK-FUNCTION-FLIP.md), according to the active OS mode. |
| NKRO: toggle multi-key rollover | Switch between NKRO and six-key output. |
| NKRO: on (more than 6 keys) | Enable the NKRO report path. |
| NKRO: off (6-key compatibility) | Use the six-key compatibility report path. |
| Reboot this half: hold 3s/release | Hold for at least three seconds, then release to reboot the half containing that key. |
| Bootloader this half: hold 3s/release | Use the same gesture to enter that half's factory bootloader. |
| Clear app settings: hold 3s, either half | Hold for three seconds; reset completes while held in USB, Bluetooth or dongle mode. RIGHT key events are routed to LEFT, which atomically clears its application bank and restores factory settings live, without rebooting. |

Battery/charging and connection selection remain separate behaviors. The native
`Sleep` action is separate too. `GMK70 sleep timeout` selects never,
1/3/5/10/20/30 minutes, or the five-minute default. These are native behavior
metadata and settings; no separate configuration application is required.

Existing direct bindings to `gmk_flip`, `gmk_nkro` and `gmk_recovery` are translated
to the corresponding controls action in RAM after native settings load, including
Studio Discard. Their historical behavior IDs remain reserved. The old three
groups are no longer advertised to Studio; existing controls values 0–8 keep their
meaning, and the new actions use explicit values 9–15. Native Studio Save writes
the converted bindings if requested; migration does not reset or automatically
rewrite the saved keymap. Layer names/order, other bindings and preference
records are preserved. New custom DTS keymaps and macros should use
`&gmk_controls GMK70_*` from `dt-bindings/zmk/gmk70_controls.h`.

The factory map follows the maintained QMK four-layer layout, including its
board shortcuts and Ctrl+Z/X/C/V column. The unsupported independent logo-effect
key remains a no-op. Saved Studio bindings keep priority; other positions use
the new defaults. Reset settings to load the complete factory map.

RGB stays on during ordinary idle, including active USB use. Both halves blank
their LEDs when the board sleep coordinator quiesces them, using the saved
sleep timeout (five minutes by default). The saved RGB on/off choice is retained;
waking restores lighting only when enabled. Native activity still becomes idle
after 30 seconds, without a separate RGB cutoff. Separately, an accepted radio
DISCONNECTED notice starts a 30-second coordinated-sleep deadline, including
with saved Never. Repeated notices and retry traffic do not extend it; reconnect,
explicit host selection and physical wake reset it. Active USB and pending
storage transactions retain their existing sleep gates.
Sleep waits for pending native settings snapshots and their existing
60-second debounce.
Critical entry starts after 60 seconds of qualified 0% readings and also waits
for the pending native settings snapshot to finish.
Held battery indicators take precedence over ordinary status/warning overlays;
the power coordinator physically blanks each half before either advertises ready.

The original runtime slot, rollover and flip settings remain native records.
One additional four-byte versioned preference record stores lock/OS, last
Bluetooth slot and sleep preferences. Wireless saves remain restricted to
`rgb/underglow/state`, `gmk_nkro/state`, `gmk_flip/state` and `gmk_controls/state`.
Studio keymaps, native behavior-ID writes and updater writes retain their wired
USB boundaries. The deliberate physical settings-reset key is a separate narrow
exception; it does not enable wireless Studio writes or generic erase. Studio waits for verified persistent behavior IDs and receives
the native lock-state notification when that check completes.

For startup recovery on LEFT, hold the knob button and Escape from startup for
three seconds, then release every key to request the existing factory bootloader.
Adding Fn selects application-settings reset instead. Recovery uses the existing
factory magic and application-owned settings banks; it leaves the resident,
radio bonds, staging and unrelated QMK/Vial storage intact.

The layout converter now imports GUI lock, USB-sleep preference, persistent OS
selection and deliberate application-settings reset. Exact QMK system-page
power/sleep/wake codes are rejected rather than producing nonworking native
bindings; native keyboard/consumer power and sleep actions remain available.

Memory savings use native synchronous input, a 128-byte USB control buffer,
Zephyr's `-Oz`, tested integer/string nano formatting, and 32-bit relative kernel
timeouts. The native uptime counter remains 64-bit. The kernel runs at 1 kHz,
with a relative wait range of about 24.86 days; compiled application deadlines
here are at most 30 minutes. Native timeout alignment means a requested 1 ms
worker sleep may take approximately 2 ms. UART, LED and USB signal timing uses
cycle counters/busy waits. No stack reservation is reduced.

The settings store retains its bank/page format and native values while replacing
its 656-byte RAM index with bounded flash traversal. Its logical limit is now
344 entries; the complete 328-entry native workload plus 16 historical behavior
IDs fits the existing bank. Longer names/values may exhaust byte capacity earlier.
Older images limited to 328 entries cannot load a bank that exceeds that count.
Historical behavior IDs remain reserved through the native settings handler,
so a newly added behavior cannot take over an unavailable behavior's old ID.
An unavailable historical binding retains native transparent fallback semantics.
