# USB-entry battery candidate

**Historical trial, withdrawn at the user's request.** The last candidate did
not resolve the percentage jump. Source and all eight packages have been
restored to the exact pre-trial application; see the
[rollback report](../2026-11-05-zmk-battery-rollback/README.md) for current device
verification. The trial observations and identities below describe that earlier
experiment, not the current canonical package. The transition getter and CLI
`--trace` documented below have been removed.

This candidate prevents a short charger-full input from being forwarded as
full, and records the evidence needed to distinguish that path from a high
converted voltage during USB sampler recovery. It is now **installed on LEFT**.
Both halves passed the separated 70-second checks before and after installation,
and the exact saved Studio keymap is preserved. The physical trial **still
reproduces the 86-to-100% jump** on cable insertion while Bluetooth is selected.
This candidate is not a complete fix. RIGHT remains on its
previous application. No radio firmware/RAM write or pairing change was made.

## Change

The worker now requires cable and full inputs to remain observed high for
1,000 ms before publishing full or sending `A6 66`. Either input falling revokes
qualification immediately. The radio's retained percentage is not overwritten,
and wired percentage reporting and the existing sampler recovery remain enabled.
This protects a demonstrated control-path exposure; it does not establish that
the original USB-entry error came from a false full input.

The existing battery getter `0x72/0x21` and normal status structure retain their
layout. Canonical Studio adds cache-only operation `0x23`, magic `BTRC`, with
raw cable/full, qualified full, physical selector, last submitted charger command,
previous valid percentage and uptime of the last transition to 100%, saturated
full-command/raw-full-rise counts and last full-command uptime. The raw-full
flag is cable-qualified. Counters describe complete UART FIFO submission, not
radio acknowledgment. Neither getter proves a fresh voltage conversion.

A separate trace getter uses the original cache lock; frequent native sensor and
LED reads do not copy trace data. Expiration now precedes RX parsing so the
first reply after a stale interval cannot record expired percentage history as
valid. The additional getter retains the existing LEFT/USB/wired gates and
does not extend an updater lease.

The temporary legacy bridge and F24 qualification builds reject the new trace
operation and omit collection. They retain their previous features and the new
full qualifier. This keeps every supported image inside its existing flash
region without removing product features or reducing stacks.

## Verification

- The first qualifier assertion failed before the fix. Native fixtures now pass
  for short pulses during USB quiet time, stable full at startup/time zero/wrap,
  immediate falling edges, immutable partial frames and saturating counters.
- Additional packet/reader assertions failed before implementation. The normal
  getter byte layout remains unchanged; trace framing, nonce, payload, role and
  USB/wired rejection paths pass. Getter fixtures assert zero MMIO/UART access.
- A fresh reviewer found stale history consulted before expiration. Tests for
  stale 86% and stale 100% with no intermediate empty poll reproduced the bug;
  moving expiration before RX makes both pass. The refinement was reviewed
  again with no material findings. Review was source-only.
- `make test` and `make check-tools` pass after the final source changes. This
  includes the full C/native suites, 92 repository tests, 195 updater-tool tests
  and 41 ZMK Python tests. Strict typing, lint, formatting and targeted GCC static
  analysis pass. The 15 affected battery/trace tests also pass on native Windows
  Python 3.12.12; Linux uses the pinned uv project runtime.
- All eight wired ZMK distributions compile and pass their image checks:
  canonical Studio, its update test, native/legacy bridge, radio qualification,
  wired updater, peripheral, wired update test and staging qualification.

## Candidate resources and identity

Canonical application: 106,316 / 106,496 bytes, CRC32 `3247243295`, SHA256
`1c0604283dc4981c1e47485c40321ea1cdd104abff2ff9a13b9914075f8da73b`.
RAM is 28,202 / 28,544 bytes; 342 bytes remain unallocated after reservations.
The ten stack arrays still reserve 14,336 bytes. Compiler frame records show
battery poll 32, ordinary getter 24, trace getter 12 and receiver callback
64 bytes; these are per-function static frames, not whole-call-chain or runtime
stack watermarks. No hardware watchdog was added.

The bridge is 106,348 bytes. The F24 image is 106,492 bytes, leaving four flash
bytes, so later changes must recheck its size. Canonical and RIGHT still use one
universal image. The packaged resident is the existing CRC32 `1770733249`.
Hardware checks confirm this application on LEFT and the previous 105,996-byte /
CRC32 `3957255507` application on RIGHT. Both resident CRCs remain `1770733249`.

## Installation and first physical observations

The frozen LEFT-only case is `.work/zmk-battery-transition-fix/left/`, with its
exact package and runtime hashes. Baseline verification established the expected
105,996-byte / CRC32 `3957255507` applications on both halves. One LEFT INSTALL
was acknowledged; independent observation confirmed the exact new image and
unchanged residents. Both 70-second checks passed with zero transport/relay
retries and no watchdog feeds. USB returned without a requested power cycle.
Saved Studio snapshots compare exactly and both have no unsaved edits. RIGHT
has not been changed by this task. The case's INSTALL marker forbids replay.

Before installation the ordinary getter already reported 100%, cable present
and full inactive. After installation the first 100% arrived at LEFT uptime
5,594 ms, with unknown previous percentage, zero raw-full rises and zero
full-command submissions. A bounded selection of the already-paired BT2 profile
connected with zero radio errors. Direct radio state still showed a valid
previous/current 100%, internal source, charging mode 3 and drop counter 1.
Returning output selection to USB left the full counters at zero. These samples
show that the reporting path and added trace work; they cannot distinguish an
old retained 100% from a fresh bad conversion during startup.

After the requested full power-off with USB unplugged, the user returned on
already-paired BT2. Scoped radio RAM and an uncached Bluetooth Battery Service
read both report **86%**, discharging mode 0, internal source, valid history and
drop counter 0. Five additional battery-only state reads remained at 86% with
the same mode/counter. The reset cleared the pre-existing radio history; it was
not needed to recover the installed main firmware. The next observation kept
Bluetooth selected while adding USB, separating cable insertion from wired
selection. No additional firmware write was issued during that trial. The
earlier software output round trip does not reproduce physical slider pin
transitions.

### Cable insertion while Bluetooth remains active

The user connected LEFT USB while keeping Bluetooth selected and power on.
Direct radio state and an uncached Battery Service read then both returned
**100%**, charging mode 3 and internal source. USB status independently showed
selected=active=BT2, connected, waiting=false, queue=0 and errors=0. The sampler
timer's ID and period field were unchanged from the 86% Bluetooth baseline.
The jump therefore reproduces **without entering wired output mode**. Wired
selection's 5.5-second sampler-recovery interval is not necessary for this
reproduction. Neither percentage nor timer identity measures physical voltage.

After the user moved only the slider to wired, the worker trace recorded the
**86-to-100% transition at uptime 192,074 ms**, with **zero raw-full rises and
zero full-command submissions**. Cable was present, raw and qualified full
were false, and the last submitted charger command was `0x64` (charging).
The ordinary getter returned 100%, with queries=replies=191 and age 54 ms.
Radio status was idle on USB with zero queue/errors.

This trial disproves a complete fix by the first candidate and excludes a
false-full notification as the cause of this reproduction. The radio's internal
charging estimate changed to 100% while Bluetooth remained active. Together
with the captured mapper, that points to its charging calculation: a synthetic
86% history stays at 86% at 4,099 mV, jumps to 100% at 4,100 mV, and the next
3,980 mV discharge sample remains at 100% with drop counter 1. These voltages are
emulator inputs, **not measured physical voltages**. We have not proved whether the
converted high voltage is accurate or distorted by the supply/ADC path.

### Unplugged return to Bluetooth

The user returned to BT2, tapped and released Shift, then unplugged only USB
without turning power off. Four scoped radio samples over 33.71 seconds were:

| Percentage | Charger mode | Large-drop counter |
| --- | --- | --- |
| 100% | 0, discharging | 1 |
| 100% | 0, discharging | 2 |
| 100% | 0, discharging | 3 |
| 98% | 0, discharging | 0 |

An uncached Battery Service read then returned 98%, matching radio RAM. The
sampler and Bluetooth reporting therefore updated without a power cycle in this
run. The live counter changes support the retained-filter explanation for the
initial hold. This run did not reproduce a permanent 100% after unplugging:
the stock mapper accepts drops of five points or fewer immediately, unlike its
long hold for larger drops. The observed 98% is still a voltage-derived estimate,
not an independent measurement of remaining charge or of battery endurance.

Six further scoped radio samples remained at 98%, discharging mode 0 and drop
counter 0. The last was 137.34 seconds after the first discharge capture. A final
uncached Battery Service read again matched 98%. No further power cycle or
firmware write was requested. The keyboard was left in Bluetooth/BT2 with USB
unplugged, as requested for this experiment; no USB-only cleanup was attempted.

## Result and remaining correction

Installation, saved settings and transport checks pass; preventing the original
percentage jump **fails**. The full-input qualifier protects a separate exposed
path and the trace successfully identifies this reproduction, but it does not
correct the stock radio's estimate. A false full notification and wired
selection's recovery delay are unnecessary to produce the observed jump.

The reporting chain agrees: the radio, uncached Bluetooth Battery Service and
main controller all carry the same value. Correcting only the main controller's
display would leave the Bluetooth gauge inconsistent. The verified UART query
provides filtered percentage, without raw voltage or a clean estimator-history
reset. Further work needs the radio's converted sample and an estimator or
measurement correction, preserving its Battery Service and charging status.
Actual cell voltage, voltage error and remaining charge were not measured here.

The normal split updater writes WB32 applications, not the separate FR8000
radio. This task made no radio RAM or firmware write. A persistent radio update
needs a verified recovery route: the current stock OTA copies over the running
image and has no automatic trial rollback, as recorded in
[radio recovery findings](../../RADIO-RECOVERY.md). No radio correction image is
ready for installation. Do not flash RIGHT with the first candidate as a claimed
percentage fix, and do not remove USB sampler recovery based on this result.

## Exact QMK/Vial comparison

The user recalls correct battery behavior on QMK. Comparison covers the historical
`vial-0.1.7` source tag, commit `2b004b3b67bead73ed6239ba2c77bf0988adb8af`,
the maintained QMK board sources, and the installed LEFT ZMK candidate described
above. The locally available unlocked Vial ELF was also inspected: its compiled
housekeeping calls the charger sender and its wireless transport stops USB.
That ELF contains later shared-board changes, so it is corroborating evidence,
not proof of the exact binary previously installed by the user. No physical QMK
comparison, firmware write, radio write or host-selection command was performed
for this source comparison.

| Path | QMK/Vial 0.1.7 | Installed ZMK candidate | Consequence |
| --- | --- | --- | --- |
| Percentage calculation | External stock radio; main MCU copies UART `0x5c` percentage. | Same external radio; main MCU copies the same percentage into a native ZMK sensor. | There is no separate QMK versus ZMK voltage curve in these main applications. |
| Regular queries | Every 3 seconds only on wireless transport in `LPWR_NORMAL`, when the FIFO admits the query. | Every 3 seconds in either transport, subject to UART/control and power gates. | ZMK exposes wired measurements which QMK does not regularly request. A fresh UART reply still need not be a fresh ADC sample. |
| USB while Bluetooth is selected | `usbStop`, disconnect bus, then B1 high to disable the external D+ pull-up. The keep-USB compile option is absent in the tag. | B1 is initialized low; the USB interface remains available for Studio/updater while typing is routed exclusively to the radio. | A concrete operating-condition difference relevant to cable insertion while remaining on Bluetooth. Any voltage/current effect is unmeasured. |
| USB sampler recovery | No explicit 5.5-second recovery interval in the main-firmware control path. | After completed USB selection, ordinary UART control/query work waits 5.5 seconds to permit radio sleep/wake and resume sampling. | A deliberate ZMK addition for wired battery refresh. It is unnecessary for the latest Bluetooth-mode reproduction. |
| Charger commands | `A6 64 0A` charging, `A6 65 0B` unplugged, `A6 66 0C` full, periodically after more than 1,000 ms. | The same three frames; changed state is prioritized as soon as the owner is available, then refreshed after at least 1,000 ms from submission. | Same target charging state, but different edge timing, ordering and queue behavior. QMK also sends charging status while Bluetooth is selected and USB power is present. |
| Charger inputs | A7 is refreshed into `confinfo.flag` by the power task before housekeeping; A15 is a raw pull-up input. | A7 is read directly; A15 has the same pull-up but full requires 1,000 ms continuously observed high with cable. | The new full qualifier is a difference, but zero rises/submissions excludes it as the cause of this reproduction. |
| Startup and UART owner | A 2-second deferred block queues sleep enables, version query and device reselection; physical mode scanning can also select earlier. High-priority ChibiOS UART worker sleeps one tick-rounded 50 microseconds per loop. | Radio core initially requests USB, physical selection stabilizes for 50 ms, sleep enables are pending immediately; the shared application worker services battery/radio. | Startup command order and scheduling differ; identical host selection does not imply identical radio history. |
| Retry policy | Ordinary controls, including charger state, permit up to 50 retries at a greater-than-10-ms timeout; battery query permits one retry. | Charger state and battery query each permit one retry at a 10-ms deadline; selection controls retain their separate bound. | Different failure behavior. The reproduced jump had zero radio errors, so retry exhaustion is not demonstrated as its cause. |
| Cached value and display | The historical tag initializes percentage to 100 and retains the last reply without the later freshness/range fields. Held level is wireless-only, with ten-LED quantization. Current QMK adds freshness validation. | Unknown until a valid reply; expires after 10 seconds. Held level is available in both modes with the same segment thresholds and explicit unknown coloring. | Historical QMK can retain an old value through wired charging. LED quantization cannot explain the matching direct-radio/BLE jump. |

The QMK housekeeping hook is not skipped: both pinned framework versions call
`housekeeping_task_kb()` and then `housekeeping_task_user()`. The Vial wrapper
also calls the real housekeeping function. The cached cable flag is refreshed
on each master power-task pass, including in the historical tag. Therefore an
explanation that QMK simply omits charging notifications is contradicted by the
source and the available compiled artifact.

UART pins and framing agree: PC10/PC11, AF7, UART3, 115200 baud, 8N1, no flow
control. The drivers and scheduling differ; QMK explicitly sets highest GPIO
speed for the UART pins, whereas ZMK's battery pin helper does not set that
register. No electrical waveform comparison was made. The valid radio/BLE
percentage and zero errors do not indicate incoming UART corruption in this run.

### What this comparison changes

The previous conclusion that a radio-firmware change is required was too strong.
The high value is generated inside the radio, but the main firmware controls its
charging state, host selection, wake/sampling lifecycle and USB operation. An
unchanged radio can consequently behave differently under the two integrations.

For charging in **wired mode**, QMK's lack of regular wired queries and lack of
explicit sampler recovery can preserve a previous reading or avoid sampling a
charging-voltage excursion. ZMK deliberately permits new wired samples. This is
a plausible explanation for a difference noticed across wired charging and
return to Bluetooth; QMK's actual sampler state in that sequence is unmeasured.

For **Bluetooth with USB inserted**, both sources request the same charging
state and poll about every 3 seconds. The latest ZMK reproduction happened in
this scenario, before wired selection. Query mode alone cannot explain a
QMK/ZMK difference here. USB stop/pull-up behavior, command timing, radio history
and actual sampled voltage must be compared under matching conditions. Keeping
USB for Studio is an intentional integration choice; its electrical effect has
not been established as the battery cause.

A decisive next experiment compares the same partially charged battery, profile,
RGB/load and initial radio history under historical QMK and ZMK, capturing radio
percentage/mode/filter state across cable insertion. A controlled ZMK diagnostic
that matches QMK's USB disconnect policy is another way to isolate that variable,
but neither was installed or requested from the user in this comparison. Keep
wired battery reporting, preserve Studio/settings and investigate main-firmware
sequencing remedies before concluding that radio flash is necessary. Matching the
observable UART percentage is not a substitute for measuring converted voltage.

Verification for this comparison: `scripts/test.sh` passes its native C suites
and 92 repository tests; the battery/radio UART fixtures pass in all three ZMK
configurations; radio core/key tests pass; all 11 captured-radio instruction
tests pass. These tests do not establish a matched physical QMK result. Existing
QMK transport fixtures explicitly compile the keep-USB option, so USB-disconnect
behavior is established here by tag source and available ELF inspection, not by
those fixtures. The frozen installed ZMK sources and images were not changed.
Private comparison logs are under `.work/zmk-battery-transition-fix/qmk-comparison/`.

## Sofle configuration and external evidence

Inspected the user's clean `zmk-sofle` checkout at commit
`8909b06149a7eebb5a6e7c43dd89f5686a0675d6`. Its manifest selects
`cormoran/zmk` branch `v0.3-branch+dya`, resolved during this inspection to
`4493783ef88ce2e653bf8217c92ee17140df71e3`. The inspected battery driver, mapper
and reporting source were re-fetched at that immutable commit and matched the
branch contents. This establishes the selected source, not the exact previously
flashed Sofle image: the manifest follows a branch rather than a frozen build.
The Sofle checkout was not modified, and no keyboard operation was performed.

Its `build.yaml` targets `nice_nano_v2`; the Eyelash shield selects `vbatt` with
`compatible = "zmk,battery-nrf-vddh"`. This is the shield's explicit sensing
choice, not a claim that every nice!nano configuration uses this driver. Both
halves explicitly disable the optional battery-history module. There is no
enabled special charging estimator or retained-percentage correction in these
configuration files.

| Battery path | Inspected Sofle source | GMK70 candidate |
| --- | --- | --- |
| Measurement | Nordic SAADC samples internal VDDH/5, converts to mV and multiplies by 5. | WB32 queries the external radio's filtered percentage; no verified raw-voltage query. |
| Percentage mapping | Simple discharge-voltage approximation, explicit 4,200 mV full clamp; 4,100 mV gives 87%. | Stock radio has different charge/discharge mappings; charging can produce 100% at 4,100 mV. |
| History | Recalculates from each successful ADC sample; no directional percentage clamp or 360-sample hold in this driver. | Radio retains history and can delay large downward corrections for 360 mapper calls. |
| Refresh | Default 60 seconds while active; reporting stops on idle/sleep and starts immediately when active again. | UART percentage inquiries every 3 seconds when available; the radio owns its separate sampling schedule. |
| Bluetooth gauge | ZMK updates its own native Battery Service from the sampled result. | The external radio owns the host-visible Battery Service; WB32's native ZMK sensor mirrors its UART percentage. |
| Charging indication | Display state uses USB power presence; that is separate from remaining-charge calculation. | Board cable/full GPIOs also drive explicit charger-state notifications to the radio. |

The numeric comparison is synthetic: the captured radio mapper was reset to
86% and given 4,100 mV in charging mode, producing 100%; the inspected Sofle
formula gives 87% for the same input. No physical GMK70 voltage was measured.
The native formula's explicit full clamp is 4,200 mV; integer rounding can
already produce 100% slightly below that, so it is not a charger-completion
detector. Evidence: `.work/zmk-battery-transition-fix/sofle-comparison/`.

Primary source links:

- [Selected Nordic VDDH driver](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/module/drivers/sensor/battery/battery_nrf_vddh.c).
- [Selected voltage mapper](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/module/drivers/sensor/battery/battery_common.c).
- [Selected reporting and native BAS updates](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/src/battery.c).

### Official documentation and similar reports

| Source | What it establishes | Applicability to this incident |
| --- | --- | --- |
| [ZMK battery sensing documentation](https://zmk.dev/docs/hardware-integration/battery) and [battery configuration](https://zmk.dev/docs/config/battery) | Native support includes divider/VDDH sensors and fuel gauges; a custom driver can supply state of charge. Default report interval is 60 seconds. | Confirms Sofle's architecture and the native sensor contract already used by GMK70. It does not provide missing WB32 voltage access. |
| [ZMK battery feature documentation](https://zmk.dev/docs/features/battery) and [issue #392](https://github.com/zmkfirmware/zmk/issues/392) | Windows may fail to subscribe to updates. The original report has different Windows/ChromeOS readings; a comment specifically reports Sofle RGB/nice!nano stuck at its pairing-time value. | Explains a host-only stale display, but not the matching internal-radio and uncached BLE 100% captured here. The issue's native Zephyr subscription workaround does not configure the separate FR8000 stack. |
| [ZMK issue #2972](https://github.com/zmkfirmware/zmk/issues/2972) | A reported zero-charge startup could leave native GATT at its 100% default when the first zero value did not count as a change. The issue is closed; the inspected fork updates BAS independently of the change event. | A real, different 100% bug. GMK70's radio RAM and UART themselves changed from 86 to 100, excluding a native GATT default as the whole explanation. |
| [TOTEM configuration's battery investigation](https://github.com/rleyvasal/totem-zmk-config/blob/main/Kconfig) | The project author documents an immediate 0-to-about-100% jump on USB insertion and treats charging readings as unsuitable for its discharge measurements. | A similar reported symptom on different hardware, not a verified FR8000 diagnosis or universal rule for ZMK boards. |
| [Nordic VDDHDIV5 report](https://devzone.nordicsemi.com/f/nordic-q-a/127092/battery-voltage-measurement-via-vddhdiv5-reports-incorrect-values/561713) | A user reports stagnant/jumping ADC values despite changing supply voltage; support discusses power mode, and the report remains unresolved. | Relevant caution when evaluating Sofle's supply-voltage sensor, but a Nordic-specific case cannot diagnose GMK70's different radio ADC. |
| [TI fuel-gauge discussion](https://www.ti.com/document-viewer/lit/html/SSZT786/GUID-5DD8C3D9-BCBE-467B-90E5-FAA672AAC15D) | Voltage-derived charge estimates depend on load, temperature and measurement accuracy; voltage alone can misrepresent remaining capacity. | Supports capturing converted voltage and load conditions, rather than treating a percentage as an independent physical charge measurement. |

These sources establish analogous symptoms and distinct mechanisms. No verified
external case matching the complete GMK70/FR8000 sequence was found in this
search. Sofle's native path does not itself guarantee accurate charging estimates
or immunity to USB-related voltage changes. Its useful difference is direct
voltage access and recalculation without the radio's retained filter. Reusing
that approach on GMK70 first requires a trustworthy voltage source and consistent
reporting through its external Bluetooth module. Main-firmware USB/charging
sequencing remains a possible remedy; this research does not establish that
radio flashing is required.

### User's Sofle startup observation

The user now recalls that Bluetooth -> USB looked normal, but a full shutdown
followed by startup on USB and a transition to Bluetooth left the battery
display looking full. They confirmed USB was physically unplugged and the
observation was on Sofle's own display; Windows was not checked. This rules
out a Windows-only explanation for that observation. No Sofle voltage, numeric
state of charge or continuous active-use trace has been captured, so its cause
and whether it matches GMK70 remain unconfirmed.

The selected fork provides a specific cache/scheduling hypothesis. Its reporter
samples immediately at startup, then defaults to 60-second intervals while
active. The default idle timeout is 30 seconds; entering idle stops that timer,
and returning to active requests an immediate sample. The fork permits runtime
idle settings, which were not inspected on the physical Sofle. The battery
reporter itself subscribes to activity changes, not USB or endpoint changes.
Changing the preferred output while already active consequently does not itself
guarantee a new ADC measurement.

The nice!view configuration disables idle display blanking. Its status screen
draws the cached battery level; a USB event redraws that cached level and the
separate charging marker rather than initiating a measurement. The battery fill
width is `(level + 2) / 4` with integer arithmetic: 98% and 100% have the same
width. The current configured screen therefore cannot establish an exact 100%
value from a visually full battery icon. USB presence does not directly force
its battery level to 100 in this drawing code.

One plausible sequence is an initially high USB-powered sample retained after
unplugging, with sampling delayed or paused while the display remains visible.
An initial Bluetooth sample could likewise remain visible through the opposite
transition, making that direction appear correct. This is an explanation to
test, not a reproduced Sofle fault. If sustained active use after unplugging
still yields a full indication across successful ADC samples, paused sampling
alone is insufficient; raw ADC voltage, cached percentage and display state
must be compared. GMK70 additionally has the separately demonstrated radio
filter, so visual similarity does not establish one shared upstream bug.

Inspected immutable fork sources:
[activity and runtime idle setting](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/src/activity.c),
[battery reporter](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/src/battery.c),
[nice!view status](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/boards/shields/nice_view/widgets/status.c),
[battery icon drawing](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/boards/shields/nice_view/widgets/util.c),
and [display configuration](https://github.com/cormoran/zmk/blob/4493783ef88ce2e653bf8217c92ee17140df71e3/app/boards/shields/nice_view/nice_view.conf).
No firmware or device state was changed for this investigation.

### Automatic recovery reported on both keyboards

The user now reports that both GMK70 and Sofle return to their expected battery
indication after a short wait, possibly about 30 seconds, without a full power
cycle. They subsequently clarified that Sofle corrected after pressing a key
again. The delay was not timed and the resulting numeric levels were not
supplied. This revises the description to a transient high indication followed
by correction without a power cycle; it does not establish physical
state-of-charge accuracy or identify one common cause.

It agrees with the measured GMK70 return from 100% to 98% over 33.71 seconds.
The radio's drop counter advanced 1/2/3 and then reset to 0 when the nearby 98%
estimate was accepted. That correction did not require expiry of the 360-call
large-drop hold. The radio/uncached BLE results already show active sampling
and reporting in that particular run.

GMK70's UART/native report interval is 3 seconds, while the radio owns a nominal
10-second sampler. Sofle defaults to a 60-second active sampling interval,
with an immediate sample at initialization or return to active. Both use a
default 30-second idle boundary, but that coincidence does not show idle causes
the correction. In particular Sofle's native reporter stops its timer on idle,
and its nice!view display remains visible. The user's correction after a keypress
supports the documented idle-to-active refresh path: returning to ACTIVE requests
a fresh ADC sample and can replace the retained USB-startup indication. The
observed approximately 30-second wait fits the default idle threshold but is not
a measured physical settling requirement. No live Sofle ADC/idle trace was
captured, so the voltage underlying the initial full indication remains unknown.
GMK70's recorded radio correction occurred without another keypress; Sofle's
activity-triggered explanation must not be substituted for that separate path.

Voltage changes after USB removal combined with the next accepted measurement
are plausible contributors. [TI's battery-gauging discussion](https://www.ti.com/video/3873675779001)
describes charging followed by voltage relaxation; this does not establish a
30-second physical settling time for either keyboard. No exact settling time,
lighting-triggered effect, permanent freeze or need for radio flashing is proven
by the latest observation. The first candidate still has not prevented the
initial jump, and no new firmware change was made.

Success still requires the user's partial-charge Bluetooth -> USB -> Bluetooth
sequence to pass repeatedly, with USB connected and after removal, without a
corrective power cycle. The zero full-command count in this trial directs that
work to the radio's converted voltage and charging estimate. An already
corrupted radio estimate may survive a main-MCU
update; do not confuse that retained state with a new transition on the candidate.

Private logs, task-start source snapshots and the source-input SHA256 manifest
are under `.work/zmk-battery-transition-fix/`. The freeze's `source_commit`
identifies parent `e5e8a8dd365b132d58fe5714fac5a5b9f0b25f84`; the candidate includes
uncommitted work captured by that separate source manifest. No commit or push
was performed. See [machine-readable result](result.json) and the
[implementation plan](../../superpowers/plans/2026-11-05-zmk-battery-transition.md).
