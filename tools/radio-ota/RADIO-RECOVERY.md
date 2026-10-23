# Bluetooth radio recovery

The radio modification must preserve a way to restore stock firmware even when
the modified Bluetooth application cannot connect. OTA is acceptable; USB
programming is preferred. Vial itself stays unchanged. This document records the
first recovery stage; **no experimental radio image is ready for installation
and no working rollback procedure has been demonstrated**.

## Confirmed behavior

The supplied stock radio dump contains two identical `0x25000`-byte snapshots,
SHA256 `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`.
It covers the declared base application region, not the full physical flash,
ROM or separate OTP storage.

The FR8000 SDK manual describes an immutable ROM bootloader. Its OTA procedure
stores the candidate in B, validates it, and on reset copies B over A. The ROM
clears B metadata after installation. This staging scheme does not retain a
known-good application for automatic trial rollback. Preserving the OTA service
in a modified application is insufficient if that application cannot boot.

The actual stock OTA callbacks support storage reads (`0x06`) and memory reads
(`0x08`). Reads of 16/128 bytes defer their data to FF00. Stock's configured
bonding/service addresses include `0x7D000` and `0x7E000`, supporting acquisition
of a candidate 512 KiB window; these configured addresses do not prove capacity.
The SDK documents 128 KiB ROM. Separate OTP trimming/calibration storage is not
included in either window and must not be erased or programmed.

## USB investigation

The current main-MCU firmware uses WB32 UART3 on PC10/PC11 to communicate with
the radio. Radio disassembly configures its application connection as UART1,
PD6 RX / PD7 TX, 115200 baud. No radio reset or power-control output has been
identified in the maintained board code; B1 controls USB power, A5/A8 LEDs, and
B6 is a wake input. They must not be repurposed by guessing.

The SDK's modern `FreqChip_Download V1.3.6.4` DLL has an FR8000-specific ROM
handshake: `freqchip` greeting, host `FR8000OK`, response `ok`, followed by the
programming protocol. This was inspected statically; the vendor executable was
not run. Boot-pin documentation conflicts: the handbook says PA0/PA1 while the
SDK header describes PA2/PA3 or PC6/PC7. Stock's handshake-disable bit is clear,
but these descriptions do not establish a connection through application PD6/PD7.

A USB/UART tunnel is implementable in the main MCU, but a tunnel into the radio's
application UART is not proven to reach the ROM programmer after an application
failure. First inspect the actual ROM, then establish the boot pins and entry
sequence on this board. If the required signals are not connected to the main
MCU, independent recovery may require a physical UART/SWD connection. No such
physical access has been tested.

## Current deliverable

[The read-only recovery collector](README.md) adds two-pass
storage/ROM snapshots and separates private flash backups from the shareable
ROM/report archive. It uses Windows PowerShell 5.1 and requires no new keyboard
firmware. Hardware-free checks cover read encoding/bounds, destructive-command
rejection, exact response lengths, partial-file preservation, hash comparison
and exclusion of private flash data from the analysis archive.

Successful acquisition will establish repeatable snapshots only. The report
retains `complete_recovery_backup = false` and `rollback_verified = false`.
The next required evidence is the actual ROM capture, physical flash layout and
capacity, and an application-independent programming connection. Only after
that path is demonstrated can a radio update/stock-restore procedure satisfy
the requested recovery requirement. Build/test commands never perform device
programming, erase, commit or reset.

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
- The existing [split updater](SPLIT-UPDATER.md) updates the WB32 application on
  each half; it does not update or recover the Bluetooth radio.
