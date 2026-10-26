# Keeping the updater during ZMK development

Source investigation, 2026-10-26. This is a proposed development workflow,
not an implemented trial loader or a hardware-tested recovery path. No device
operation, ZMK build or resident modification was performed.

## Finding

The existing resident installer can be retained. Routine development without
moving the left USB cable is plausible, but keeping that installer alone does
not provide recovery from broken ZMK drivers. The USB receiver and right-half
relay currently run inside the main Vial/QMK application.

The preferred early-development design is to retain working Vial/updater in
the application slot and **execute a separately linked ZMK trial from the staging
slot**. A short hardware-watchdog deadline would return execution to Vial on
reset. The installed resident would remain unchanged. This design needs new
application/host support and a hardware proof before using it for ZMK.

## What the current code establishes

- [boot.c](../firmware/qmk-common/keyboards/zuoya/gmk70/update/boot.c) processes an
  install record and starts the application at `0x08002000`. It contains no USB
  update server or UART split receiver.
- [qmk_update.c](../firmware/qmk-common/keyboards/zuoya/gmk70/update/qmk_update.c)
  implements those receivers using QMK transactions, ChibiOS locking, USB state
  and housekeeping. ZMK's normal wired split is a different protocol.
- [image.c](../firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c) validates
  staged images as applications linked for `0x08002000`; ordinary INSTALL copies
  them over the current application. It has no boot-health confirmation or
  automatic old-image rollback. A corrupt transfer and a CRC-valid program that
  hangs after boot are different cases.
- [update.py](../tools/firmware-update/update.py) likewise accepts only the
  current application link address/profiles and waits for the new application's
  updater to respond. A staging-linked ZMK image is deliberately rejected.

An offline C fixture linked the actual maintained `image.c` and `protocol.c`,
with flash access replaced by a bounded RAM array. Compiled with warnings as
errors and ASan/UBSan, it confirmed:

1. A staging-linked reset vector is rejected by the current SEAL operation.
2. Staged bytes without a valid install commit do not replace the application.
3. Ordinary INSTALL replaces the previous application; it does not preserve an
   automatically bootable fallback copy.

This checks current protocol/image behavior only. It does not execute WB32
instructions, a watchdog, USB, split UART, Zephyr or the proposed trial path.

## Candidate layout with the resident unchanged

The [existing layout](SPLIT-UPDATER.md#flash-layout-and-interruption-handling)
already has enough separate address space for this arrangement on the
previously identified 256 KiB parts:

| Region | Proposed development use |
| --- | --- |
| `0x08000000–0x08001FFF` | Existing 8 KiB resident, unchanged |
| `0x08002000–0x0801FFFF` | Working Vial/updater plus a trial-launch extension; maximum 120 KiB |
| `0x08020000–0x0803DFFF` | Separately linked ZMK trial; maximum 120 KiB |
| `0x0803E000–0x0803FEFF` | Existing reserved range; no allocation assumed here |
| `0x0803FF00–0x0803FFFF` | Ordinary install commit; must not contain a trial install record |

The current local Vial-updater artifact is 92,048 application bytes, leaving
30,832 bytes in its slot. The launch extension has not been built or sized.
No ZMK fit has been established. The images run sequentially and can reuse SRAM;
this does not require two kernels to fit in RAM simultaneously.

Zephyr's selected Cortex-M
[linker script](https://github.com/zmkfirmware/zephyr/blob/10ba6d0cb38bc3d258775d27982f707599320085/include/zephyr/arch/arm/cortex_m/scripts/linker.ld)
supports a bounded flash load offset. A future board configuration could target
the staging address, with the raw vector table there and reset/code/data-load
addresses linked accordingly. Moving an ordinary application binary to that
address would not relocate it. Upper-flash execution, WB32 startup and the actual
ELF/vector layout still need validation.

## Upload, trial and return

1. Deliver a trial-capable **Vial application** to both halves through the
   existing updater. This can preserve the resident bytes and ABI; it does not
   inherently require another per-half DFU installation.
2. Upload a distinct trial package through left USB, relaying to the right when
   selected. A new trial validator checks its staging-linked vectors, length,
   CRC and compatible RAM layout. Keep normal installation and trial sessions
   distinct so a trial can never be passed to ordinary INSTALL.
3. Check that no ordinary installation is pending. Leave the normal commit
   record invalid, preserve the Vial application and arm a one-shot trial.
4. Start the independent watchdog, then perform a controlled handoff to the
   trial. An early-startup trampoline or fully audited peripheral/CPU handoff
   must handle VTOR, MSP/PSP, CONTROL/masks, pending interrupts, SysTick, USB,
   clocks and cache. This is not an ordinary C function call between kernels.
5. For the first tests, the trial does not refresh the watchdog. Its expiry
   resets the MCU even if the test stalls. Clear/consume the one-shot launch
   state before entering the trial, so reset cannot repeat it indefinitely.
6. With no install commit, the unchanged resident starts Vial again. The host
   reopens its USB interface and verifies application/resident identity before
   another upload. Left-side trials may cause automatic USB re-enumeration;
   the physical cable can stay connected.

The [WB32 reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf),
sections 7.1 and 18, documents an independent watchdog reset. Its table gives
nominal maximum timeout 32.768 seconds at 32 kHz. Actual timeout, clock/gate
behavior during trial startup, debug freeze and reset recovery need measurement.
The current pinned WB32 ChibiOS platform does not provide a ready-to-use IWDG
driver in its driver list; a small verified integration is still required.

Reserve any launch/result RAM explicitly in both linkers, separately from the
resident's boot magic at `0x20006FFC`. Initial trials should use conservative
RAM bounds and disable settings/flash programming and deep sleep until their
own integration is validated. The trial is cooperative firmware, not an
isolation boundary against arbitrary writes to flash or reset controls.

## Both halves and later full installation

Start with the right half as a short hardware trial while the left remains
Vial. Pause ordinary QMK split polling while that half runs the other protocol;
after its reset, re-establish the normal QMK handshake and updater response.
This arrangement permits hardware tests, not a fully mixed QMK/ZMK keyboard.

When both trials run ZMK, the link can use ZMK's native wired split. Each half
needs its own reset deadline and a predictable return to Vial. A hung right
trial must not depend on a UART command to reset, since UART itself may be under
test. Test each half's return independently before coordinating both trials.

For eventual normal installation at `0x08002000`, port the application-side
updater to Zephyr: USB protocol, split relay/receiver, bounded flash backend,
housekeeping/reset behavior, identity checks, and an intentional physical write
authorization mechanism. The old Vial unlock logic does not exist in ZMK.
During a mixed-framework transition, retain a compatible maintenance transport
or change the bridge deliberately; a shared baud rate alone is insufficient.

Rewriting the resident into a standalone USB/UART recovery monitor is another
design, but it would require a new resident image, layout/size proof and a
separate installation/recovery procedure. It is not necessary to try the
staging-slot approach and would undermine the goal of retaining the installed
resident for the first experiments.

## Proof required before claiming cable-free iteration

- Software checks reject incorrect link addresses, oversized images, malformed
  vectors, interrupted uploads and accidental trial-to-INSTALL requests. Assert
  byte-for-byte preservation of the resident and current application.
- A tiny returning trial and a deliberate non-returning loop both recover via
  reset to the expected Vial image. Verify watchdog return before any ZMK test.
- Exercise trials with USB/UART interrupts disabled or uninitialized, then
  clock and startup failures. Verify the right returns without a working link.
- Verify both halves' updater identity through left USB after each trial, with
  the cable left in place; later validate automatic left USB re-enumeration.
- Verify no persistent trial loop, settings changes, stale install commit or
  repeated-boot side effect, including ordinary power loss during upload.

Until these are demonstrated, no guarantee of recovery without physical access
is established. The recommended first implementation milestone is the trial
launcher/watchdog return, followed by the WB32/ZMK port through that path.
