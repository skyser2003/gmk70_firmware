# Repository custom feature parity

VIA and Vial share board behavior through `qmk-common`. Vial adds native
configuration features; ZMK uses separate board integration and explicit
policy differences. Compare enabled builds rather than every feature offered
by their upstream frameworks.

### Enabled framework and configuration features

| Area | QMK/VIA | QMK/Vial | Current ZMK build |
| --- | --- | --- | --- |
| Ordinary keys, four Windows/Mac layers, Fn/media flip, GUI lock, mouse keys and knob direction bindings | Implemented | Implemented | Implemented; saved map and defaults must be distinguished |
| Runtime keymap and ordinary macros | VIA dynamic keymap/macros | Vial dynamic keymap/macros | Studio edits existing bindings/layers; this image has no compiled macro instances |
| Tap/hold and one-shot/toggle actions | Native QMK mod-tap, layer-tap, one-shot and layer-toggle keycodes | Same native actions plus enabled Vial editors/features | Hold-tap/sticky/tap-dance behaviors and layer-toggle instances are not currently compiled into this image |
| Dynamic tap dance, combos, overrides, alternate-repeat rules, QMK Settings/Auto Shift | Not enabled as Vial-style dynamic editors | Enabled; 32 entries each for tap dance, combos, overrides and alternate-repeat | No equivalent dynamic Studio editors; relevant behavior definitions are also missing from this build |
| Caps Word, layer lock and repeat features | Depends on compiled QMK features; not the same enabled set as Vial | Explicitly enabled in this port | No matching Caps Word/repeat/toggle instances in this image |
| Generic Desktop system power/sleep/wake HID reports | Enabled EXTRAKEY path | Enabled EXTRAKEY path | Missing native report path; migration rejects these rather than emitting inert bindings. Keyboard/consumer alternatives are different usages |
| Lighting effects | Native matrix catalog plus independent six-pixel decorative/logo engine | Same board engine plus Vial RGB interface | Four native underglow effects across one 77-pixel strip; no independent logo engine |
| GUI lighting controls | Stock VIA lighting controls | Native Vial RGB controls | RGB actions can be assigned to keys; this Studio integration has no equivalent lighting-control panel |
| Bluetooth configuration | Stock vendor raw channel; compatible client/radio services required | Same carrier; the Web Vial RAM-service experiment exists | USB CDC Studio only; vendor raw frames are not forwarded into Studio RPC |

ZMK upstream supports many behaviors missing from this particular image. Adding
their instances and metadata requires a firmware build; their absence must not
be described as a general limitation of ZMK. Studio binds existing behavior IDs
and cannot create the missing definitions. Conversely, the converter's ability
to emit a symbolic behavior does not mean that behavior exists in the installed
image: runtime ID-resolution preflight rejects missing IDs before writing.
The offline converter also accepts keyboard usages `0x98`–`0xA4`, whereas the
current native NKRO report ends at LANG8 (`0x97`). Actual Studio metadata and
firmware checks reject those values before assignment. This is overly optimistic
offline readiness and a device-bound test-fixture gap, not a silent inert import.

Build evidence includes [Vial rules](../firmware/qmk-vial/keymaps/vial/rules.mk),
[Vial entry capacities](../firmware/qmk-vial/keymaps/vial/config.h),
[VIA rules](../firmware/qmk-via/keymaps/release/rules.mk), and the generated
configuration accompanying the ZMK baseline. Native Bluetooth Studio support is
blocked by the separate stock radio interface, not by a lack of upstream Studio
BLE support. The existing radio RAM experiment implements Vial rather than Studio.

### Intentional or unresolved policy differences

- **Active wired USB:** both prevent ordinary inactivity sleep. QMK's stock
  forced USB-sleep controls request a special software sleep class immediately;
  ZMK's saved option permits shallow sleep only after actual host USB suspension.
  ZMK also blocks battery STOP whenever USB power is present, including when
  typing is routed over Bluetooth.
- **Critical qualification:** QMK deliberately retains the stock timer across
  low nonzero readings (1–15%). ZMK requires continuous fresh 0% readings for
  60 seconds. This is a recovered-stock policy difference, not an accidental
  QMK timer-reset defect.
- **Critical peer/wake policy:** QMK sends ordinary sleep to its peer and
  accepts cable/USB for local critical recovery. ZMK propagates critical mode to
  RIGHT and arms additional switch/knob/peer wake sources. Harmonization needs a
  deliberate board power-policy choice.
- **Wake:** QMK resumes its ordinary low-power path in place. Current ZMK uses
  returning STOP with retained state when its RTC/hardware contract is available;
  critical power and unavailable retention keep the reset fallback. Physical
  wake reselects the current radio profile before scanning resumes. First wake
  tap and knob retention still require physical qualification.
- **OS and NKRO changes:** ZMK preserves unrelated held layers and projects
  six-key reports through its native NKRO descriptor. QMK changes its default
  layer/report mode and clears report ownership. The controls exist on all three
  builds, but transitions are not identical.
- **Pairing:** stock QMK sends CLEAN and profile/dongle identity information.
  ZMK preserves existing radio identities/bonds and sends selection/PAIR without
  those destructive or renaming steps.
- **Physical settings reset:** both act after a three-second hold, from either
  half in any output mode, without rebooting. ZMK clears its application bank
  and restores factory bindings, names, order, RGB and board preferences live.
  Its startup recovery remains a separate LEFT wired USB operation that reboots.
- **Settings persistence:** VIA preserves its stock magic across rebuilds.
  Normal Vial retains upstream random-build-ID invalidation, so a different
  build can reset dynamic configuration despite an unchanged schema. Only its
  separate diagnostic exception preserves installed magic. ZMK persists native
  settings without per-build invalidation; general Studio saves require wired
  mode, with a narrow safe wireless allowlist for board/RGB/NKRO/flip preferences.
- **Updater carrier:** resident image installation and ABI1 packets remain
  shared, but canonical ZMK native split frames cannot directly update a legacy
  QMK/Vial peer. Mixed-stack migration/rollback needs the separately named LEFT
  bridge. Release/debug, native Vial unlock, explicit insecure variants and
  diagnostic-query exclusions remain deliberate build boundaries.

### Fixes and investigations still warranted

| Priority | Finding | Evidence and next action |
| --- | --- | --- |
| High | QMK/VIA and QMK/Vial critical-battery qualification depends on RGB rendering. | `bat_indicators()` has its sole caller in the advanced RGB callback. Both pinned frameworks skip that callback for effect zero; the RGB-off board wrapper renders held indicators only. Move power qualification into ordinary board housekeeping, preserving the recovered stock timer policy. This establishes a firmware path defect, not failure of hardware battery protection. |
| Medium | The QMK/Vial held settings-reset action also depends on RGB rendering. | `EE_CLR` starts/cancels `ee_clr_timer`; its only expiry check and `hs_reset_settings()` dispatch are in the same advanced RGB callback. Holding reset with ordinary RGB off cannot complete that path. Service the timer independently of rendering. This additional defect was identified in this audit. |
| Medium | ZMK omits radio-driven host suspend/resume. | QMK handles radio `0x60` in `md_receive_host_cb()`; ZMK ACKs it without changing sleep policy. Its separate accepted-DISCONNECTED timeout now requests coordinated sleep after 30 seconds, including with saved Never. USB and storage safety gates still apply. |
| Medium | Wireless mouse scroll axes differ between stacks. | QMK sends horizontal/vertical in payload bytes 3/4; ZMK sends vertical/horizontal. Both feed the same stock radio command. The existing ZMK test asserts its own ordering and cannot establish the hardware contract. Decode the receiver or test separate horizontal/vertical scrolling before choosing which implementation to change. |
| Medium | Battery operating conditions remain different. | QMK regular queries are wireless-only; ZMK queries periodically only in wireless mode and refreshes on a battery-level key press. ZMK retains USB while wireless and a 5.5-second sampler-recovery interval. Both use the unchanged radio estimator. The cable-insertion jump occurred while Bluetooth remained selected, so wired polling alone is not an established cause. |
| Medium | RIGHT update acknowledgment is not fully qualified. | An INSTALL acknowledgment can be lost before restart. Retain once-only INSTALL dispatch and independent image/resident readback when investigating response delivery. |

Source evidence: [QMK board callbacks](../firmware/qmk-common/keyboards/zuoya/gmk70/gmk70.c),
[RGB-off adapter](../firmware/qmk-common/keyboards/zuoya/gmk70/hardware.c),
[QMK radio power hook](../firmware/qmk-common/drivers/wireless/lowpower.c),
[disconnected timeout](../firmware/qmk-common/keyboards/zuoya/gmk70/wls/wls.c),
[QMK mouse projection](../firmware/qmk-common/drivers/wireless/wireless.c),
[ZMK mouse projection](../firmware/zmk/radio/endpoint.c),
[ZMK notification handling](../firmware/zmk/radio/core.c), and
[ZMK power policy](../firmware/zmk/power/core.c).
The callback gate is present in both pinned `quantum/rgb_matrix/rgb_matrix.c`
implementations. Existing held-battery-overlay and power-policy tests do not
exercise critical/reset dispatch through that actual RGB-off callback gate.

All three builds now refresh the same radio profile on physical wake and bound
retries when CONNECTED status is missing. ZMK retains wake snapshots and held
Alt/Tab within a finite reconnect window; VIA/Vial preserve their same-host
native input state. Host changes retain their clear boundaries. See
[ZMK reconnect](ZMK-WIRELESS.md#sleep-and-reconnect) and
[VIA/Vial reconnect](SLEEP-WAKE-REGRESSION.md#radio-reconnect-after-wake) for
protocol evidence, bounds and the remaining hardware-validation requirement.

### Factory map and remaining migration work

ZMK now follows all 72 physical positions of each maintained QMK factory layer,
including Ctrl+Z/X/C/V, Bluetooth/dongle selection, OS selection, battery/charging,
function flip, GUI lock, NKRO and settings reset. Its two appended knob positions
remain Studio-editable. The stock logo-effect key (`RL_MOD`) is an explicit no-op
because ZMK has no independent logo engine; reserved stock no-ops stay inert.
Saved Studio bindings keep priority; positions without saved overrides use
the new defaults. A deliberate settings reset removes those overrides.


- Hold-tap, sticky keys/layers, tap dance, macros, toggle-layer, Caps Word and
  repeat behavior instances are absent from this canonical ZMK image. Their
  implementations exist in the pinned upstream tree. They can be integrated
  with explicit instances and resource checks; native availability does not
  imply that Studio can create arbitrary behavior definitions dynamically.
  Vial's dynamic combos/tap-dance/overrides/alternate-repeat editors remain a
  separate capability from enabling ZMK behavior instances.
- The Vial-to-ZMK converter still rejects populated encoder maps, despite
  Studio supporting both knob directions. It also accepts toggle-layer and
  keyboard-usage mappings that can fail live device preflight because this
  image lacks the behavior or usage range. Preflight prevents a silent invalid
  import, but the offline report should reflect target capabilities.
- ZMK has four shared underglow effects and lacks the independent decorative
  logo engine, Vial-style RGB panel, and flip/NKRO feedback overlays. Generic
  Desktop system power/sleep/wake reports also remain unsupported; keyboard or
  consumer usages are not equivalent replacements.
- Complete joined-keyboard operation through RIGHT USB and Bluetooth Studio
  remain absent from this ZMK integration. The former needs reverse central
  routing/settings ownership; the latter needs a usable transport through the
  separate stock radio. Neither follows merely from using one universal image.

### Wired battery and system-key clarification

The automatic three-second UART inquiry is wireless-only. Pressing the
battery-level action queues one coalesced refresh through the existing UART
worker, including its bounded retry. Charging
status and key release do not request percentages. Wired entry cancels queued
periodic work; a partial frame may finish and retain ownership of its ACK, but
does not start periodic retries. Wireless cadence is unchanged.

The native state-of-charge sensor exposes the GMK70 cache and does not cause
wired UART queries. Canonical USB Studio disables the extra native battery
broadcast timer: native BLE BAS and display widgets are absent, and RIGHT does
not initialize this LEFT-owned sensor. The stock radio supplies host battery
information; GMK70 indicators and sleep policy read the board cache directly. The ten-second freshness limit remains: cold/expired data appears
unknown until a valid reply arrives, and holding the display beyond that limit
requires another press for a fresh query. Charger GPIO reporting, notifications
and the 5.5-second USB sampler-recovery interval remain. This policy does not correct the radio estimator's transient percentages.

Charge-present and charge-complete indications are different: A7/A15 provide
them without a percentage inquiry. All three builds accept a held percentage
display in wired mode and request a coalesced refresh through their existing
UART owner. QMK/VIA and Vial no longer reject that press. None changes the
radio estimator or enables continuous wired UART polling.

The missing system key reports target the host computer. QMK `KC_PWR`,
`KC_SLEP` and `KC_WAKE` map to Generic Desktop HID page `0x01`, usages
`0x81`, `0x82`, `0x83`. Pinned ZMK defines these symbolic constants but its
native key press and endpoint paths implement keyboard/consumer pages only;
the GMK70 radio endpoint likewise has no system-report route. `K_POWER`,
`C_POWER` and `C_SLEEP` use different pages/usages, with different host support,
so translating between them would not preserve the original command. The
keyboard's own sleep/soft-off and USB remote wake are separate mechanisms.
See the [native endpoint](../dependencies/zmk/app/src/endpoints.c),
[native HID dispatcher](../dependencies/zmk/app/src/hid.c),
[QMK radio system-report path](../firmware/qmk-common/drivers/wireless/wireless.c),
and [official ZMK keycode compatibility](https://zmk.dev/docs/keymaps/list-of-keycodes).

## VIA and Vial: awake idle matrix scanning

The shared board adapter wraps native matrix scanning. Once local raw and
debounced keys are released and quiet, it drives the populated columns low with
open drains and arms falling-edge row interrupts. Idle calls retain native
`matrix_post_scan()`: split transactions, peer-only presses, disconnect releases
and board hooks continue. Encoder maintenance and other framework tasks continue
in their native paths. A pending row edge resumes native scanning and debounce.

Interrupts are armed before driving the columns, followed by a row sample so a
key pressed during arming cannot depend on another edge. Existing EXTI ownership
causes polling fallback. Matrix initialization restores normal scanning. An
explicit low-power hook yields awake IRQ ownership before STOP preparation;
after resume, dormant stock wake callbacks are retired before row IRQs are
re-enabled. This includes split RX, which shares C13's interrupt group.

This removes local idle GPIO sweeps; it does not replace the existing board
sleep policy or claim measured battery-current savings. Upstream framework
sources remain unmodified.

## VIA and Vial: pairing hold ownership

Pending three-second pairing holds now belong to a physical matrix position.
Releasing another key with the same selector binding cannot cancel the newer
hold. The early physical-release hook cancels a hold even when the key has been
remapped or another feature consumes its release. Synthetic selectors can select
a host but cannot establish a physical pairing hold.

Live matrix keymap writes, buffer writes and resets cancel a pending hold through
the native storage boundaries. Cancellation is conservative, including edits to
other positions. Native bounds and Vial authorization remain authoritative.
Connection shortcuts can select USB, Bluetooth or the dongle from any valid
slider position, matching ZMK. The slider selects at startup and after a
debounced move instead of continually enforcing its position. Any observed
slider movement cancels pending pairing; the expiry callback also checks that
the physical position has not changed. Firmware updates require both logical
USB output and the physical USB position on the left half.

## VIA and Vial: radio report recovery

Accepted connection changes latch recovery for the normal keyboard task; the
radio parser does not invoke native report APIs. Actual loss discards native
keyboard/modifier state and custom flip ownership, drains later releases and
blocks new disconnected key presses. Empty transport reports also clear host
state when native report caches remember a release that pairing suppressed.
Repeated status and same-channel wake preserve live held input; actual host
changes clear ownership before selecting a new destination.

Both-low physical selector input is explicitly invalid. It cannot inherit an
old logical Bluetooth channel and accidentally pass a physical-mode gate.
The no-selector policy remains separate.

## ZMK: native map replacement and RGB saves

Individual edits, Studio Discard/Reset and layer move/add/remove/restore cancel
physical holds and queued/pending pairing before native work begins. Slow,
failed or no-op native operations cannot race the old pairing deadline. Native
arguments/results and settings behavior remain unchanged; a command already
transmitted to the radio cannot be retracted.

The native RGB callback retains its exact saved bytes and 60-second debounce.
Its latest snapshot remains pending through storage contention and failed
writes. The storage worker rechecks role, power and service/storage ownership
at execution. RGB uses the narrow wireless runtime-save allowlist; keymaps and
behavior-ID writes retain their wired gates. Retry is bounded to once per
second. Latched storage faults retain their fail-closed recovery policy.

## ZMK: held shortcut modifiers

Mapped function-flip shortcuts keep independent modifier contributions through
unrelated native events and overlapping shortcut releases. Report copies merge
them with native explicit/implicit state for NKRO, six-key projection and USB
boot. Only custom mapped events bypass native singleton implicit updates;
unaffected native events keep their pinned semantics. Native clears cancel
custom ownership, including with the radio endpoint disabled. See
[function/media flip](ZMK-FUNCTION-FLIP.md) for the native HID/listener tests.

## ZMK: physical LED power control

The existing serialized pixel owner keeps both local LED rails off initially.
A nonzero frame enables the rails with the data pin low, allows a separate
1 ms supply-startup guard, then sends the frame. Already-powered transfers
retain the 280 us data-reset interval. A successful black frame completes DMA/SPI
and the low latch interval before switching the rails off. The unpowered data pin stays low.
An already-dark, unpowered frame needs no transmission. Failed transmissions do
not pretend the rails have been switched off; a subsequent successful retry can
complete shutdown.

The same behavior applies to both physical roles. Held battery/charging overlays
can temporarily light an otherwise disabled RGB frame and restore physical
darkness when released. Native saved RGB enable/effect/brightness settings and
split replay remain unchanged.

Software tests do not establish RF delivery, STOP current, battery endurance,
rail settling or runtime stack watermarks. Use
[hardware acceptance procedures](HARDWARE-VALIDATION.md) for those checks.
