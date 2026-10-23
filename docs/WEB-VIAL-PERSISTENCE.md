# Persistent Bluetooth Web Vial: offline route analysis

Analysis date: 2026-10-23. Scope: unchanged official Web Vial, firmware-only
operation, and persistence across a real radio power cycle. Desktop Vial is
outside this investigation. **No keyboard access, flashing, RAM upload, reset,
pairing operation, installer, or flashable image was used or produced.**
The only maintained changes from this analysis are documentation. Publishing
these notes is separate from developing or installing persistent firmware.

## Required architecture and dongle compatibility

The user requires minimum radio-firmware changes, with Vial and keyboard feature
logic implemented in the main MCU. Keep keymaps, layers, macros, combos, Vial
command interpretation, keyboard definition, native unlock and settings storage
in the existing MCU implementation. Radio additions should provide only the BLE
HID transport: service/report descriptors, connection/subscription handling,
bounded report buffers and exact 32-byte UART forwarding. Descriptor/service
handling and Bluetooth transport state necessarily remain in the radio with
the existing hardware interface.

Support must be permanent: the radio must expose Web Vial after a true power
cycle without a PC reinstalling the RAM patch. Preserve all stock Bluetooth
features, including typing/media/mouse reports, indicators, battery/device
information, pairing profiles, reconnect, sleep/wake and existing configuration
and maintenance services. Compatibility is an acceptance requirement; the RAM
readout trial does not certify these regressions or persistent operation.

The current RAM payload already follows this functional separation. It treats
Vial request/reply payloads as opaque bytes and invokes the stock UART32 and BLE
notification helpers; it contains no keymap engine or Vial-command parser.
Its allocation/gateway/refresh mechanisms are temporary installation machinery,
not additional keyboard feature logic, and are not the intended persistent
architecture.

**The 2.4 GHz dongle mode must retain its current behavior.** Keep the original
dongle protocol, pairing, identity and report paths, and preserve existing
UART/FFF0 behavior when adding BLE forwarding. Any persistent adapter must
isolate its BLE state and account for shared heap, timers, power and mode
transitions. The trial did not intentionally replace dongle logic, but dongle
operation and Bluetooth/dongle transitions have not been regression-tested;
unaffected dongle behavior is a requirement, not a verified result.

This requirement update does not authorize implementation, device access or
flashing. The earlier evidence ZIP is a snapshot of the preceding route analysis;
this maintained document records the current constraints.

## Assessment

| Route | Evidence | Assessment |
| --- | --- | --- |
| Modify the stock radio application in flash | The existing radio ran the RAM extension successfully; captured startup has a service-registration call and the image has a possible extension area. | The strongest candidate for permanent support. A bounded stock-image extension needs initialization/data placement, normal OTA preservation and verified independent recovery. No persistent image is validated. |
| Change only the main MCU; keep stock radio flash | The actual stock UART parser exposes reports/settings/control commands. No legitimate RAM-load, readback, allocation, execution or loader-entry command was identified. | No supported bootstrap was established. Storing the payload in MCU flash does not make the stock radio accept or execute it. |
| Radio UART loader plus MCU-held RAM payload | A new radio-side receiver could supply the missing UART operation. | A possible hybrid, requiring an initial radio flash change and two firmware components. It does not meet a strict MCU-only/unchanged-radio-flash condition. |

The earlier suggestion to investigate UART first was a way to preserve stock
radio flash while checking feasibility. The actual dispatcher audit now makes
radio-image integration the better-supported candidate. Lack of a loader in
these bounded paths is not proof that no undocumented boot mode or additional
PCB connection can exist.

## Inputs and demonstrated baseline

| Input | Size / SHA256 |
| --- | --- |
| Captured stock radio application | `0x25000` bytes; `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c` |
| Captured radio ROM | `0x20000` bytes; `a2214bdee4ec073d5800df66025f65f77c43433385f5ed51cdc2bfcff64f0908` |
| Existing compiled RAM payload | 1,168 bytes; `f1ea19e63d0ca5cb2f64c62756396220c62c91611248abaddda69daa9fc5b599` |

Commit `a7f58c3` records the working RAM-trial sources. The trial added a second
BLE HID service, usage `FF60/61`, with unnumbered 32-byte input/output reports.
The unchanged official Web Vial loaded the actual GMK70 keymap and native
extension tabs. Commands travel through the stock UART32 helpers to the existing
main-MCU Vial implementation. The stock typing HID, FFF0 configuration service,
and OTA service were retained.

That experiment establishes compatibility of the transport and keymap readout.
It does not establish automatic cold-start initialization, reliable reconnect,
configuration setters, another host/platform, or persistent recovery. The
previous full flash comparison after the successful trial matched the private
stock backup; this analysis did not recapture flash. See the
[RAM trial status](../firmware/qmk-common/radio/ram-vial-trial/README.md).

## Route A: persistent stock-radio extension

### Startup and storage feasibility

Captured application entry `0x10002D80` initializes data, clears BSS and copies
the stock executable RAM region. The copy maps flash offsets
`[0x1907C, 0x1BD54)` to radio RAM `[0x11001000, 0x11003CD8)`.
Initialized data follows at flash `[0x1BD54, 0x1C5FC)`.

The stock startup chain includes a call at `0x1001347A` to `0x10013644`, which
registers five original user services. The captured user-service array has eight
slots; the prior live trial successfully used a sixth. A wrapper around this
startup call is a concrete candidate for adding the new service while retaining
the original initialization. It is not a validated patch point: ordering,
thread/interrupt context, asynchronous service completion and failure handling
still require verification.

The image's suffix `[0x1CE00, 0x25000)` contains 33,280 bytes of `FF`. It is much
larger than the current payload. A bounded scan of application text, relocated
RAM code and ROM found no direct branch, literal-pointer or decoded MOVW/MOVT
reference into this suffix. **Neither erased-looking bytes nor that scan proves
the region is unused:** computed accesses, reserved data, image checks and
operating modes must still be audited. The preceding zero-filled range must not
be treated as free space by association.

### Why the live RAM binary cannot simply be saved into flash

- The current ELF links code, tables and mutable state to a heap allocation
  obtained for one session. The observed allocation address is not a permanent
  reservation. Persistent execution needs an explicit code/data layout and
  initialization, or a runtime allocation/relocation design.
- Of the existing payload, 1,104 bytes are text/constant tables and 64 bytes are
  mutable service definition/state. Flash-resident code and constants are a
  plausible arrangement, but state needs owned writable RAM. Allocating more
  heap storage cannot silently overlap stock stack, BSS, BLE control arrays or
  live allocations.
- The live route gateway occupies the original OTA CRC validator's 178-byte RAM
  range, `[0x11001AF4, 0x11001BA6)`, and makes that validator return failure.
  Baking this trial arrangement into flash would compromise normal radio OTA
  updates. A persistent design needs a separate initialization/reply wrapper
  and must preserve the original CRC/update path.
- A separate reply-wrapper candidate exists at the stock absolute veneer
  `0x1100115E`, loaded from file offset `0x191DA`. Its caller at `0x1100196E`
  passes the reply payload/length to stock FFF0 helper `0x10002698`. Any wrapper
  must retain that helper's behavior and ABI. This candidate has not been
  modified or validated. Widening the current two-byte dispatch branch in place
  would overwrite adjacent dispatch entries.

Extending the captured stock application preserves more known behavior than
replacing it with a generic vendor keyboard demo. Historical SDK compilation
proved a toolchain could build those examples; it did not supply GMK70's
proprietary 2.4 GHz protocol, board integration, mode behavior or pairing glue.
No generic demo is a substitute for a validated GMK70 image.

### Stock OTA and recovery constraints

The captured A header declares image size `0x25000`. Stock's staging-address
getter at `0x1000312A` derives the base from the **installed A size**, adding
`0x2000`, giving `0x27000` for this image. Commit code around
`0x10003204..0x1000324C` reads the staged header there and sets body source to
`0x29000`. With the same declared size, the body length is `0x23000` and staging
ends at `0x4C000`. The ROM copies it to `[0x2000, 0x25000)` and separately handles
A header and B metadata. These are observed address calculations, not permission
to use every intervening address or proof of physical capacity.

Keeping the declared application size unchanged is an attractive constraint if
the extension area is proven available. Enlarging an image cannot be treated as
changing a single header: staging arithmetic, page alignment, overlap, code
placement, settings and actual flash capacity all need revalidation. Original
pairing/identity/configuration regions and OTP/calibration storage must retain
their established ownership.

The stock OTA transfer check and ROM image checks also have different coverage
and CRC conventions. The transfer check starts `0x100` bytes after the staged
image base; the ROM separately checks the first `0x60` header bytes and the
staged/copied body. A generic host CRC calculation cannot be assumed compatible
without checking the captured routines. No candidate checksums or update record
were generated here.

ROM A/B handling is an update-copy mechanism, **not automatic functional
rollback**. After a valid copy, it clears B metadata before launching the new
application. A CRC-correct application with broken BLE therefore does not cause
stock firmware to return automatically. A pending B record with an invalid
candidate can instead trigger repeated resets.

The independent ROM programmer uses UART0 PA0/PA1. The established main-MCU link
uses radio UART1 PD6/PD7. A USB relay through the existing connection cannot by
itself reach that ROM programmer. A verified physical ROM/SWD/flash access route,
or a separately engineered and verified recovery component, is still required.
No working independent restore path has been demonstrated. A backup file and a
surviving application OTA service do not establish recovery after that
application fails. Details remain in [RADIO-RECOVERY.md](RADIO-RECOVERY.md).

## Route B: automatic upload from main MCU

### What the stock UART actually accepts

The radio UART1 receive ISR at `0x10016CE0` calls the relocated byte parser
`0x110014D4` through veneer `0x100025C2`. Checksum-valid packets enter consumer
`0x1100161C`, whose bounded dispatch covers command bytes `A1..B4`.

| Command family | Captured behavior relevant to loading |
| --- | --- |
| Keyboard, NKRO, consumer, system, mouse | Fixed report senders and buffers. |
| Device name, manufacturer, product, VID/PID | Copies to fixed settings fields and invokes fixed settings callbacks. No destination-address argument. |
| `A6 60` reserved debug / `A6 61` reserved sleep | No subtype-specific action in this capture. |
| Other `A6` values | Fixed mode, pairing, battery, charging or configuration-byte actions; no general memory loader identified. |
| `AF` raw report | Sends a GATT notification to the host; it does not dispatch embedded bytes as OTA commands. |
| `B3`, `B4` | Fixed RF helpers / synthetic mouse-cycle behavior. |

In particular, `AF` goes from `0x11001842` to `0x1100196A`, then helper
`0x10002698`, notification wrapper `0x1001361C` and `gatt_notification` at
`0x1000CC4C`. Placing BLE OTA `WRITE_MEM` bytes in that report does not turn it
into a radio RAM writer. The reverse UART32 helper used by the RAM service sends
requests toward the main MCU and also provides no radio loading operation.

The live UART execution gateway only runs **after** the PC has installed it
through BLE OTA RAM writes. Using that installed gateway is not a solution to
its own cold-start installation. Malformed packet memory-corruption behavior
is outside a supported loading protocol and is not proposed as a route.

### MCU resource and lifecycle requirements

The existing Vial-updater ELF has 31,620 bytes between its flash-free symbol and
application end, and a 3,232-byte heap span. Its SHA256 is
`3e9ca33fece9afb2e079c48c42da16f56703800d1ec19f1f305ddc6e0bf68b98`.
This is an indicative existing artifact, not a rebuild of current source or a
claim about the installed image. A 1,168-byte payload could conceptually stay
in MCU flash and be streamed in small pieces. Storage size is therefore not the
first demonstrated blocker; the missing radio-side loader is.

If a supported loading path is found, it must also handle actual radio cold
starts independently of main-MCU resets, detect an already active compatible
patch, obtain owned RAM, initialize/relocate it correctly, and leave stock typing
usable if loading fails. Main-MCU reboot and USB unplug are not dependable
evidence of radio power loss: a prior trial retained the exact RAM marker/code
through such a sequence. No main-MCU-controlled radio reset/power line has been
established.

Adding a UART1 receiver to radio flash could meet these transport requirements,
but makes the design hybrid. A loader intended also for recovery must run before
a bad application, have a reliable entry/reset trigger, and survive the update
write ranges. An ordinary application command handler does not provide those
properties.

## Updater portability

The present diagnostic/RAM transport scripts are source in this repository but
depend on Windows PowerShell 5.1 and WinRT. They are not a complete persistent
radio flasher. The radio's GATT OTA packet protocol is not inherently tied to
Windows, so a separate open-source updater could use cross-platform BLE APIs
on Windows, Linux and macOS. Python/Bleak is a candidate, not an implemented or
tested choice for this radio. Each platform needs its own transport validation.

Normal installation could use the stock Bluetooth OTA path after returning the
radio to verified stock runtime with its original validator intact. The existing
USB QMK/Vial updater targets the WB32 MCUs, not the radio. Application-independent
recovery may need external hardware; neither a portable updater nor retained
BLE OTA establishes that recovery. Update tools remain separate from build/test
commands and from normal Web Vial operation; no host runtime bridge is proposed.

## Shared work before either route is usable

1. Restore notification state correctly for each bonded host and connection.
   The trial clears its local subscription flag on link loss/create; Windows
   may retain its enabled subscription and omit a new CCCD write. Offline replay
   reproduces the resulting request/reply suppression. Rewriting one observed
   host's RAM bytes was a trial initialization, not an automatic solution.
2. Confirm service creation and define registration failure behavior. Current
   `status=1` means queued, not asynchronous success. An initial capacity failure
   sets a status that also suppresses later retries, even if capacity recovers;
   the offline fixture reproduced this behavior.
3. Preserve the exact 32-byte report/UART contract, native main-MCU Vial physical
   unlock rules and one-configuration-client limit. The stock response stream
   has no transaction IDs, so late replies cannot safely be assigned invented
   request identities.
4. Validate idle wake, cold boot, reconnect, host/slot changes and real keymap
   writes with saved settings and native unlock in a later authorized trial.
   Retain working USB typing/configuration and stock radio modes.
5. Validate recovery and complete stock readback/restore before relying on a
   persistent experimental image. No such device operations are authorized by
   this analysis request.

## Verification and remaining analysis

Fresh offline checks in this investigation:

- **1,285 UART fixtures:** 256 byte-parser starts; 512 consumer dispatch cases;
  512 device-control cases; five mouse-state cases, plus canonical complete
  packets for accepted framing bytes. Subordinate APIs are recorded/stubbed;
  these checks do not prove every subordinate function or undocumented mode.
- **Seven ROM fixtures:** three programmer pin/argument cases, two timeout cases
  and two synthetic JEDEC replies. Peripheral I/O is simulated; no physical
  flash identification or restore is established.
- **22 existing compiled-payload checks**, plus **four automatic-readiness
  cases** confirming initial subscription, missing subscription after reconnect,
  recovery after an explicit new subscription, and sticky registration failure.
  Passing readiness cases document limitations; they do not certify fixes.
- Read-only ELF sizing, stock startup/staging/reference inspection, and source
  comparisons confirming the maintained radio C/assembly/build files still match
  the committed trial baseline.

Further offline evidence can refine suffix ownership, startup ordering,
state/constant placement and separate reply routing while preserving original
CRC and settings behavior. Additional UART documentation or a different proven
PCB connection could change the MCU-only assessment. Actual pin access, flash
capacity, restoration and cold-start timing require a separate hardware session;
they cannot be certified from these captures.

Local evidence is under `.work/research/radio/persistence-20261023/`.
The shareable report bundle is
`artifacts/analysis/web-vial-persistence-2026-10-23/gmk70-web-vial-persistence-20261023-analysis.zip`.
It excludes raw flash/RAM, pairing keys, HID paths, ownership markers, executables
and firmware images. No new dependency or vendor source was imported.
