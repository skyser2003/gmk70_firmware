# Repository custom feature parity

## Three-stack audit — 2026-11-03

This audit compares the maintained QMK/VIA, QMK/Vial and GMK70 ZMK builds,
including their enabled features rather than everything their upstream
frameworks can theoretically provide. The originally installed ZMK baseline was the
105,972-byte image, CRC32 `1734310917`, described in the
[battery-refresh validation](validation/2026-11-03-zmk-battery-refresh/README.md).
Older sizes and validation paragraphs below are historical.

The [connection-switch correction](validation/2026-11-03-zmk-radio-switch/README.md)
is now installed on both halves: 105,900 bytes, CRC32 `2042255645`. Automated
profile transitions and the initial user shortcut retest pass; extended-use
intermittent stability remains pending. This correction addresses the first row
below without enabling the other missing features.

The implementations are not fully synchronized. VIA and Vial share most board
behavior through `qmk-common`; Vial enables additional native configuration
features. ZMK has independent board integration and several explicit policy
differences. No firmware equivalence or measured battery-current claim follows
from passing the existing tests.

### Confirmed defects and missing board behavior

| Behavior | QMK/VIA and QMK/Vial | GMK70 ZMK | Classification |
| --- | --- | --- | --- |
| Connection-mode changes while an older radio command awaits ACK | Failed controls retire through the stock queue; reports can flow after a connection notification | The audited baseline retries old controls, can erase a newer selection, and can fault an already connected destination | Confirmed transition defects corrected in the installed follow-up; initial checks pass, extended use is pending |
| Critical-battery handling with ordinary RGB disabled | Critical qualification is called from native RGB indicators, which are skipped for effect zero | Power qualification runs independently in the service owner | Likely shared QMK/Vial defect: RGB-off can suppress firmware critical shutdown; hardware battery protection was not assessed |
| Charging ADC recovery after USB selection | One-second charger heartbeat has no deliberate radio quiet interval | Explicit 5.5-second quiet interval restarts the stock sampler through UART sleep/wake | Recent ZMK correction has not been ported back. Same stale-estimate risk in QMK/Vial is inferred from captured radio code, not a new physical QMK test |
| Disconnected wireless timeout | Stock retry/fallback requests MCU sleep after about 30 seconds disconnected | Uses the selected ordinary inactivity timeout; "Never" removes that fallback | Missing stock disconnected fallback |
| Radio host suspend/resume notification | `0x60` participates in early sleep and ordinary wake | Parsed/ACKed without a matching power action | Missing radio-driven host power integration |
| Mouse scroll wire order | Sends horizontal then vertical | Sends vertical then horizontal | Confirmed inconsistency; captured-module decoding or a hardware direction test must establish the correct contract before a fix |
| Full joined keyboard hosted through RIGHT USB | Has direct-RIGHT USB master promotion for configuration/recovery; complete joined typing and LEFT demotion are not established by existing tests | LEFT alone gathers peer keys and owns the saved Studio map; RIGHT-local keys may type, but it cannot host the complete joined keyboard | Documented ZMK LEFT-central constraint; a right-central/reverse-routing capability is absent, while cross-stack joined-RIGHT-USB behavior remains unqualified |
| Factory board bindings | Stock Fn radio, battery, flip, lock, reset and four MOR shortcuts are assigned | Several default slots remain transparent/unassigned although handlers exist | Default-map backlog. This does not establish missing bindings in the user's saved Studio map |
| Function-flip / NKRO feedback | Persistent flip indicator and NKRO-toggle feedback | No matching overlays | Missing visual feedback; the underlying actions exist |
| Vial encoder-map migration | Encoders can be configured dynamically per layer | Knob CCW/CW are editable in Studio, but the converter rejects nonempty Vial encoder maps | Migration backlog; applying a 72-key map preserves existing knob positions |

Evidence: shared [radio queue](../firmware/qmk-common/drivers/wireless/module.c),
[wireless timeout](../firmware/qmk-common/keyboards/zuoya/gmk70/wls/wls.c),
[board battery/indicator hooks](../firmware/qmk-common/keyboards/zuoya/gmk70/gmk70.c),
[native RGB-off adapter](../firmware/qmk-common/keyboards/zuoya/gmk70/hardware.c),
ZMK [radio core](../firmware/zmk/radio/core.c),
[radio endpoint](../firmware/zmk/radio/endpoint.c),
[power owner](../firmware/zmk/power/power.c),
[factory keymap](../firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired.keymap),
and [migration converter](../tools/keymap-migration/vial_to_zmk.py).

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
- **Wake:** QMK resumes its ordinary low-power path in place. ZMK reboots after
  WB32 STOP wake, as permitted by its integration specification. First wake tap
  and knob retention still require physical qualification.
- **OS and NKRO changes:** ZMK preserves unrelated held layers and projects
  six-key reports through its native NKRO descriptor. QMK changes its default
  layer/report mode and clears report ownership. The controls exist on all three
  builds, but transitions are not identical.
- **Pairing:** stock QMK sends CLEAN and profile/dongle identity information.
  ZMK preserves existing radio identities/bonds and sends selection/PAIR without
  those destructive or renaming steps.
- **Settings reset:** QMK's physical hold handler is available without a wired
  gate and triggers while held. ZMK requires a three-second hold plus release,
  LEFT wired USB authorization, then resets only ZMK application settings.
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

Two native RIGHT update runs returned an unvalidated INSTALL acknowledgment
despite successful installation. Exact running-image/resident readback and
70-second health windows established both outcomes, with one INSTALL request
each. Acknowledgment visibility remains an updater follow-up; once-only dispatch
and independent observation must be preserved.

See the [stock sleep contract](STOCK-SLEEP-WAKE-MATCH.md),
[ZMK basic parity specification](ZMK-BASIC-PARITY.md),
[settings backend](../firmware/zmk/studio/settings_backend.c),
[Vial initialization](../dependencies/vial-qmk/quantum/via.c),
[stock EEPROM adapter](../firmware/qmk-common/keyboards/zuoya/gmk70/stock_eeprom.c),
and [native update bridge](../firmware/zmk/wired/native_update_bridge.c).

### Verification scope

Targeted power, radio, lighting, native settings, updater/bridge, migration and
Studio RPC suites pass for their existing assertions. Three new transition
probes reproduce the Bluetooth/connection-mode defects despite those passes.
Local idle matrix IRQ optimization is already present in all three builds;
continued split/service maintenance must not be reported as missing idle-scan
support. Physical battery-only sleep/wake/current, overnight endurance and the
mouse wire-order contract remain separate validation items.
Dormant legacy RGB-recording sources and the unactivated white-test flag were
not counted as working canonical QMK/Vial features. The LEFT-central USB-role
constraint is documented in [the universal-image report](ZMK-UNIVERSAL.md);
universal image compatibility does not imply interchangeable USB host roles.

These additions synchronize project-specific behavior, independently of native
framework features and restored stock GMK70 controls. They do not change saved
settings formats, dependency pins, resident installers or custom keycode values.

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
other positions. Native bounds, Vial authorization and physical switch gates
remain authoritative; the expiry callback checks the physical mode again.

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
Its latest snapshot remains pending through wireless mode, USB loss, storage
contention and failed writes. The existing update worker schedules a separate
system-work save; the storage worker rechecks LEFT, physical wired mode,
configured USB and service/storage ownership at execution. Retry is bounded to
once per second. Native startup behavior-ID writes retain their existing
pre-enumeration route. No RGB schema, bool-setting API or thread is added.
Latched storage faults retain the existing fail-closed recovery policy.

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

## Verification boundary

The current [consistency validation](validation/2026-11-01-custom-behavior-consistency/README.md)
adds the radio/native-cache, native map-replacement, RGB callback/backend and
native HID/listener regressions described above. All mandatory suites and 19
builds pass. The canonical Studio image is 85,456 bytes and uses 28,200 of 28,544
RAM bytes, leaving 344 bytes; both retained regions and all resident bytes remain
preserved. The following paragraphs retain the preceding parity/hardware history.

Dedicated native regressions execute both pinned frameworks' actual matrix,
debounce and split-post-scan code with simulated GPIO/IRQs, plus native deferred
timers and native keymap/storage routines for pairing. Pixel tests simulate the
real MMIO driver and exercise battery overlays through the real split lighting
path for both half sizes. The standard build/test commands do not access devices.

The preceding parity batch passed `make test`, `make test-zmk`, `make check-tools`,
formatting and all 19 affected firmware builds. ARM checks confirm the final
matrix/STOP integration in all twelve QMK/Vial images. All fourteen application
packages retain the pinned resident checksums. After integration with `7a8d7da`
(Studio knob keys) and the held-indicator refresh correction, the canonical ZMK
Studio image uses 28,144 of 28,544 RAM bytes, leaving 400 bytes, with both retained
regions excluded. Focused tests
check that LED power cycling preserves the encoder's B6/B7 GPIO configuration
and that battery overlays at virtual knob positions 72/73 release power correctly.

The first hardware check found that LEFT's initial indicator frame after LED
power restoration was not visible: held indicators worked with RGB on and stayed
lit when RGB was turned off during the hold, but a new hold from off remained
dark on LEFT. RIGHT's repeated peer frames still displayed the level. The driver
distinguishes supply startup from data reset, but a 1 ms guard alone did not fix
the physical test. Pressing both indicator keys supplied another frame and made
LEFT display correctly, including after either key was released.

Held indicators now refresh locally every 50 ms, matching native animation
cadence even while upstream RGB is off. This removes the dependency on a single
unacknowledged LED frame after supply restoration. Releasing the final hold
restores darkness and stops the extra local refresh. Native tests cover the
unchanged held frame being sent again, both indicator modes, and no continued
local transmissions after release. The corrected held-refresh image subsequently
passed the user's typing, RGB, knob and individual RGB-off indicator/release
checks. This consistency batch has no new hardware acceptance; LED rail rise
time remains unmeasured.

Software tests cannot establish first-key latency on silicon, STOP recovery on
the keyboard, rail settling, electrical leakage/backpower or battery-current
savings. Those remain separate hardware acceptance checks for these changes.
