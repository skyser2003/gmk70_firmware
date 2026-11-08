# Permanent ZMK updater

Permanent applications run at `0x08002000`, preserving the existing resident.
The canonical [universal Studio image](ZMK-UNIVERSAL.md) serves both halves.
The static-keymap A/B builds and temporary qualification image remain separate
development targets. See the [build guide](../firmware/zmk/README.md).

Packages require resident CRC32 `1770733249`, padded SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.
Packaging reads `artifacts/qmk/debug-updater/resident.bin`; it never rebuilds
the resident. The canonical universal package includes full first-install
images; application updates preserve the installed resident.
The universal package uses `profile=zmk-studio-universal`, with a 104 KiB
application limit and separate 16 KiB settings reservation.

The updater is explicitly unlocked. Permanent firmware never initializes or
feeds a hardware watchdog. Update admission retains service, wired, USB, role
and storage gates. Migration from legacy QMK/Vial transport requires the
[separate native bridge](ZMK-NATIVE-UPDATER-WIRE.md) when using the split update
route. Direct first installation on each half does not need that bridge.

## First installation

The canonical universal package includes `install.bin`, a complete 256 KiB
flash image: the exact reviewed resident
in the first 8 KiB, the universal application at `0x08002000`, and `0xff` in
the remaining space, including both Studio settings banks and all staging and
commit data. Flash this BIN at `0x08000000`.
Both halves use the same image. Experimental/bridge packages remain
application-only.

Use first-install images for installing the complete firmware through the
factory WB32 USB bootloader on **each half separately**, after verifying
**256 KiB flash** on each MCU. This does not require an existing resident or a
QMK/Vial installation. It clears saved ZMK settings, so back up first. Use
`application.bin` and `update.json` for routine updates that preserve settings.

Follow the [direct-USB installation procedure](SPLIT-UPDATER.md#build-and-first-installation)
with the ZMK package's `install.bin`. In particular, the inspected WB32 CLI
overruns its erase boundary with a single exact 256 KiB transfer: write the
first 262140 bytes at `0x08000000`, then the final four at `0x0803FFFC`, without
resetting between transfers. Read back all 262144 bytes and compare with
`install.bin` before resetting. Do not omit the final bytes. First installation
is not power-fail tolerant. Do not use these images on a 128 KiB MCU.

After both installations, join the halves, select wired mode and connect LEFT
USB. Check both roles, typing and Studio before using the normal updater.
Image layout, resident identity, settings boundaries and package checksums
are checked offline; the new direct-install package has not yet been tested
on physical hardware. Build/package commands never operate on a device.

## Manual updater workflow

Transfer and running-image verification are separate. An acknowledged INSTALL
does not prove the device restarted. If restart verification times out,
inspect first; use a joined-halves cold power cycle when the device is absent.
Never replay INSTALL to resolve a restart-verification timeout.

Keep both halves joined in the tested wired configuration, close other HID
readers, and use the OS/Python backend that owns the USB device. The existing
driver setup is unchanged. Ordinary WSL Python does not automatically see Windows
HID. `scripts/update-device.sh` uses the locked firmware-update uv environment
and honors an existing `GMK70_DEVICE_PYTHON` override; use the same HID-capable
Python for health reads. No new driver installation is part of this workflow.

For later application updates, first inspect a compatible package and choose
**one explicit target**, for example right:

```sh
source scripts/project.sh
bash scripts/update-device.sh artifacts/zmk/wired-studio-updater --target right --inspect
```

Only after checking current identities/residents and the package, transfer once:

```sh
bash scripts/update-device.sh artifacts/zmk/wired-studio-updater --target right --install
```

The ordinary CLI still attempts automatic restart verification and may time out
after requesting INSTALL. **Do not repeat INSTALL to fix that timeout.** The
last byte-progress line can precede SEAL/INSTALL; do not disconnect at that line.
Wait for verified success or the explicit restart-verification timeout. Preserve
the output; a timeout leaves installation unverified. If verification succeeds,
no manual cycle is needed. If it times out and the device has not returned, let
that attempt end, unplug LEFT USB and turn both joined halves off for a few
seconds. Turn them on and reconnect LEFT USB.
Inspect and read health after return; if the expected image is already reachable,
perform those read-only checks without another cycle. A pre-INSTALL validation
or transfer error, wrong readback or failed health stops further updates; it does not authorize a retry or the next half.

```sh
bash scripts/update-device.sh artifacts/zmk/wired-studio-updater --target right --inspect
uv run --offline --frozen --project tools/firmware-update python \
  tools/firmware-update/read_health.py --target both --duration 60 \
  --output .work/zmk-after-manual-update.json
```

Run the health command using the selected Windows Python instead when that is
the HID backend. Confirm both exact expected application identities, resident
CRCs and roles, normal native USB and at least 60 seconds of advancing uptime,
intentional no-watchdog health and zero feed counts, with no hidden reset,
reconnect or transport/relay errors, before touching
the next half. For a left update, choose `--target left` explicitly for INSTALL, then
verify both native halves. Never rely on the updater's default `both` target
for this manual sequence. First Vial→right-ZMK migration additionally requires
reviewed staging qualification and shared-recovery gates; these short commands
do not replace that separate procedure.

## Recovery limits

There is no old-application rollback slot or guaranteed automatic fallback.
Power cycling cannot repair every unreachable application. A zero/unavailable
matrix-event count is not typing evidence. Native tests cannot establish
physical USB/UART timing or power behavior. Keep firmware/settings backups
outside version control and verify both halves before continuing an update.
