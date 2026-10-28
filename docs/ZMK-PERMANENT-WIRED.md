# Permanent wired ZMK

As of 2026-10-28, both halves run the normal permanent native ZMK image A
(36576 bytes, CRC32 `1296309606`) with unchanged residents. Fresh independent
readback and 61 seconds of both-half uptime/feed progress verified A/A through
LEFT USB. Different-image left A→B/B→A and right B→A updates were demonstrated.
Automatic restart remains unqualified; joined-left USB cycling is a tested
fallback when a device does not return. See [validation evidence](validation/2026-10-28-zmk-permanent/README.md).

## Firmware and scope

The universal `gmk70_wired` application is genuine pinned ZMK/Zephyr from source
commit `0554dffec19bfb6fb50f3afd87c3c5afb9f9820a`. One image serves both halves:
B9 selects the role and C14/C15 select wired mode. It runs from the application
slot at `0x08002000`, with the existing resident unchanged. USB exposes keyboard
HID0 and updater HID1 (`342D:E491`, `HS`, raw usage `FF60:61`). Packages use
`profile=zmk` and require resident CRC32 `1770733249`, padded SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.

This is an **unlocked development build**, separate from normal QMK/Vial release
assets. It uses a static keymap in the stock physical order, with Fn at left
row 4/column 2. Radio, RGB, encoder rotation, low-power support, Studio, persistent
ZMK settings and stock macro actions are not implemented. Existing native Vial
configuration backups are preserved privately; they are not imported into ZMK,
and no post-ZMK EEPROM/settings readback is claimed.

## Builds and packages

Use the pinned workspace and tools described in the [ZMK build guide](../firmware/zmk/README.md).
Build/setup/test commands never operate the keyboard. Native USB/B1 regressions
also require a host C++20 compiler (`c++`), alongside the native C compiler.

| Command | Output |
| --- | --- |
| `make build-zmk-wired-updater` | Normal A application/updater, product `GMK70 ZMK Wired` |
| `make build-zmk-wired-update-test` | Different B image, product `GMK70 ZMK Wired Update Test` |
| `make build-zmk-wired-qualification` | Bounded staging Q; no permanent install manifest |

Outputs are under `artifacts/zmk/{wired-updater,wired-update-test,wired-qualification}/`.
A/B include `application.bin`, `resident.bin`, `update.json`, ELF/map, image checks,
lockfile and checksums. They update already compatible residents; no full initial
installer is generated. Packaging requires the existing pinned
`artifacts/qmk/debug-updater/resident.bin` and does not rebuild or replace it.
Q links at `0x08020000`, emits target-specific `left/` and `right/` trial packages,
never feeds the inherited watchdog and must not be passed to ordinary INSTALL.

## Manual updater workflow

Automatic restart remains unresolved. Four earlier different-image left
installations failed restart verification; later healthy operation required
joined-left USB reconnection. Those reports remain failed. Application updating
and installation verification are separate: **transfer → inspect/health**, with
a **joined-LEFT manual USB cycle as fallback** if restart verification times out.
Do not request a cycle when the expected image has already returned and passes
verification. A transfer ACK alone is not an installed-image proof.

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
bash scripts/update-device.sh artifacts/zmk/wired-updater --target right --inspect
```

Only after checking current identities/residents and the package, transfer once:

```sh
bash scripts/update-device.sh artifacts/zmk/wired-updater --target right --install
```

The ordinary CLI still attempts automatic restart verification and may time out
after requesting INSTALL. **Do not repeat INSTALL to fix that timeout.** The
last byte-progress line can precede SEAL/INSTALL; do not disconnect at that line.
Wait for verified success or the explicit restart-verification timeout. Preserve
the output; a timeout leaves installation unverified. If verification succeeds,
no manual cycle is needed. If it times out and the device has not returned, let
that attempt end, disconnect only LEFT USB, leave both halves joined, wait
approximately five seconds as in the recorded fallback, then reconnect LEFT USB.
Inspect and read health after return; if the expected image is already reachable,
perform those read-only checks without another cycle. A pre-INSTALL validation
or transfer error, wrong readback or failed health stops further updates; it does not authorize a retry or the next half.

```sh
bash scripts/update-device.sh artifacts/zmk/wired-updater --target right --inspect
uv run --offline --frozen --project tools/firmware-update python \
  tools/firmware-update/read_health.py --target both --duration 60 \
  --output .work/zmk-after-manual-update.json
```

Run the health command using the selected Windows Python instead when that is
the HID backend. Confirm both exact expected application identities, resident
CRCs and roles, normal native USB and at least 60 seconds of advancing uptime/feed
counts, with no hidden reset, reconnect or transport/relay errors, before touching
the next half. For a left update, choose `--target left` explicitly for INSTALL, then
verify both native halves. Never rely on the updater's default `both` target
for this manual sequence. First Vial→right-ZMK migration additionally requires
reviewed staging qualification and shared-recovery gates; these short commands
do not replace that separate procedure.

## Evidence and limits

The genuine right Vial empty PROBE returned automatically with 60.156 seconds of
advancing recovery feeds. After the left B transfer and joined-left cycle, its
saved terminal record became invalid, right uptime/feed reset and both applicable
health windows passed: native left 60.297 seconds and ordinary right Vial 60.375
seconds. Ordinary cold Vial flags 0/feed 0 are valid uptime evidence, not fabricated
recovery feeds. This supports practical shared reinitialization on this wiring,
not an electrical power measurement or a universal recovery guarantee.

Right Q (36152 bytes/CRC 3681276862) supplied actual INFO and staging health (flags 10,
zero feeds) within 0.750 seconds of RUN ACK, under the unchanged 3-second cutoff,
then genuinely returned to Vial for 60.141 seconds with zero retries. Q executes
from staging; it does not prove permanent app-slot entry or feeding before the
first right replacement. That residual risk was explicitly reviewed.

The first permanent right B installation was separately verified after the
joined-left cycle: both halves reported B (36600 bytes/CRC 752626188) and unchanged
residents, with 60.984 seconds of advancing native uptime/feeds and no transport,
relay or reconnect errors. This establishes permanent native B/B runtime.

Right B→A then passed separate B/A readback and 61 seconds of both-half health.
After the final left B→A transfer, native USB was present and independent
read-only inspection, 61 seconds of both-half health and a fresh post-health
identity/USB check established final A/A. Every final transport/relay/reconnect
counter was zero; both residents remained CRC32 `1770733249`.

The strict `verify-left-A-final` manual stage was **not executed** because no
final cycle confirmation was available. No confirmation was invented. The final
physical action is unknown, so this result proves installed A/A and updating,
not automatic restart or completion of every guarded manual stage. The user
reports typing seems functional and Fn+number produces function keys. An initial
Fn+Left test in this input field appeared to jump by words. A follow-up,
user-confirmed Left / Fn+Left / Fn+Right sequence was observed as Windows
Left / Home / End, with no Ctrl held in any of the 61 recorded navigation states.
Separate Ctrl presses were visible to the same observer. This confirms the
tested mappings at the OS key-state boundary; it does not localize the field's
visual navigation behavior or constitute raw USB capture. The actual compiled
normal A mapping agrees: `&mo 1` at left row 4/column 2, HID Home (`0x7004a`)
and End (`0x7004d`) on the function-layer arrows. No firmware change was needed.
Hardware peer-disconnect release remains untested. A zero/unavailable
matrix-event count is not typing evidence.

There is no old-application rollback slot or proven universal automatic fallback.
Joined-left cycling cannot repair every possible unreachable right image. Arbitrary
warm rollback from ZMK to frozen Vial is unqualified and may need joined-left
power cycling; never manufacture a trial record to claim recovery. Source-backed
worker/B1 repairs and the failed/reverted 50-ms interval experiment did not isolate
the automatic restart cause. The identical-image control was deferred without
hardware execution. [Sanitized validation](validation/2026-10-28-zmk-permanent/README.md)
keeps successful manual results separate from earlier failures.
