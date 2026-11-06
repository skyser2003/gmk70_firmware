# Local continuation: updater-preserving ZMK development

## Wired battery background polling removed — 2026-11-06, not installed

The user clarified that removal concerns the automatic three-second radio
percentage inquiry while wired. It is now wireless-only. The battery-level key
queues one coalesced refresh using the existing UART worker and bounded retry;
charging-status presses and releases do not request percentages. Native sensor
reads remain cache-only, and the ten-second freshness limit remains. Queued
periodic work is canceled on wired entry without cutting off a partial frame
or reassigning its ACK. Charger maintenance and the USB sampler-recovery quiet
interval are unchanged. The radio estimator's transient high reading is not
claimed fixed.

Canonical source builds to 106,044 bytes, SHA256
`f39ec4977db0c85aad56cc9f83307ab558fe6e0a9513f4534e8c8fd58e0ea0b4`.
RAM is 28,170 / 28,544 bytes; stack reservations remain 14,336 bytes. The
temporary F24 qualification image is exactly 106,496 bytes, leaving no flash
margin. Compact private battery-field ordering and reuse of the radio-idle
predicate/marker edge loop keep it within the existing boundary.

All eight wired ZMK builds, `make test`, `make test-zmk`, `make check-tools`,
strict compiler warnings and edited-C formatting checks pass. Regression tests
cover zero wired background queries, cache-only native sampling, coalesced
press requests, one retry, cancellation on wired entry, partial-frame/ACK
ownership, quiescence, right-role isolation and ongoing charger heartbeats.
The F24 tests cover pending controls and cleanup after either edge fails.

No device operation was performed for this change. Both halves remain on the
last verified 105,996-byte image below; the resident, saved layout and radio
firmware have not been modified. No user answer or physical test is pending.
See [battery policy](ZMK-BATTERY-LAYOUT.md) and the
[repository audit](CUSTOM-FEATURE-PARITY.md).

## Last battery trial rolled back — 2026-11-05

The user requested removal of the last attempted battery correction, a renewed
QMK comparison and an explanation of wired polling. Source and all eight builds
are restored to the exact pre-trial application. **LEFT is now rolled back and
verified; RIGHT already had that image and was not written.** Both applications
are 105,996 bytes / CRC32 `3957255507`, SHA256
`88a1c6490e8543201a29b73fa13bd079d5d1c0bfbf8ef26dad8b716c492fd1b2`.
Both residents remain CRC32 `1770733249`. RAM is 28,162 / 28,544 bytes, leaving
382 bytes unallocated; stacks remain 14,336 bytes, without a runtime watermark
claim. Permanent firmware still initializes/feeds no hardware watchdog.

The full-input qualifier, transition metadata/getter `0x72/0x23`, CLI `--trace`
and trace-only tests are removed. Ordinary battery getter `0x21`, the earlier
three-second wired inquiries, charger notifications and 5.5-second USB sampler
recovery remain. This rollback is not a battery-estimation fix. The stock radio
still supplies the percentage; no radio firmware/RAM or pairing was changed.

`make test`, `make check-tools`, all eight builds, formatting, lint, strict
typing and dependency-duplication review pass. Frozen LEFT case
`.work/zmk-battery-transition-rollback/left/` has a completed 70-second baseline,
one acknowledged INSTALL and successful independent 70-second after-health.
Exact identities, zero transport/relay retries, no watchdog feeds and the saved
four-layer keymap are verified. USB returned without requesting reconnect or
power cycling. **Never replay the case's INSTALL marker.** Private keymap
backups must not be printed or committed. No commit or push was performed.

Wired polling is the GMK70 integration policy for a current battery indicator
and USB telemetry during charging, not an updater requirement. Native ZMK's
active battery reporter is endpoint-independent, but GMK70's separate UART
worker implements the three-second inquiries. QMK's regular inquiries are
wireless-only in normal power state; its power-state assignment hook also sends
a one-off inquiry without that gate, so it is inaccurate to say QMK never
queries while wired. ZMK additionally keeps USB available during wireless use
and has different startup/charger timing. See the
[rollback comparison](validation/2026-11-05-zmk-battery-rollback/README.md).
No physical test or user answer remains pending. The older trial below is
historical evidence and does not describe the installed image.

## Battery transition candidate — historical trial, 2026-11-05

Latest user update: both GMK70 and Sofle return to expected battery indication
after a short wait, possibly30s, without powercycling. Exact delay/levels are
unmeasured; this changes the description to a transient high reading with
automatic correction. Existing GMK trace100->98 over33.71s already supports it.
User clarified Sofle corrected after pressing a key again. This supports its
idle-to-ACTIVE fresh-sample path; actual ADC/idle timing remains uncaptured.
No question remains pending. GMK's radio trace already corrected without a
keypress, so do not assign one shared idle mechanism to both keyboards.

User authorized trying the main-firmware fix and supplied LEFT USB/wired. The
candidate is **installed on LEFT**: exact image/residents and both-half 70-second
health checks pass before and after. Saved Studio keymap compares exactly, with
no unsaved edits. RIGHT remains on application 105,996 / CRC32 `3957255507`.
USB returned without a requested power cycle. **The first candidate does not
fully fix the fault**: after clearing old history, radio/BLE read 86% unplugged;
adding USB while staying on BT2 produced 100%, charging mode 3 and internal
source. USB status independently verified selected=active=BT2, connected,
queue=0, waiting=false and errors=0. Wired recovery is unnecessary for this jump.

The worker requires 1,000 ms continuous observed cable/full before sending full,
with immediate falling-edge revocation. Canonical Studio has a separate cached
transition getter (`read_battery.py --trace`, opcode `0x72/0x23`); normal battery
getter `0x21` and its status layout remain compatible. Metadata records full
pulses, complete charger-frame submissions and the previous percentage/time at
100%. It cannot establish fresh voltage. The reproduced increase originates in
the radio's estimate; physical voltage and ADC correctness remain unmeasured.
USB recovery and wired percentage reporting remain enabled.

Candidate: 106,316 bytes, CRC32 `3247243295`, SHA256
`1c0604283dc4981c1e47485c40321ea1cdd104abff2ff9a13b9914075f8da73b`.
RAM 28,202 / 28,544, stacks unchanged 14,336; no watchdog. All eight builds,
`make test`, `make check-tools`, strict typing/lint/format, static analysis and
15 Windows battery tests pass. Fresh review's stale-history ordering finding
was reproduced and fixed, then re-reviewed. Temporary bridge/F24 images omit
only the new trace, retaining their previous features and full qualification;
F24 image is 106,492 bytes, with only four flash bytes spare.

Freeze `.work/zmk-battery-transition-fix/left/` is LEFT-only and expects both
last-verified before images 105,996 / CRC32 `3957255507`. Its frozen `source_commit`
is the parent; uncommitted candidate inputs have a separate SHA256 manifest.
The baseline, one INSTALL acknowledgment and after-health evidence now exist;
**do not replay INSTALL**. The initial radio state was already 100% before
installation. A software USB -> BT2 -> USB round trip worked with zero errors,
but could not distinguish retained history from a new bad reading.

The user then established an unplugged Bluetooth baseline after powering off:
radio and uncached BLE both reported 86%. Adding USB while remaining on BT2
produced 100%, charging mode 3, unchanged timer identity and zero radio errors.
Moving only the slider to wired exposed the trace: 86% -> 100% at uptime
192,074 ms, **zero raw-full rises and zero full commands**, last charger `0x64`.
The ordinary getter matched 100%, queries=replies=191 and age 54 ms. Thus a
false-full notification is also unnecessary for this reproduction.

The user returned to BT2, tapped Shift and unplugged only USB, keeping power on.
Four radio samples over 33.71 seconds were 100/100/100/98%, all discharging,
with drop counters 1/2/3/0. Uncached BLE then matched 98%. Six later reads stayed
98% with counter 0; final observation was 137.34 seconds after the first
discharge capture. A final uncached BLE read also matched. This proves updates
without a power cycle in this run; permanent 100% after unplug was not reproduced.
The captured mapper separately proves its 4.1 V charging threshold and long
large-drop hold. Those tests use synthetic voltages; actual ADC input is unknown.

No question remains pending. Keyboard was left on Bluetooth/BT2 with USB unplugged.
The user's QMK comparison is now recorded in the validation report: historical
Vial0.1.7 sends the same charging commands, but polls only in wireless mode,
stops USB/disables B1 pull-up on wireless selection, and has no explicit wired
sampler recovery interval. ZMK keeps USB available for Studio, queries both modes,
and has the 5.5-second recovery plus different edge/queue/startup timing.
The historical tag's battery cache has no freshness handling; current QMK does.
The bad value originates in the unchanged radio, but concluding radio flash is
required is premature: main-firmware operating conditions differ. No matched
physical QMK or QMK-style USB-disconnect experiment was run. Next isolate those
differences and converted voltage before deciding the remedy. Do not blindly
remove recovery or overwrite percentage. UART exposes no verified raw voltage
or clean history reset. No radio correction image is prepared, and
persistent radio recovery remains unverified; see `docs/RADIO-RECOVERY.md`.
Do not update RIGHT with this candidate as a claimed percentage fix. Preserve
resident CRC32 `1770733249` and saved mappings. Private backups contain bindings
and must not be printed or committed. No radio firmware/RAM or pairing change
was made. No commit or push was performed for this candidate.

Follow-up Sofle/source research: clean `zmk-sofle` checkout8909b061 selects Nordic
VDDH ADC via the Eyelash shield and disables battery-history on both halves.
Selected cormoran fork resolved to4493783ef88ce2e653bf8217c92ee17140df71e3;
immutable driver/mapper/reporting sources verified. Native calculation gives87%
at synthetic4100mV; captured GMK radio incharging mode gives100% for the same
synthetic input. Native reporting defaults60s whileactive, pausesidle/sleep, and
owns nativeBAS; GMK radio owns independentBAS and only exposespercentage toWB32.
No special Sofle charging fix was found. Official docs and cases #392 (Windows
subscription, including Sofle report), #2972 (zero-at-startup/nativeGATT100), a
TOTEM USB0->100 observation and a Nordic ADC sticking report were inspected.
None establishes GMK's exact cause. Details/source links are in the validation
report. No code/device changes, no additional physical test or question pending.

User then recalled Sofle also stays visually full after cold USB startup ->
Bluetooth, confirming cable unplugged and Sofle display (Windows unchecked).
Checked fork nice!view implementation: cached SOC, no USB-forced100, icon fill
rounds98/100 to same width; idle display blanking disabled. Native battery
reporter samples initially/every60s active, stopsidle (default30s, runtime setting
unknown), resumes immediately onACTIVE; reporter has no USB/endpoint subscription.
Initial USB sample retained whileidle/displayvisible is a plausible explanation,
not proven; sustained active-use and raw voltage/SOC need capture to distinguish.
Do not attribute both boards to one bug or treat Sofle as a proven good reference.
No question pending; no code, firmware or device operation performed.
See [validation and limitations](validation/2026-11-05-zmk-battery-transition/README.md).

## Live battery follow-up — 2026-11-05

Historical checkpoint: the trial above supersedes the unresolved trigger and
hardware-availability statements below. The earlier captures remain evidence
for the estimator and ADC hypotheses, not the latest device state.

First-100% investigation: native cache/LED and captured UART query handler do
not force 100 on USB entry. A production-core C trace sends no full notification
with stable false full input; a synthetic 10 ms full pulse does send one. Cold
radio ADC setup consumes ten ignored conversions, while periodic setup schedules
one completion after 10 ms without those ignored reads. Eleven instruction tests
pass. Ordinary Bluetooth uses the same periodic path, so this does not establish
an analog settling fault. The first bad converted sample or a transient full
state still needs correlation with USB recovery. No firmware/RAM write, output
selection or reset was requested. See
[initial-result analysis](validation/2026-11-05-zmk-battery-capture/INITIAL-100-ANALYSIS.md).

User's latest reproduction: Bluetooth -> wired becomes 100%; returning to
Bluetooth retains 100% with or without USB power; full off/on restores normal.
This is stronger transition evidence than the isolated captures below. Three
additional captured-instruction tests conditionally explain persistence after
one high sample or transient full-mode input; all eight mapper tests pass.
The first incorrect sample's trigger is still unproven. Investigate QMK/ZMK
USB control ordering, transient full notifications and sampler wake validity,
including the 5.5-second quiet workaround. Preserve wired percentage reporting
and exclusive host routing. No firmware/RAM write, mode switch or reset requested.
Latest isolated direct Bluetooth snapshot: 98%, discharging mode 0, internal
source, drop counter 0 and armed timer; not a capture of the reported sequence.
See the validation's `mode-latch-result.json` and final section. Private evidence
ends in `.work/battery-estimator-investigation/2026-11-05-mode-latch/`.

Latest wired return: local USB telemetry confirms 100%, cable present and
hardware full inactive. Seven samples over 60.047 seconds retain 100%, with
query/reply counts 144 -> 164, selected=active=USB and radio errors 0. LEFT uptime
429,446 -> 489,462 ms advances continuously during this capture. File timing
places the previous 86% Bluetooth capture about three hours earlier, before the
current main-controller boot; this is not an immediate mode-switch comparison.
The captured mapper can reach 100% at 4,100 mV before hardware full and retain it
while charging, but no physical voltage or radio sampler freshness was measured.
No firmware/RAM write, output switch or reset was requested. See the validation's
`wired-return-result.json` and final section; private evidence ends in
`.work/battery-estimator-investigation/2026-11-05-wired-return/`.

Latest battery-only follow-up, with Bluetooth mode preserved: the radio and an
uncached BLE read both show 86%. Thirteen samples over 63.165 seconds remain at
86%, charger mode 0, internal source, BAS enabled and drop-counter 0; the sampler
timer is armed. No large-drop hold progression was observed. New ADC conversion
freshness and real voltage remain unmeasured. See the validation's Bluetooth
section and `bluetooth-result.json`; private evidence ends in
`.work/battery-estimator-investigation/2026-11-05-bluetooth/`. No firmware/RAM
write, output switch, pairing or reset was requested in this latest follow-up.

User connected USB without a requested power cycle. Both applications/residents
still verify the November 4 identities below. USB, radio RAM and uncached BLE
Battery Service all read 100%; charging mode 3, internal source and armed sampler
timer were observed during a temporary existing-BT2 probe. Baseline UART replies
advanced normally, with zero radio errors and continuous LEFT uptime. The ceiling
prevents this capture from proving a charging freeze or fresh ADC conversions.

The manual unplug comparison ended after 11 charging samples and obtained no
discharge sample. Cleanup failed while USB was absent and masked the initial
exception. Both main controllers had restarted by the later inspection, so the
continuous-state comparison is invalid. Sleep/power cause remains unrecorded.
USB output is explicitly restored and verified selected=active=0 with no pending
work/errors. No firmware/RAM write, pairing or software reset was requested.
See [capture and limits](validation/2026-11-05-zmk-battery-capture/README.md).

## Battery estimator analysis — 2026-11-04

The user reports higher levels while charging and values that refresh after a
full power-off. Stateful execution of the stock radio mapper confirms a
360-sample large-drop hold and mode-dependent clamps that retain old values.
The native ZMK cache timestamps replies, not ADC conversions. See the
[analysis and offline proof](validation/2026-11-04-zmk-battery-estimator/README.md).
Five captured-instruction tests, strict typing/lint/format and the existing
power-core tests pass. No firmware was changed or flashed; the installed image
below remains current. Live USB telemetry was unavailable and the known BLE
profile was unreachable. The user has been asked to connect LEFT USB in wired
mode without fully powering off, to preserve the current stale state for capture.

## Battery sleep handoff correction installed — 2026-11-04

Both halves now run the same 105,996-byte universal image, CRC32 `3957255507`,
SHA256 `88a1c6490e8543201a29b73fa13bd079d5d1c0bfbf8ef26dad8b716c492fd1b2`.
Residents remain CRC32 `1770733249`. Each half received one acknowledged
INSTALL; four before/after 70-second health windows passed with zero retries,
reconnects or feeds. No recovery or right-only connection was needed. Exact
four-layer/296-binding saved Studio snapshots verify unchanged and clean.

The sleep handshake no longer treats its own peer RPC as physical input, and
ordinary LEFT matrix polling stops during preparation so it cannot restart
traffic at the final pause. Full UART/storage and genuine-wake gates remain.
The previous unflashed radio-clear USB-detour correction is also included.
RAM remains 28,162 / 28,544 with unchanged 14,336 stack bytes and no watchdog.
Eight builds, full tests, typing/lint/format checks and the new failing-then-
passing regressions pass. See [validation](validation/2026-11-04-zmk-sleep/README.md).
The user completed the requested battery-only idle test and confirmed a keypress
resumed Bluetooth typing normally. Subsequent uptimes were about 37 seconds on
both halves, with exact application/resident identities and zero feeds. This is
positive functional wake evidence. The watcher expired before a verified wired
return, so exact sleep/reconnect timing and entry reason were not captured.
Saved Studio layers/bindings remain identical after the test. Sleep current and
overnight endurance remain unmeasured; see the validation record for limits.
The additional post-wake both-half 70-second health window also passes with no
retries, reconnects or feeds and unchanged image/resident identities.

## Repeating-key investigation — 2026-11-03

The [offline follow-up](validation/2026-11-03-zmk-radio-switch/STUCK-KEY-ANALYSIS.md)
found no main-MCU per-switch heap leak or sanitizer-detected corruption. A host
model reproduces a held key plus stopped reports when release delivery and its
emergency zero fail. The original trigger remains unproven. A concrete USB
detour during radio-owned HID clear is corrected and regression-tested in
source. Candidate: 105,928 bytes, CRC32 `1671338335`, SHA256
`1d6102e98c74b6c123426a1c2e10f91f348f883de62958386b6f90f543a5ef96`;
RAM and stack reservations are unchanged. At this checkpoint the candidate was
not flashed; its correction is now included in the November 4 image above.
No device operations were performed during the follow-up. Installed identity
below remains authoritative. Residual radio-state/delivery/queue risks and
unmeasured live stacks/radio heap are documented in the follow-up.

## Bluetooth shortcut transition correction — 2026-11-03

Current installed universal image on both halves: 105,900 bytes, CRC32
`2042255645`, SHA256
`897d6ea48bd14a9b119b009c8a78e0aee3985529358af7f4209aa036042331fa`.
Residents remain CRC32 `1770733249`; no radio firmware was written. Older
installed image identities below are historical.

Latest radio requests survive old control completion/failure, obsolete PAIR
retries stop, and an attributed CONNECTED can recover a fully transmitted SELECT
despite separate ACK loss. A finite 20 ms late-ACK quarantine precedes transfer
to another control; arbitrary delayed untagged packets remain unattributable.
The existing 5.5-second USB ADC-recovery interval remains. Core state is still
228 bytes and total RAM remains 28,162 / 28,544 bytes with unchanged stacks.

Eight builds and all required checks pass. Both images/residents and four
70-second before/after health windows verify, with zero retries/reconnects/feeds.
RIGHT's single INSTALL acknowledgment was unvalidated; no request was replayed.
Its exact running image and final health window established completion. Exact
four-layer/296-binding Studio map and native metadata/geometry remain preserved.

Automatic BT1/BT2/return/rapid-return checks passed with zero radio errors. The
user reports the original shortcuts work initially but will continue normal use
because the prior failure appeared only after several switches. **Do not claim
long-term intermittent stability is proven.** No matching USB diagnostic
interface was available for the final post-retest snapshot. See
[validation](validation/2026-11-03-zmk-radio-switch/README.md), private evidence in
`.work/zmk-profile-switch-investigation/`, and the
[three-stack audit](CUSTOM-FEATURE-PARITY.md) for remaining gaps and policies.

## Battery refresh follow-up — 2026-11-03

Both joined halves now run the same 105,972-byte canonical Studio image,
CRC32 `1734310917`, SHA256
`e940c81a45798f74480c742efc7ea4cbd7936ea141be22cb0ed7d3d53a8c646a`.
Both residents remain CRC32 `1770733249`; no radio firmware was written.
The USB-selection fix inserts one 5.5-second radio UART quiet interval, then
resumes normal maintenance so the stock radio can restart its battery sampler.
Bounded radio faults also retire their work instead of permanently blocking
battery queries and MCU sleep. No watchdog, stack reduction or new mutable state
was added. RAM remains 28,162 / 28,544 bytes.

Charging progressed from 58% to 73% on continuous LEFT uptime with USB still
selected, after the old image had held at 30% and then 49%. Exact Studio layers,
296 bindings and native metadata/geometry verify after both installations.
RIGHT's single INSTALL had an unvalidated acknowledgment; do not replay it.
Independent exact readback and 70-second both-half health established completion.
See [validation](validation/2026-11-03-zmk-battery-refresh/README.md) and private
evidence under `.work/zmk-battery-investigation/`.

Physical battery-only endurance/current remains unqualified. A charging-curve
percentage and fresh UART reply do not by themselves measure energy consumption.

## Basic feature parity installed — 2026-11-03

[Basic parity](ZMK-BASIC-PARITY.md) adds coordinated battery sleep/wake, native
RGB idle-off, critical/low-battery handling, status/lock LEDs, persistent OS/host
preferences, safe wireless runtime saves, native mouse transport, USB remote wake
and deliberate reset/recovery. It keeps one universal image, resident preservation
and the permanent no-watchdog policy. See the
[validation record](validation/2026-11-03-zmk-basic-parity/README.md).

The same build also restores the stock radio's charging/stopped/full
notifications. The earlier port omitted these while continuing to query the
module, matching a reported battery level frozen at 3% until a restart. See
[charging-update details](ZMK-BATTERY-LAYOUT.md#battery-implementation).

Both halves now run the same 105,876-byte image, CRC32 2614232591; both residents
remain 1770733249. One acknowledged INSTALL per half returned automatically
through joined LEFT USB, without a manual reconnect, right-only connection or
pads. Final both-half health passed 70.265 seconds with zero retries,
reconnects/feeds. All four saved Studio layers and 296 bindings, native metadata
and geometry verify after each update. RAM is 28,162 / 28,544 bytes, including
unchanged 14,336 stack bytes, leaving 382 bytes free.

Battery replies are fresh with zero radio errors, currently 60% while cable/full
inputs indicate charging. No percentage increase or Windows Bluetooth refresh
was observed; physical sleep/wake, key/knob/LED actuation and overnight life
remain unverified. The reviewed source is still uncommitted because required
GPG signing was unavailable; its patch/overlay and exact packages are frozen in
ignored private evidence. The following checkpoints are historical.

## Native updater framing — 2026-11-02

[Native ZMK updater frames](ZMK-NATIVE-UPDATER-WIRE.md) now carry the relay.
Both halves run the same canonical 89,308-byte application, CRC32 599821369;
both residents remain 1770733249. The final RIGHT firmware transfer was sent by
native-only LEFT, whose ELF excludes legacy updater RPC18. Matrix/RGB and
behavior RPC17 retain their board transport. One explicit bridge artifact remains
available for migrations to older applications.

Four distinct image/target INSTALLs migrated LEFT bridge, RIGHT bridge, LEFT
canonical, RIGHT canonical. Both halves stayed joined to LEFT USB; no manual
cycle, right-only USB or pads were needed. All four layers and 296 saved Studio
bindings were independently preserved after each step. RAM remains 28,322 /
28,544 bytes, with 14,336 reserved stack bytes and 222 bytes unallocated.

The first post-RIGHT-bridge health window was rejected; fresh diagnostic and
strict checks passed. The final RIGHT INSTALL acknowledgement also failed strict
validation, but **was not resent**. Independent INFO, a 70.203-second diagnostic,
final 70.171-second both-half qualification and Studio verification proved that
both canonical images run correctly. Exact cause of the acknowledgement mismatch
is unresolved; see the [validation record](validation/2026-11-02-native-updater-wire/README.md).
Source and sanitized validation are published together in a signed commit on
`codex/zmk-native-integrations`. The exact reviewed predeployment tree/archive
remains recorded there. The following checkpoints are historical.

## Native encoder and split cores — 2026-11-02

[Native ZMK integrations](ZMK-NATIVE-INTEGRATIONS.md) are installed on both joined
halves: the same 88,424-byte application, CRC32 192780560. Each half received one
INSTALL through LEFT USB and returned automatically, without cable reconnect or
right-only recovery. Both resident CRCs remain 1770733249. All four saved Studio
layers and 296 bindings are preserved; geometry and runtime metadata verify.

The pre-flash Studio ELF reserves 28,322 / 28,544 RAM bytes, including unchanged
14,336-byte stack reservations; 222 bytes remain unallocated. The native generic
cores use the retained GMK70 wire framing for updater/RGB/legacy compatibility;
this is not upstream UART transport or general GLOBAL behavior fan-out.

The first post-RIGHT strict health window was rejected for an unrecorded
counter/threshold condition. No flash/reset was repeated. An independent fresh
70.203-second diagnostic and final 70.172-second strict both-half qualification
then passed with exact identities and zero retries/reconnects/feeds. Hardware
stack peaks and physical knob/key/RGB actuation remain unmeasured. See the
[validation record](validation/2026-11-02-native-zmk/README.md).

At this checkpoint, source changes were uncommitted because noninteractive
signing could not use the user's key. The exact reviewed source tree and archive
are recorded in validation; publication now includes the subsequent native
updater framing in the signed branch commit described above.
The following checkpoints are historical.

## Universal full-featured ZMK — 2026-10-30

[One common application](ZMK-UNIVERSAL.md) is installed on both halves:
79,624 bytes/CRC32 893851802, source `bf0ccad85030f886e39aa13c0663e8c022aab654`.
PB9 selects runtime roles; only LEFT owns saved Studio storage. Legacy Studio
packages stay LEFT-only; the new `zmk-studio-universal` profile explicitly
supports either half. Both residents remain 1770733249.

LEFT then RIGHT each received one INSTALL and returned automatically through
joined LEFT USB. Five 70-second both-half health windows passed, with no physical
reconnect/pad procedure and zero retries/reconnects/feeds. All 288 saved bindings,
four layer names/order and factory defaults are preserved. Normal Studio is
connected after desktop startup reconnect recovery, without another firmware
write. The user confirmed keys, both RGBs and the saved lighting action work.
See [validation and hardware limits](validation/2026-10-30-zmk-universal/README.md).
The following checkpoints are historical.

## Function/media flip — 2026-10-30

[Persistent function/media flip](ZMK-FUNCTION-FLIP.md) is installed on LEFT:
79,612 bytes/CRC32 799253422. RIGHT remains 43,752/2368050678; both resident CRCs
remain 1770733249. One LEFT INSTALL returned automatically with no physical
reconnect, RIGHT write or pad recovery. Baseline, post-install and final
post-layout health each passed 70.375 seconds with zero retries/reconnects/feeds.

Studio advertises the parameterless action. Its one saved binding is restored;
all other 287 bindings, four layer names/order, source export and factory
defaults are preserved. Normal Studio is connected. Flip starts off; mechanical
actuation, host/media response and enabled-state reboot persistence remain
unobserved on hardware, with dedicated native coverage. Four original positions
remain unassigned: logo effect, a reserved action and two QMK settings resets.
See [sanitized validation](validation/2026-10-30-zmk-function-flip/README.md).
Earlier checkpoints below are historical.

## Bluetooth and dongle support — 2026-10-29

[Wireless support](ZMK-WIRELESS.md) is installed on LEFT: 78,032 bytes/CRC32
3592996973. RIGHT remains 43,752/2368050678; both resident CRCs remain 1770733249.
Two LEFT-only updates returned automatically with no physical reconnect or
right-only operation. Bluetooth slot 2 reused the existing Windows bond and
delivered two independently observed F24 press/release pairs on the temporary
qualification image; the final normal image excludes that command and also
reconnects to the existing bond. Dongle selection is implemented, but receiver-side
typing is unverified because no receiver is detected on this host.

Eight wireless bindings are restored in saved Studio settings; all other 280
bindings and four layer names/order are preserved. Normal Studio is connected
and USB output is restored. Final both-half health passed 70.140 seconds with
zero retries/reconnects/feeds. Five of the original 13 unsupported bindings remain:
function/media flip, logo effect, a reserved action and two QMK settings resets.
See [sanitized validation](validation/2026-10-29-zmk-wireless/README.md).
Earlier checkpoints below are historical.

## Battery indicators and saved layout import — 2026-10-29

[Battery actions and saved Vial layout migration](ZMK-BATTERY-LAYOUT.md) are now
installed and verified. LEFT is 74,020/3805544436; RIGHT remains
43,752/2368050678 and both resident CRCs remain 1770733249. LEFT returned
automatically after one INSTALL, without physical reconnect. Final both-half
health passed 70.203 seconds with zero retries/reconnects/feeds. Fresh telemetry
reports 100%, cable/full asserted and 111 replies to 111 queries. The user
confirmed both held indicators and lighting restoration. All 275 supported
bindings were imported into saved Studio settings, with the selected 13
unsupported bindings unassigned; all 288 bindings were saved, reloaded and
independently compared. Factory defaults and the private source export are
unchanged. Normal Studio is connected with all four layers. The checkpoints below
are historical.

## Right-half RGB restoration — 2026-10-29

[RGB-enabled canonical builds](ZMK-RIGHT-RGB.md) are now installed on both
halves: LEFT 70,672/1286125312; RIGHT 43,752/2368050678. Both returned automatically
through joined LEFT USB, with no reconnect or pad procedure. Final both-half
health passed 70.234 seconds with zero retries/reconnects/feeds; resident CRCs
remain 1770733249. Studio connects and the user's freshly saved four-layer map
is preserved exactly. The user confirmed that right RGB works and Fn+X controls
both halves together.
The watchdog-removal checkpoint below is the preceding installation.


## Permanent watchdog removal — 2026-10-29

The user-requested [permanent removal](ZMK-PERMANENT-WATCHDOG-REMOVAL.md) now
applies to all canonical permanent ZMK builds and both installed halves.
LEFT is standard Studio 70,196/986599886; RIGHT is no-lighting peripheral
38,160/1887741245; both resident CRCs remain 1770733249. LEFT returned automatically;
RIGHT's old-watchdog transition required one confirmed joined-LEFT cold cycle.
Both then passed 70.125 seconds of zero-feed health; new RIGHT staging writes and
actual Studio save/reload/full original map restoration passed. No right-only
USB/pad recovery or repeated INSTALL occurred. The long watchdog adapter and
outgoing handoff are deleted; temporary staging-trial protection remains separate.
Use the linked current evidence; earlier checkpoints below are historical.

## Previous LEFT watchdog rollback — 2026-10-29

[Watchdog removal](ZMK-WATCHDOG-ROLLBACK.md) restored the original SYSRESETREQ
update path on LEFT in two distinct-image tests, without further cycles after
one bootstrap cold start. LEFT now runs the separately labeled no-watchdog
Studio image 70,340/1796245169; RIGHT remains 36,576/1296309606 and both resident
CRCs 1770733249. Both updates passed 70-second health; actual Studio save/reload
and complete original four-layer map restoration passed. Normal Studio connects.
LEFT no longer has watchdog hang recovery or outgoing resident handoff. Ordinary
variants retain the watchdog; RIGHT reset behavior is untested. The exact
watchdog/reset-retention mechanism remains unmeasured. Use the linked validation
for the current checkpoint; the handoff experiment below is historical.

## LEFT update activation workaround — 2026-10-29

The [experimental resident handoff](ZMK-RESIDENT-HANDOFF.md) is now hardware-tested
on LEFT. After one bootstrap cycle required by the old sender, distinct-image
A→B→A updates returned automatically and passed both-half health with zero
retries/reconnects. LEFT now runs the separately labeled Studio handoff A
(71,684 bytes/CRC32 `2883208108`); RIGHT remains 36,576/`1296309606`. Residents
remain `1770733249`. Actual Studio RPC reads four layers; normal Studio reconnects.
LEFT renders local RGB only; peer lighting is disabled in these experiment
variants to leave the older RIGHT untouched. Ordinary SYSRESETREQ remains
undiagnosed, and RIGHT handoff is untested. Use the linked current evidence
instead of assuming every normal build includes the workaround.

## Offline candidate — 2026-10-29

[Mac layers and native lighting](ZMK-MAC-LIGHTING.md) are implemented and built
offline. They are **not installed**: the verified hardware checkpoint below is
unchanged. Four-layer compatible settings migration, RGB DMA and wired frame
tests accompany the candidate. [Warm-restart analysis](ZMK-WARM-RESTART-ANALYSIS.md)
narrows hypotheses without claiming a root cause or fix. No device operation
was made while the user slept. Read the candidate's hardware-validation and
stack-watermark limits before considering installation.

## Current status — 2026-10-28

[Wired ZMK Studio](ZMK-STUDIO.md) is now implemented and hardware-verified.
LEFT runs the experimental unlocked Studio central (61,696 bytes/CRC32
`1010245074`); RIGHT remains permanent normal A (36,576/`1296309606`). The installed
local Studio app connects via USB serial COM3, displays 72 keys and Base/Fn, and
saved edits survived a distinct-image LEFT update. The full original map was
restored and reloaded. Final both-half health passed 61 seconds with zero
transport/relay retries; residents remain `1770733249`. No RIGHT flash or pad
recovery was used. Warm restart still needs the documented joined-left cycle
after a timeout. See [Studio validation](validation/2026-10-28-zmk-studio/README.md).

## Previous permanent checkpoint — 2026-10-28

The permanent wired milestone has superseded the software-only/staging handoff
below. Read [Permanent wired ZMK](ZMK-PERMANENT-WIRED.md) and the
[sanitized validation](validation/2026-10-28-zmk-permanent/README.md) first.
Both halves now run permanent normal A (36576 bytes/CRC32 `1296309606`), with
unchanged residents and 61 seconds of fresh both-half health. Shared recovery,
right Q and distinct-image left/right updating through LEFT USB were verified.
Automatic restart remains unresolved; a joined-left USB cycle is a tested fallback
when restart verification times out, followed by read-only verification. The final
strict manual stage was unexecuted because cycle confirmation was unavailable;
independent final readback/health establishes A/A without an automatic-restart
claim. The user reports typing seems functional and Fn+number sends function keys.
Follow-up Windows observation confirmed Fn+Left/Right as Home/End without Ctrl held;
the input field's word-like navigation remains unexplained. Hardware peer-release
is untested.

Native permanent source is `0554dff`; the manual plan is `0e0a699`. Maintained
integration remains in this repository. Existing private configuration backups
and residents are preserved; no post-ZMK settings readback is claimed.

## Historical handoff and staging checkpoints

The text below records the 2026-10-26/27 investigation and its then-current
state/authorization boundaries. Its Vial-only status and disabled-RUN notes are
historical; use the current milestone above for present operation and permissions.


Handoff prepared 2026-10-26. The user requested continuing this work in the local
`gmk70_firmware` project. The immediate goal is to develop ZMK without repeatedly
moving USB between the halves or losing the working updater during early tests.
The Bluetooth-radio firmware project is deferred.

## Start here

Read [AGENTS.md](../AGENTS.md), [ZMK feasibility](ZMK-FEASIBILITY.md),
[the proposed trial workflow](ZMK-UPDATER-TRIAL.md),
[the installed updater contract](SPLIT-UPDATER.md), and
[the platform layout](FIRMWARE-ARCHITECTURE.md).

The cloud research branch is `codex/zmk-updater-investigation`, based on
`91131f728b3c97a1ff1f80305719c7fa534fba68` (the manual-key additions already
pushed to main). Fetch and inspect that branch without resetting or overwriting
existing local work. Keep the radio research branch and private local backups
separate. The cloud branch contains documentation, not a working ZMK port.

Continue with software design, implementation, offline tests and builds. This
handoff does not request installing firmware, resetting the keyboard or running
device programming tools. Do not reuse older radio-operation authorization for
these new main-MCU experiments. Preserve the separation of build/test from
physical device operations required by AGENTS.md.

## Confirmed findings

- Neither inspected upstream Zephyr nor ZMK's selected Zephyr has an established
  WB32FQ95 port. WB32 startup/clocks/GPIO/USB/UART integration is new work. The
  Cortex-M3 architecture itself is supported.
- Research references: ZMK `4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85`, its selected
  Zephyr `10ba6d0cb38bc3d258775d27982f707599320085`. These are research snapshots,
  not new repository dependency pins. Freeze exact versions when implementing.
- The saved records identify the user's two main MCUs as 256 KiB flash / 36 KiB
  SRAM. Current application linking and resident stack validation use 28 KiB.
- Resident: first 8 KiB. Normal application: `0x08002000`, 120 KiB maximum.
  Staging: `0x08020000`, 120 KiB. Commit page: `0x0803FF00`. Boot magic:
  `0x20006FFC`. These are main-MCU addresses, unrelated to radio flash layouts.
- The resident only installs and starts applications. USB update reception and
  right-half forwarding run in Vial/QMK, not inside the resident.
- Ordinary INSTALL copies staging over the current application. A CRC-valid
  program that hangs has no automatic old-image rollback in this path.
- Existing image/host validators require application-linked vectors. They reject
  a ZMK image linked to run from staging. A distinct trial mode is required.
- Current local Vial-updater application is 92,048 bytes. It has 30,832 bytes
  remaining in its 120 KiB slot; no trial extension or ZMK size is measured.
- Split UART1 is A9/A10, full duplex, 115200; radio UART3 is C10/C11. ZMK wired
  split is available upstream but is not QMK transaction compatible.
- WB32's independent watchdog exists in vendor documentation, but there is no
  ready-to-use IWDG driver in the pinned WB32 platform driver list. Its actual
  timeout/reset/clock behavior still requires a later hardware proof.

An ignored cloud C fixture linked the actual maintained `image.c` and
`protocol.c` with RAM-backed simulated flash. With warnings as errors and
ASan/UBSan, it confirmed rejection of staging-linked vectors, preservation of
the application without an install commit, and replacement on ordinary INSTALL.
The fixture is not in this Git branch; reconstruct it locally if needed. No
CPU execution, watchdog, USB, split transport or proposed trial mode was tested.

## Candidate architecture to review first

Keep the installed resident unchanged, retain Vial/updater in its application
slot, and run short ZMK trials directly from the staging region. Arm an
independent watchdog before handoff; initial trials do not feed it. Expiry
resets the MCU, and an invalid ordinary commit record makes the resident boot
Vial again. The host can then verify identity and upload the next trial through
left USB. A left trial may trigger USB re-enumeration without moving the cable.

This is a proposal, not a validated loader. Independently review:

- Correct upper-flash execution, separately linked vectors and data-load
  addresses, RAM bounds, and reserved boot/launch/result RAM.
- A controlled startup handoff, including CPU mode/stack, VTOR, interrupt masks
  and pending IRQs, USB, timers, clocks and cache. Do not call the next kernel
  as a normal C function or assume it receives reset-state peripherals.
- A consumed one-shot launch record, predictable watchdog return and no trial
  reboot loop. Verify clock/gate/debug interactions; a software timer cannot
  recover a hung kernel. Do not let initial trial code keep feeding the watchdog.
- Trial/ordinary-install separation: dedicated package identity and operations,
  full CRC/bounds checks, no ordinary commit, and rejection of any attempt to
  INSTALL a staging-linked trial. Only one use of the staging slot at a time.
- Native Vial physical unlock for writes/launch, direct USB/wired-only access,
  role restrictions and unchanged saved settings. No weakening of authorization
  or the existing installer ABI/resident identity checks.
- Mixed-framework operation: suspend normal QMK polling while the peer is in a
  trial, then restore the QMK handshake when it returns. A mixed QMK/ZMK pair is
  not a working ordinary split. Both-ZMK trials need coordinated start and
  independent reset deadlines.

## Recommended implementation sequence

1. Establish the memory/startup/watchdog design and the separate host trial
   protocol, preserving existing updater operations and resident bytes.
2. Implement the smallest Vial trial-launch extension plus host tooling and
   offline tests. Keep maintained GMK70 code in this repository and upstream
   QMK/Vial dependencies unchanged. New ZMK-specific integration belongs under
   `firmware/zmk/`; respect file licenses and ZMK's upstream contribution policy.
3. Build tiny staging-linked return and deliberate-hang programs as future
   proof fixtures. They must be separately labeled and must not be passed to
   the normal updater's install path. Building does not authorize running them.
4. Format edited C; run `make test`, `make check-tools`, and affected supported
   QMK/VIA and Vial normal/updater builds through `scripts/project.sh`. Cover
   corruption, incomplete transfer, identity/address/role checks, native unlock,
   commit exclusion and preservation of application/resident/settings regions.
5. Report the reviewable software result and exact future hardware proof:
   return/hang recovery on each half, right-side return without UART, then both
   halves returning and updater INFO through left USB with the cable stationary.
   Hardware execution is a separate step after authorization.
6. After that recovery path is proven, bring up the WB32 Zephyr/ZMK USB/matrix
   port using it; follow with encoder, native wired split, settings/RGB/power.
   Keep the stock radio unchanged initially. Native ZMK Bluetooth APIs do not
   control the vendor radio protocol, and wireless Studio is not established.

Full ZMK installation in the normal application slot comes later and requires
a Zephyr implementation of the updater USB/relay/flash/authorization services.
The staging-trial design is intended to avoid depending on those services while
their underlying drivers are still being developed. No claim of cable-free
recovery should be made before the independent watchdog path is demonstrated.

## Local implementation continuation

The first software milestone is now tracked in [ZMK-TRIAL-LAUNCHER.md](ZMK-TRIAL-LAUNCHER.md):
a separate opt-in Vial staging receiver, portable launch-intent model, offline
package/packet tooling and bare reset-return/hang fixtures. Device RUN is still
disabled. The next execution milestone is the early-startup trampoline and
independent-watchdog integration/proof in that document; no hardware trial,
firmware installation or resident modification is implied by the offline work.

The [2026-10-27 hardware continuation](ZMK-TRIAL-HARDWARE-VALIDATION.md) installed
and validated the trial receiver on both halves through left USB after explicit
user authorization. Saved native configuration was restored and verified, Vial
was relocked, and resident CRCs stayed unchanged. RUN remains disabled; the next
work is still the startup/watchdog backend, followed by its separate recovery proof.

The [execution continuation](ZMK-TRIAL-EXECUTION.md) now implements the ABI 2
pre-CRT launcher and watchdog backend, with the explicitly requested unlocked
Vial release variant. Consult that document for current validation status.

The final staging-trial checkpoint recorded here was the [first actual right ZMK trial](ZMK-RIGHT-FIRST-TRIAL.md),
following the [left USB trial](ZMK-LEFT-FIRST-TRIAL.md). Right staging was flashed
through left USB; actual ZMK main and matrix readiness preceded the expected
software reset. Watchdog, bare return, bare hang and actual ZMK all returned
automatically to Vial and passed 60 seconds of continuous uptime/feed checks.
Short right relay retries are allowed only within bounded observation gaps;
transport interruptions and reboot/stopped-feed evidence are not waived.

At that 2026-10-27 checkpoint, the left application was unchanged at 95,772 bytes / CRC32 `44883505`. The right
ran the experimental health-capable trial updater, 95,908 bytes / CRC32
`2635584202`, source `5308274`. Both resident CRCs remain `1770733249`; all 263
saved-configuration getters match without writes. Upgrading the right updater
required one left USB reconnect with both halves joined after its warm restart
timed out. That restart cause is unmeasured. No pad bridge or direct right USB
flashing was required during the right qualification.

This historical checkpoint used `codex/zmk-trial-launcher`. Maintained ZMK integration is
under `firmware/zmk/`, with exact dependencies in the root lockfile and explicit
`make setup-zmk`. `make build-zmk` builds the real application without device
operations; `make build-zmk-right-trial` builds the right main-return target.
These are separate bounded trials: physical input, simultaneous ZMK split/radio
and permanent ZMK operation remain future work. At that checkpoint both halves ran Vial;
ZMK stayed in staging and did not launch at power-on. Preserve the user's
preference to avoid right-only physical recovery, and keep the updater warm
restart limitation explicit before any further application upgrade.
