# Local continuation: updater-preserving ZMK development

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
