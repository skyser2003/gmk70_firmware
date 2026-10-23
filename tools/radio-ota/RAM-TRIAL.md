# Volatile radio trial

This document describes the initial canary tool. The later physically verified
native Web Vial experiment is recorded in the
[radio payload README](../../firmware/qmk-common/radio/ram-vial-trial/README.md)
and [trial notes](../../docs/superpowers/plans/2026-10-23-gmk70-ram-trial.md).
The scripts here remain separate from that experiment's local installation tools.

`ram-trial.ps1` is a separate physical-device tool. It uses Windows PowerShell
5.1 and the existing WinRT collection/buffer adapters. Loading its helper files,
building the offline bootstrap, and running tests do not connect to a keyboard.

The trial is restricted to GMK70-2 at `c7:02:53:aa:76:69` and stock application
SHA-256 `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`.
The user supplies that exact captured application as `stock-application.bin`
beside the Windows scripts. Fixtures and private captures are not committed.

## Physical operations

Each invocation requires a new `-OutputDirectory` and saves private local
evidence. Keep the keyboard awake by pressing and releasing Shift approximately
every ten seconds. `-SnapshotFlash` reads the candidate 512 KiB window rather
than only the 148 KiB application region. This is evidence, not a certified
complete recovery backup.

- `-Mode Inspect`: read code, live GATT slots, heap metadata and stock storage.
- `-Mode Canary`: verify stock first, then write and read back exactly four RAM
  bytes at `0x11001AF4`. The marker is `movs r0,#0; bx lr`, replacing the inactive
  OTA CRC-validator entry with return-false. The GATT callback stays stock.
- `-Mode VerifyStock`: require the original code bytes and revalidate storage
  after a user-controlled full radio power-off. USB-mode selection and unplugging
  alone did not reliably clear the later trial; verify actual RAM restoration.
- `-Mode RestoreCanary`: restore those four original RAM bytes only when the
  entire inspected code window matches the exact canary state.

The send boundary rejects every opcode except bounded `READ_DATA` (`6`), RAM
`READ_MEM` (`8`), and a scoped `WRITE_MEM` (`7`). Writes contain at most eight
data bytes to fit one minimum-MTU ATT request. The executable trial grants only
the four-byte canary scope, after stock-image and current-byte validation. It
does not implement callback activation or arbitrary payload execution. A timeout
ends the session, without an automatic retry or cleanup write.

Raw RAM/storage files and `private-report.json` remain private: they may contain
pairing material. Share only a deliberately prepared summary and offline evidence.

## Canary tool's activation boundary

The handoff bootstrap compiles locally to the same 148-byte fragment and passes
the eleven original emulator cases. Independent review also exercised more ABI,
allocator and memory-copy cases. These establish offline behavior, not scheduling
or safe callback activation on the actual device.

The actual stock opcode-7 path calls ROM memcpy at Thumb `0x77D`. A four-byte
aligned destination receives a single word store only when the source is aligned
too. Otherwise it receives four byte stores. The transport does not control or
prove the source alignment of the received GATT packet. Normal dispatch appears
queued, but exhaustive exclusion of interrupt/reentrant dispatch is unproven.
Therefore the callback pointer is never written by this tool. A separately
validated one-byte UART gateway later demonstrated the second HID service and
unchanged Web Vial readout; it does not make this four-byte pointer write safe.

## Offline checks

Run from the repository root with the project environment loaded:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/radio-ota/ram-trial-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/radio-ota/winrt-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/radio-ota/tests.ps1
```

`make test` remains hardware-free. No QMK/Vial board code or pinned dependency
is changed for this host-side canary trial. Vial's installed executable is hashed
in every physical session and is never edited.
