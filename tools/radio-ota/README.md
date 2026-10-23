# GMK70 Bluetooth radio dump

The separate physical [RAM trial tool](RAM-TRIAL.md) performs a narrowly scoped
volatile canary experiment using `ram-trial.ps1`. The collectors described below
perform storage/ROM reads.

The subsequent 2026-10-23 RAM-only native Web Vial result and its remaining
reconnect/desktop limitations are recorded in the
[radio payload README](../../firmware/qmk-common/radio/ram-vial-trial/README.md).
The acquisition instructions below describe the imported collector; extended
flash and ROM captures were subsequently verified as documented in
[the repository recovery findings](../../docs/RADIO-RECOVERY.md).

This local copy imports `gmk70-radio-recovery.zip` (SHA-256
`0555488f6cf885e7b0bf10814560f0e9a750a909f8f3ef010be71ddbd9c6516e`)
and includes the verified Windows PowerShell WinRT collection and buffer fixes.
The collector keeps its timeout behavior: it ends acquisition rather than
retrying a request while an earlier WinRT operation could still be pending.

## Recovery preparation

Before testing modified radio firmware, use **`run-recovery.cmd`** instead of the
ordinary dumper. Keep the keyboard connected in **Bluetooth mode** and press and
release Shift occasionally during the capture so the main firmware does not sleep.
The launcher explicitly uses built-in Windows PowerShell 5.1; no MSVC or extra
Python packages are needed. This does not flash either processor.

Recovery mode captures two matching passes of each requested region:

- A **candidate 512 KiB storage window**, including the application and the
  locations stock configures for Bluetooth bonding/service data.
- The documented **128 KiB ROM window**, using bounded `READ_MEM` requests. This
  is needed to inspect the actual bootloader and resolve conflicting UART pin
  documentation.

The script produces two ZIPs:

- **`*-private.zip`** contains the flash snapshots. Keep it on your PC: it may
  contain Bluetooth bonding keys. This is backup evidence, not a validated
  restoration package.
- **`*-analysis.zip`** contains ROM, the boot header and the acquisition report.
  Send this archive for analysis. Flash snapshots are excluded using a fixed
  file allowlist.

The extended read paths still need a run on this keyboard. The report always
sets `complete_recovery_backup = false` and `rollback_verified = false`:
repeatable reads do not establish physical flash capacity, OTP calibration
backup or a working programming connection. A timeout ends the session rather
than issuing another request while a WinRT operation might still be pending.
Interrupted runs retain partial files and produce analysis evidence when possible.

Stock FR8000 OTA stages an image and then **copies it over the running image**;
it is not an automatic trial/rollback mechanism. USB programming is preferred,
but the stock application's UART pins differ from the SDK's boot-pin descriptions,
and no main-MCU radio reset/power control has been identified. A working USB
recovery path is not yet established. See [radio recovery findings](RADIO-RECOVERY.md).

## Ordinary application snapshot

Read the radio's declared base image region (`0x00000` through `0x24FFF`) twice
through its existing Freqchip OTA service. No MSVC, Python packages, administrator
rights, or firmware update are needed. Windows PowerShell 5.1 is built into Windows.

1. Extract all files together to a writable folder on your Windows PC.
2. Keep **GMK70-2 connected to Windows in Bluetooth mode** and awake. Close
   Bluetooth LE Explorer and other Bluetooth inspection programs.
3. Double-click **`run-dump.cmd`**. It selects Windows PowerShell 5.1 explicitly.
   Version 2 also relaunches itself in the built-in 5.1 host if started from
   PowerShell 7 on Windows, preserving its arguments and the child's exit code.
4. Wait for both passes. Keep the keyboard awake if needed; reads may take several
   minutes. Send the resulting **`gmk70-radio-dump-*.zip`**.

The ZIP is produced on a handled failure too. Send it even if the script reports
an error. An interrupted run may leave an output folder and `.partial` file;
send those if no ZIP was produced.

The default radio address, `c7:02:53:aa:76:69`, comes from this device's GMK70-2
Bluetooth screenshot. To use another address or force smaller blocks, open
Windows PowerShell in this folder and run:

```powershell
.\dump-radio.ps1 -BluetoothAddress 'c7:02:53:aa:76:69' -SmallChunks
```

The launcher uses a process-only execution policy; it does not change the global
PowerShell policy.

## Acquisition and limits

- Ordinary mode uses OTA **`READ_DATA` (opcode `0x06`)**. Recovery mode also uses
  **`READ_MEM` (`0x08`)**, restricted to ROM `[0, 0x20000)`. Both are sent to FF01
  with GATT write responses. No erase, flash/RAM programming, reboot, settings,
  host-switch, or pairing commands are implemented. The sender rejects other
  opcodes, malformed headers and reads outside the fixed windows.
- In the public SDK, requests larger than ten bytes queue their data for **FF00**.
  This script consumes that value using uncached reads, so notification callbacks
  are unnecessary. Two 148 KiB application passes using 128-byte storage reads
  were captured successfully on Windows PowerShell 5.1. Extended flash/ROM reads
  remain untested on hardware.
- A 16-byte preflight checks the observed `image_size = 0x25000` and reserved word.
  The script tries a 128-byte read and checks its prefix against the small read.
  If that read fails, it uses 16-byte blocks that fit the minimum BLE ATT MTU.
- Every block must have the exact requested length. The report distinguishes
  GATT write responses from OTA notification acknowledgements, which are not
  captured. Two full passes must have identical SHA-256 hashes and the same header
  as the preflight. Partial files are never presented as complete images.
- Output includes two binaries, the first 128 header bytes, a JSON report and a
  progress/error log. Existing output folders and dump files are not overwritten.
- This is a **base image-region snapshot**, not a complete recovery backup. Total
  flash capacity, active-bank mapping and storage outside this region are not yet
  established. Repeatable data alone does not prove arbitrary reads, authentic
  physical flash mapping, or acceptance of modified firmware.
- The radio may start an OTA session timer during reads and briefly disconnect
  afterward. The script does not send an explicit reboot command.

The protocol was initially identified from Freqchip's public SDK at
[`9c19ed5`](https://github.com/qdfreqchip/FR801xH-SDK/tree/9c19ed5a0fab09ced402384d2d6a785a77e6224a):
[`ota.c`](https://github.com/qdfreqchip/FR801xH-SDK/blob/9c19ed5a0fab09ced402384d2d6a785a77e6224a/components/ble/profiles/ble_ota/ota.c),
[`ota.h`](https://github.com/qdfreqchip/FR801xH-SDK/blob/9c19ed5a0fab09ced402384d2d6a785a77e6224a/components/ble/profiles/ble_ota/ota.h),
and [`jump_table.h`](https://github.com/qdfreqchip/FR801xH-SDK/blob/9c19ed5a0fab09ced402384d2d6a785a77e6224a/components/modules/platform/include/jump_table.h).
The matching FR8000-family SDK is available in the public mirror
[`shunxingzhe/fr8000` at `9a8a2dd`](https://github.com/shunxingzhe/fr8000/tree/9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6).
The acquired application's actual read callbacks and FR8000 documentation also
support the recovery collector's bounded ROM-read path. This does not establish
the exact installed part, its flash capacity or programming accessibility.

## Offline checks

```powershell
pwsh -NoProfile -File ./tools/radio-ota/tests.ps1
```

Tests exercise packet encoding, bounds, deferred read consumption, short reads,
file integrity, and mismatched/incomplete passes without accessing hardware.
The Windows WinRT path still requires testing on the physical keyboard.
