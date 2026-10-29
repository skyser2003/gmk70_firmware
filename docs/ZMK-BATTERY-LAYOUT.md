# Battery indicators and saved Vial layout migration

The Studio central now implements two held actions under **GMK70 Battery /
Charging**: parameter 0 displays battery level; parameter 1 displays charging
status. They can be assigned in Studio. The compiled factory keymap is unchanged.
The right peripheral displays the central's existing RGB frames and needs no
battery firmware update.

Holding the level action temporarily shows the stock ten number-row LEDs. Green
indicates at least 30%; lower levels use red. Zero still lights one segment,
matching the stock display. Unknown or older-than-ten-second data shows all ten
segments purple. Holding charging status overlays the left Fn LED red when cable
power is detected, or green when the stock full-status input is also asserted.
Releasing restores the current normal animation or off state. Both actions work
with ordinary RGB off and can be held together.

## Battery implementation

Only LEFT uses UART3, PC10/PC11 AF7 at 115200 baud. The existing worker polls a
bounded parser and transmitter without a new thread, interrupt or blocking wait.
Outgoing messages are restricted to the vendor battery inquiry `A6 53 F9` and
acknowledgment `61 0D 0A`. Queries run every three seconds with one bounded retry.
Checksummed percentages must be in 0..100. The parser consumes other status/raw
frames so their embedded bytes cannot become battery samples. A timed-out raw
frame retains a finite discard budget; truncation can delay recovery of valid
samples, which remain explicitly unavailable during that interval.

GPIO A7 is cable detection; A15 is the stock charger-full input. This reports the
board's status signal, not measured charge current. The cache also feeds a native
Zephyr state-of-charge sensor, with no fabricated voltage reading. Percentage is
the central radio's stock reading, not independent measurements of both packs.
Radio firmware, pairing, wireless transport and speculative radio-reset pins are
unchanged. This adds held Studio actions, not a Studio battery dashboard.

`tools/firmware-update/read_battery.py` reads a cache-only LEFT snapshot through
the updater's separate namespace `0x72`. It never resets, retries, writes flash
or sends radio commands. Run it separately from builds/tests, using the existing
firmware-update uv project after loading `scripts/project.sh`.

## Saved layout import

`tools/keymap-migration/vial_to_zmk.py` is an offline converter. It resolves the
maintained 72-position matrix geometry, preserves all four source layers and
translates supported HID, modifier, layer, lighting, OS-selection and battery
actions into symbolic ZMK behaviors. Unsupported actions are reported explicitly;
`--unsupported=unassigned` is the user's selected policy for this import. Active
unsupported macros or other auxiliary features still block conversion. Vial
tuning settings are not imported.

The separate `tools/firmware-update/studio_rpc.py` device client resolves runtime
behavior IDs and validates all 72 physical keys before a write. It requires a
clean saved map, creates a fresh private backup, preserves current layer names
and order, writes bindings, saves, reloads and compares the complete result.
Failures attempt to restore and verify the fresh original map. There is no
automatic reconnect or firmware update in this client. Personal exports, plans,
backups and full readbacks stay outside version control.

## Build and verification

Source `74f1012c493f2bae6b2efaeffb3b235e8cfe964b` builds with
`make build-zmk-wired-studio-updater`. The canonical peripheral and qualification
variants also compile. Permanent watchdog removal, residents, settings banks,
thread stacks and the factory keymap are unchanged.

The Studio application is 74,020 bytes, CRC32 `3805544436`, SHA256
`d6226bdcef65c3539972c4b49575ca1034ffa6b67d3610e691347ce7eabdf695`.
Linked static RAM is 27,560 of 27,648 bytes. Existing thread stacks are retained;
conservative compiler stack analysis estimates about 640 bytes including an
exception frame in the 768-byte low-priority queue. Actual hardware stack
watermarks remain unmeasured. Existing upstream KSCAN deprecation and RWX linker
warnings remain.

`make test test-zmk check-tools` passes repository, migration, native firmware,
protocol, Studio persistence/rollback and strict Python checks. Dedicated tests
cover malformed and delayed raw frames, UART/GPIO isolation, stale samples,
overlapping holds, released/reassigned keys, RGB-off restoration, split slicing
and read-only packet admission. Device validation is recorded separately.

## Installed result — 2026-10-29

LEFT received one INSTALL request and returned automatically on the exact
74,020-byte image above. RIGHT remains 43,752 bytes/CRC32 `2368050678`; both
resident CRCs remain `1770733249`. No physical reconnect, right-only connection
or recovery pads were needed. Post-install both-half health passed 70.110 seconds;
after the layout save it passed another 70.203 seconds, with zero transport/relay
retries, reconnects or watchdog feeds throughout.

Read-only battery telemetry reports a fresh 100% reading, cable present and the
full-status input asserted. The final sample had 111 replies to 111 queries,
with age 1,056 ms. The user confirmed both visible held indicators and normal
lighting restoration on release. Low percentages, stale/unknown indication and
RGB-off operation have dedicated native tests; hardware checking covered the
full-charge point, with no separate RGB-off confirmation or complete charge/
discharge cycle. No charge-current or battery-capacity accuracy claim is made.

Before importing, all 288 existing saved bindings and four layer names/order
matched the fresh pre-update snapshot exactly. The user then selected importing
275 supported bindings and leaving 13 unsupported bindings unassigned. Those 13
are eight wireless selectors, function/media flip, a logo effect, one reserved
custom slot and two QMK settings resets. The converter also records 27 Vial
tuning settings as excluded; the export has no active unsupported auxiliary
features.

All 288 resulting bindings were saved, reloaded and compared through Studio RPC.
An independent new serial connection resolved the runtime behavior IDs and
matched the entire imported map and physical geometry, with no unsaved changes.
Original layer names/order and the source export are preserved. The compiled
factory keymap is unchanged. The normal local Studio app is connected on COM3
and displays all four layers. See the [sanitized validation](validation/2026-10-29-zmk-battery-layout/README.md).
