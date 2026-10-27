# Local continuation: updater-preserving ZMK development

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

The latest checkpoint is the [first actual left ZMK trial](ZMK-LEFT-FIRST-TRIAL.md).
Pinned ZMK was flashed into left staging, enumerated as a USB keyboard, and
returned automatically to Vial. The full launcher also passed watchdog, staging
return and staging hang tests. The left now runs the experimental unlocked trial
updater; the right remains on published unlocked Vial 0.1.7. Both residents and
all 263 saved-configuration getters verify. No right firmware write or physical
recovery was needed during this sequence.

Current sources are on `codex/zmk-trial-launcher`. Maintained ZMK integration is
under `firmware/zmk/`, with exact dependencies in the root lockfile and explicit
`make setup-zmk`. `make build-zmk` builds the real application without device
operations. This is a bounded left USB trial; physical input, right-side launch,
native split/radio and permanent ZMK installation remain future work. Use the
qualified left staging path for further development and preserve the user's
preference to avoid right-only physical recovery.
