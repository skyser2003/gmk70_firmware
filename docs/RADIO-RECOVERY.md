# Bluetooth radio recovery

The radio modification must preserve a way to restore stock firmware even when
the modified Bluetooth application cannot connect. OTA is acceptable; USB
programming is preferred. Vial itself stays unchanged. This document records the
recovery investigation; **no experimental radio image is ready for installation
and no working persistent-update rollback procedure has been demonstrated**.

The later 2026-10-23 RAM-only trial demonstrated native Bluetooth HID and an
unchanged Web Vial keymap readout without changing stock radio flash. Its sources,
verification and reconnect limitations are recorded in the
[radio payload README](../firmware/qmk-common/radio/ram-vial-trial/README.md).
The subsequent [offline persistence analysis](WEB-VIAL-PERSISTENCE.md) compares
a stock-radio flash extension with MCU-driven RAM loading and records their
remaining initialization and recovery requirements; neither route was installed.
The offline-prototype statements below describe the earlier recovery checkpoint;
they do not supersede that later volatile trial or establish persistent rollback.

## Confirmed behavior

The supplied stock radio dump contains two identical `0x25000`-byte snapshots,
SHA256 `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`.
It covers the declared base application region, not the full physical flash,
ROM or separate OTP storage.

The actual captured FR8000 ROM stages an OTA candidate in B, validates it, then
copies B over A on reset. It clears B metadata **before running the new
application**. This is not automatic trial rollback: the ROM cannot distinguish
a working image from a CRC-correct application that fails to connect. Preserving
the OTA service in a modified application is insufficient if it cannot boot.

For raw updates, the ROM retains staging while copying and validates the copied
body and header before clearing B metadata. This supports restarting an
interrupted copy when staging survives; it does not restore the previous image.
A pending B record with a bad CRC causes a reset rather than fallback to old A.
These are instruction-level findings, not physical power-interruption tests.

The actual stock OTA callbacks support storage reads (`0x06`) and memory reads
(`0x08`). Reads of 16/128 bytes defer their data to FF00. Stock's configured
bonding/service addresses include `0x7D000` and `0x7E000`, supporting acquisition
of a candidate 512 KiB window; these configured addresses do not prove capacity.
The SDK documents 128 KiB ROM. Separate OTP trimming/calibration storage is not
included in either window and must not be erased or programmed.

## Capture received on 2026-10-22

The `gmk70-radio-recovery-20261022-165557-381-analysis.zip` acquisition completed
with 10,242 read requests and 10,242 responses. The archive contains both ROM
passes; their lengths, hashes and byte equality were independently verified here.

| Evidence | Result |
| --- | --- |
| ROM, two 128 KiB passes | Both SHA256 `a2214bdee4ec073d5800df66025f65f77c43433385f5ed51cdc2bfcff64f0908` |
| Candidate flash window, two 512 KiB passes | Reported SHA256 `db2924effc06cae742cb6436b79db8270f245f04ea6899fd26cec9d78e3c22a8` |
| Application prefix of flash capture | Reported SHA256 matches the original stock `0x25000`-byte image above |
| Uploaded 128-byte header | Independently matches the original stock image |
| Recovery certification | `complete_recovery_backup = false`, `rollback_verified = false` |

The full flash binaries correctly remain in the private ZIP; only their report
was available for this analysis. The repeated 512 KiB window does not establish
physical capacity or rule out address aliases. Keep that private ZIP: it may
contain bonding keys and is not a general-purpose release image.

The report identifies a locally adjusted collector, `winrt-projections-1`, with
source hashes different from the earlier distributed scripts. It validates this
device capture, not an unchanged copy of those earlier Windows WinRT scripts.

## USB investigation

The current main-MCU firmware uses WB32 UART3 on PC10/PC11 to communicate with
the radio. Radio disassembly configures its application connection as UART1,
PD6 RX / PD7 TX, 115200 baud. No radio reset or power-control output has been
identified in the maintained board code; B1 controls USB power, A5/A8 LEDs, and
B6 is a wake input. They must not be repurposed by guessing.

The captured ROM resolves the SDK's conflicting boot-pin descriptions:

| Connection | Actual peripheral and pins | Baud |
| --- | --- | --- |
| Native ROM programmer | UART0, **PA0 RX / PA1 TX** | 115200, 8N1 |
| Stock radio application to main MCU | UART1, **PD6 RX / PD7 TX** | 115200 |

`app_boot_handshake_init` at ROM `0x22AC` ignores the incoming header pointer,
sets PA0/PA1 mux function 4 and initializes UART0 at `0x50050000`. The alternate
PC6/PC7 flag described in an SDK comment is not implemented in this ROM routine.
PD6/PD7 function 4 selects UART0 flow-control signals, not its RX/TX data pins.
Changing that mux would therefore not solve the mismatch.

The ROM sends `freqchip`, expects `FR8000OK`, replies `ok`, then enters its
programming command loop. Stock's receive opportunity is approximately 30 ms;
an invalid A header uses approximately 100 ms. It skips the handshake only when
A is marked valid, timeout is `0xFE`, and system-option bit 9 is set. These
timings are code-derived and have not been measured on hardware.

The modern `FreqChip_Download V1.3.6.4` DLL has a matching FR8000 handshake. It
was inspected statically; the vendor executable was not run. The native ROM
protocol also has a read-only JEDEC-ID command (`0x23`, response `0x24`). This is
a different command namespace from the Bluetooth OTA service.

A plain USB/UART relay over the existing application connection **cannot reach
this native ROM programmer**. No additional PCB route to PA0/PA1 has been
established. USB recovery could instead use a new radio first-stage loader on
UART1, but that would need its own protected entry, reset path and demonstrated
stock restore. It would not recover corruption of that loader or its header.
Physical UART/SWD access remains another possibility; neither route is tested.

Seven bounded CPU simulations of the actual ROM verified pin selection for
three header cases, timeout polling for two cases, and the JEDEC command for two
synthetic IDs. UART/SPI I/O and delay are simulated. This is not an official
FR8000 emulator, a physical flash identification, or a recovery test.

## RAM-only trial investigation

A first experiment can avoid replacing the stock application by loading only
volatile RAM code through stock OTA's memory-write operation. Stock startup
reloads the original RAM code from flash, so a power cycle would remove such a
patch if persistent flash remains unchanged. This is the intended recovery
property of a possible trial, not a demonstrated installation/rollback result.

The stock OTA reassembly buffer is unsuitable for executable staging: its timer
and disconnect handler can free it. A bounded offline bootstrap instead uses
the statically owned RAM copy of the OTA commit CRC validator and requests
owned heap storage through the original allocator. The original validator entry
returns failure in the experiment; a trial must not perform flash OTA commits.
This location and its size were checked against the stock application's code.

The 148-byte bootstrap passed eleven bounded Cortex-M CPU simulations, including
insufficient heap, disconnect, and preserving existing interrupt masks. Stock
startup uses five of eight user-service slots, leaving space to investigate a
second HID service without deleting the existing one. Live occupancy still
needs to be checked.

The bootstrap is still an offline control-flow prototype. Live allocation,
service registration, Bluetooth sleep/reconnect behavior, and unchanged Windows
Vial discovery have not been demonstrated. No RAM writer or installable radio
image is supplied by this recovery stage. Native Vial over Bluetooth remains
unproven; it has not been ruled out merely by this recovery investigation.

## Current deliverable

[The read-only recovery collector](../tools/radio-ota/README.md) adds two-pass
storage/ROM snapshots and separates private flash backups from the shareable
ROM/report archive. It uses Windows PowerShell 5.1 and requires no new keyboard
firmware. Hardware-free checks cover read encoding/bounds, destructive-command
rejection, exact response lengths, partial-file preservation, hash comparison
and exclusion of private flash data from the analysis archive.

The capture establishes repeatable snapshots and resolves ROM behavior. For a
persistent update, the remaining recovery evidence is physical flash layout and
capacity plus an application-independent programming connection that can read
back stock and restore it. A private backup alone does not establish that path.
The RAM-only approach can be investigated before replacing stock flash, but
still needs a bounded installer and hardware validation. Build/test commands
never perform device programming, erase, commit or reset.

## Sources

- FR8000 SDK mirror, exact commit
  [`9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6`](https://github.com/shunxingzhe/fr8000/tree/9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6):
  `docs/FR8000 SDK 用户手册 V1.1.pdf`, sections 1.2–1.5 and 6;
  `docs/FR8000 Specification V1.2.0.pdf`, memory description;
  `components/modules/platform/include/jump_table.h`;
  `components/ble/profiles/ble_ota/ota.c`;
  `tools/freqchip_Download_tool/FreqChip_Download V1.3.6.4.zip`.
- Stock image disassembly: OTA callback `0x10002FA8`, FF00 reader `0x10002F78`,
  application UART initialization `0x10017374`, UART sender `0x10017474`.
- Actual captured ROM: boot `0x1F70`, update `0x20D8`, handshake initialization
  `0x22AC`, host handshake `0x22D4`, serial receive `0x26CC`, command processor
  `0x2398`, JEDEC-ID read `0x4A74`, GPIO mux `0x1C4BC`.
- The existing [split updater](SPLIT-UPDATER.md) updates the WB32 application on
  each half; it does not update or recover the Bluetooth radio.
