# GMK70 wired ZMK Studio

Current 2026-10-29 checkpoint: [LEFT watchdog rollback](ZMK-WATCHDOG-ROLLBACK.md) passed two
automatic original-SYSRESETREQ updates and Studio save/reload. Installed LEFT is
70,340/1796245169 with no watchdog; RIGHT and residents are unchanged. This
supersedes the installed-state notes below. The exact reset/retention mechanism
remains unmeasured; ordinary watchdog-enabled variants are unchanged.

Current LEFT checkpoint: the [resident-handoff experiment](ZMK-RESIDENT-HANDOFF.md) now runs four-layer Studio and passed automatic A→B→A updates. RIGHT and both residents remain unchanged. The two-layer result below is the earlier milestone.

The experimental `wired-studio-updater` image adds standard USB serial Studio
communication and persistent keymap editing to the LEFT central. The existing
RIGHT wired ZMK scanner supplies all right-hand keys; it needs no Studio flash.
This is an explicitly unlocked development image, separate from normal ZMK and
Vial distributions. It is not a normal release asset.

Current sources also contain an **uninstalled** four-layer/RGB candidate. See
[Mac layers and lighting](ZMK-MAC-LIGHTING.md) for behavior, storage migration,
memory changes and the remaining hardware validation. The installed checkpoint
and evidence in the next section remain the earlier two-layer image.

## Verified on 2026-10-28

The installed local Windows Studio app connects over COM3, displays the GMK70
72-key layout with Base/Fn layers, and edits/saves settings. A temporary key
binding and a 19-character layer name survived a distinct-image LEFT Studio
firmware update. Both were restored, reloaded and compared with the entire
original keymap; Studio reports no unsaved changes.

The final LEFT image is 61,696 bytes/CRC32 `1010245074`. RIGHT remains the original
permanent normal A at 36,576 bytes/CRC32 `1296309606`. Both resident CRCs remain
`1770733249`. Final both-half health passed 61 seconds with zero transport/relay
retries while Studio was connected. Two acknowledged LEFT installs each required
a joined-left USB power cycle after the warm-restart timeout. No RIGHT flash,
right-only USB connection or pad recovery was used.

See the [validation record](validation/2026-10-28-zmk-studio/README.md) for scope,
review findings and evidence limits.

## Build and connect

Run `make setup-zmk` explicitly to initialize the exact locked dependencies, then
`make build-zmk-wired-studio-updater`. Build and test commands never access the
keyboard. The generated application-only package is in
`artifacts/zmk/wired-studio-updater/`, with profile `zmk-studio`. Its installer
accepts only LEFT installation. The resident is the existing pinned installer;
this build does not rebuild it.

With the halves joined and USB on the left, open local ZMK Studio and select its
USB serial port. On the tested Windows installation this is **USB Serial Device
(COM3)**; the port number can differ. Once connected, the app identifies the
keyboard as **GMK70 ZMK Studio**. The physical layout contains all 72
positions. Current candidate builds add Windows/Fn and Mac/Fn; the verified
installed image still has Base/Fn. Changes take effect while editing; use Studio's
Save action to persist them. Close another Studio connection before opening the
same serial port in a different application.

The updater remains available alongside Studio. Its host client discovers the
vendor Feature collection (report 4, page FF60/usage62) on keyboard HID0; the normal
images retain the legacy usage 61 interrupt transport. No extra driver is required
for the standard CDC interface. Device operations remain separate from builds.

## Storage and update contract

Studio applications are limited to 104 KiB at `08002000..0801bfff`. Two 8 KiB settings
banks occupy `0801c000..0801ffff`. Staging, the commit page, retained RAM and the
resident keep their existing locations. The external SPI Vial configuration is
not used or erased.

A compatible Studio application update preserves these settings banks. A larger
non-Studio application can overwrite them; this reservation is not a new
restriction on the common resident ABI. Keep the generated package and matching
checker report together, and use the maintained updater's explicit LEFT target.

Each acknowledged setting is durable independently. A Studio Save can update
several settings; interruption may retain a durable prefix of that Save rather
than make the whole keymap atomic. Native tests inject interrupted writes at
append and bank-compaction boundaries. Real brownout timing is not established.

Unknown nonblank settings storage is never silently formatted. Studio mutations
stay locked when the backend cannot mount or safely write its banks. An
interrupted very first provisioning can require explicit settings recovery,
because no previous committed bank exists. Keyboard operation and the firmware
updater remain separate from that Studio lock.

Automatic warm restart remains an unresolved platform limitation. An acknowledged
installation and a returning USB device are separate checks. If restart
verification times out, inspect USB and use the established joined-left power
cycle, followed by image/resident readback. Do not replay INSTALL or move USB to
the right to recover a restart timeout.
