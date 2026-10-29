# Wireless implementation validation — 2026-10-29

See [implementation and controls](../../ZMK-WIRELESS.md) and the aggregate
[result](result.json). Firmware source is `f9f12ddadd59e0481f750206c03a4ae5741d6fab`;
the corrected guarded installer is `b994409`.

Both LEFT-only updates returned automatically, with one INSTALL per image and no
physical reconnect, RIGHT write or pad recovery. The first helper rejected a
valid acknowledgment because it expected INFO's resident CRC in INSTALL's
reserved-zero field. No command was replayed; independent image/resident/health
observation verified the installation. The fixture now reflects the real
protocol, and the corrected helper accepted the normal image's acknowledgment.

The both-half pre/post-qualification and pre/post-normal health windows were
70.171, 70.125, 70.265 and 70.234 seconds. The final post-layout window was
70.140 seconds. All had zero transport/relay retries, reconnects or watchdog
feeds. LEFT is normal 78,032/3592996973, RIGHT remains 43,752/2368050678 and both
resident CRCs remain 1770733249.

Bluetooth slot 2 reused an existing Windows BLE bond. Two independent temporary
Raw Input sessions observed one F24 press and release each from the approved
Bluetooth keyboard interface, including after switching away and back. Other
keys were not recorded. The normal image also reconnects with two approved
keyboard interfaces and zero radio errors. Its qualification flag is false, the
marker symbol is absent and the marker command is unavailable. USB output is
restored and normal Studio connects with all four saved layer names visible.

Dongle selection and protocol acknowledgment were checked, but no receiver is
detected on this host; receiver-side typing is unverified. New pairing, physical
host-switch movement, consumer input reception, mechanical key actuation and
battery-powered operation were not tested during this unattended run. Native
tests cover frame routing, pairing holds/cancellation, physical-mode debounce,
split/RGB continuity and report/release bounds. No battery-life, untethered-split
or actual stack-watermark claim is made.

Both images preserved the original saved map. Selective import then restored
exactly eight wireless bindings and preserved all other 280, all four layer
names/order and the private source export. Save/reload and a new serial connection
verified all 288 bindings, behavior metadata and physical geometry. Five original
unsupported bindings remain explicitly unassigned: function/media flip, logo
effect, reserved board action and two QMK settings resets.

`make test test-zmk check-tools` passes repository, migration, native firmware,
ZMK image and strict Python checks. The final updater suite runs 175 tests with
one optional built-resident integration skip; other suites include 88 repository,
21 migration, 23 ZMK Python and 16 battery-diagnostics tests. Edited C formatting,
Ruff and strict mypy pass. Normal, radio-qualification, peripheral and staging
qualification ARM builds pass. Normal static RAM is 27,488/27,648 bytes;
qualification RAM is 27,512, with all existing stacks retained. Existing upstream
KSCAN deprecation and RWX linker warnings remain.

Private snapshots, frozen cases, host tickets, test logs and verification helpers
are archived locally with a SHA256 inventory and excluded from Git. Archived
operators are provenance, not resume commands. This directory contains no
personal mappings, host identifiers or bond data.
