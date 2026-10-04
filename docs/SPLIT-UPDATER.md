# Left USB connection firmware updater

Experimental, opt-in implementation. Direct DFU installation and first boot have
been verified on both halves (2026-10-03), including right-side updater INFO
through the left USB bridge. Application-controlled flash programming, image
relay and interrupted update recovery still require hardware validation. The
existing, hardware-tested
normal firmware packages remain separate.

## What it does

With updater firmware initially installed on both halves, a debug left half can
receive a firmware image over USB and forward it to the right over the existing
UART cable. The host checks both halves, installs and verifies the right first,
then installs the left. The right-side receiver also exists in release updater
firmware. Only debug exposes the USB entry point; a release left half cannot
initiate this procedure. The supplied host tool keeps the left on debug so it
can verify completion and support future updates.

This is an application updater, not forwarding of the factory USB DFU protocol.
It does not update the radio module, receiver dongle, factory ROM bootloader,
external EEPROM, saved VIA maps, or resident installer. It accepts only packages
linked for its application address and the exact installed resident installer.
CRC/SHA checks detect corruption; they are not firmware signatures or an access
control boundary against software already able to access debug USB.

## Build and first installation

Run `make setup`, `make test`, `make check-tools`, then `make build-updater`.
This produces `artifacts/qmk/debug-updater/` and
`artifacts/qmk/release-updater/`. `make build` continues to build the normal,
non-updater release/debug distributions.

Each updater directory contains:

- `install.bin` / `install.hex`: equivalent full 256 KB initial-install images,
  including the resident installer, application and empty staging/commit area.
- `application.bin` and `update.json`: subsequent update payload and manifest.
- `application.hex`: application at **0x08002000**; not an initial-install image.
- Resident/application ELF and maps, checksums and matching source archive.

The initial installation requires direct USB access to **each half separately**,
using the factory WB32 DFU bootloader. The prior installed firmware cannot relay
an update. Verify the device reports **256 KB flash** before using `install.*`.
Both units in the saved 2026-10-03 DFU logs reported 256 KB flash and 36 KB SRAM;
this does not establish capacity for every GMK70 revision. Do not use these
images on 128 KB units. Initial installation itself is not power-fail tolerant.

The inspected Westberry CLI's `write_fm_to_flash` has an unconditional final
partial-page branch even when no bytes remain. A single exact 256 KB transfer
would therefore attempt an extra erase beyond the flash boundary. For initial
installation with that CLI, transfer the first 262140 bytes at `0x08000000`, then
the last four bytes at `0x0803FFFC`, without resetting between transfers. Its
partial-page operation preserves the other bytes in the page. Read back all
262144 bytes and compare them exactly with `install.bin` before resetting. Do
not simply omit the final bytes: the left unit's pre-install backup contained
zeros there. This procedure does not alter the packaged firmware image.
See the vendor [write implementation](https://github.com/WestberryTech/wb32-dfu-updater/blob/master/src_common/dfufw.c).

Use debug updater on both for the first hardware validation. Both halves must
have the updater split protocol before normal split operation is expected;
adding its RPC changes QMK's transaction-count handshake. Do not mix the
normal and updater packages across halves. Updating the resident installer
later also requires direct USB installation, not this relay.

## Subsequent updates

Close VIA and diagnostic readers. Connect the PC to the left USB port, connect
the halves, and select wired mode. Keep power stable throughout the operation.
From the repository root:

```sh
bash scripts/update-device.sh artifacts/qmk/debug-updater --inspect
bash scripts/update-device.sh artifacts/qmk/debug-updater --install
```

`--inspect` sends only updater INFO requests. `--install` explicitly permits
flash writes and resets. The default target is both; `--target right` can also
install the release updater application while the left stays debug. No build,
test, migration or setup command runs this script. The script loads the root
environment through `scripts/project.sh`.

Use a Python/HID backend on the OS that owns the USB device. Ordinary WSL does
not automatically see Windows USB HID. This workstation already has a Windows
HID environment; it can be selected from WSL with:

```sh
GMK70_DEVICE_PYTHON=.work/venvs/battery-diagnostics-windows/Scripts/python.exe \
  bash scripts/update-device.sh artifacts/qmk/debug-updater --inspect
```

The updater's own locked uv project is `tools/firmware-update/`. Its host tests
use simulated devices and do not open USB, and can also run on Windows Python.

## Flash layout and interruption handling

| Address range | Purpose |
| --- | --- |
| 0x08000000–0x08001FFF | Resident installer, never written by the update protocol |
| 0x08002000–0x0801FFFF | Application, maximum 120 KB |
| 0x08020000–0x0803DFFF | Staged image, maximum 120 KB |
| 0x0803E000–0x0803FEFF | Reserved |
| 0x0803FF00–0x0803FFFF | Commit record with bounds, CRC and complement checks |

Packets carry a session and offset. Writes are bounded and contiguous; repeated
packets with the same contents are idempotent. The existing application remains
intact during transfer. A complete staged image must pass its CRC and vector
checks before the commit is written. After reset the resident installer validates
it again, copies and verifies application pages, then invalidates the commit.
An interrupted application copy resumes from staging on the next boot. A failed
validation/copy enters the original WB32 ROM bootloader instead of running a
known-incomplete application. Direct USB/hardware boot entry remains the recovery
path if the receiver or installed application cannot run.

The implementation links the unmodified pinned Westberry/ChibiOS flash driver,
including the vendor's existing pre-operation machine-code sequence and RAM flash
primitive. It does not invent controller programming commands. The resident
installer is independently linked without QMK, initialized RAM or BSS; link-time
assertions keep it within its 8 KB region. The application still uses QMK's
existing 28 KB SRAM layout and bootloader-magic address.

## Normal behavior and validation

No update traffic, flash writes or periodic CRC scans run while idle. The small
housekeeping hook processes only explicit queued requests. It avoids flash work
inside the split transport thread. An unfinished session expires after 10 seconds
without a processed command; only an active update temporarily inhibits sleep.
The 5 ms debounce, stock VIA JSON/keycodes, Custom(7)/Custom(27), radio mode
handling and NONE-wake fix remain as before. Radio-delivered update requests are
rejected before reaching the debug USB handler.

Tests cover packet/session/bounds checks, repeat packets, corrupt images, torn
commit writes, interruption/resumption of every application page, the debug/
release USB boundary, role/radio restrictions, lack of idle traffic and host
right-before-left ordering. The power-cut tests model page erase/program behavior;
real brownout behavior and the vendor flash routine still require hardware tests.

Before recommending daily use, validate direct initial boot, typing/VIA/maps,
the software DFU command, a right-only relay update, then both halves updating;
repeat the sleep/wake and battery-indicator checks afterwards.

The 2026-10-03 initial installation verified the entire 256 KB flash on each
half against `install.bin`, preserving all 360 matrix and eight encoder
assignments on each. Both returned the expected application and resident CRCs
through updater INFO using only the left USB connection; the right reported its
correct slave role. The user confirmed that both halves typed normally after
reconnection. The new debug application's software DFU command and return to the
application also worked on each half. No EEPROM reset or read-protection change
was performed. The local record and backups are under
`artifacts/recovery/2026-10-03-split-updater-installation.json`. These checks do
not establish successful relay programming or real power-loss recovery.
