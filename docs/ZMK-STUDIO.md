# GMK70 wired ZMK Studio

Current keyboard state (2026-11-07): both halves run the canonical universal
`wired-studio-updater`, built from the working tree based on `fbcaa1d` with the
[rotation controls above the knob click](ZMK-KNOB-KEYS.md). The application is
106,044 bytes, CRC32 `1225914593`, SHA256
`541cb595e65294b263127c049874c0a2dfae2f8c16bf34c2f38494ea176e6e44`.
Both resident CRCs remain `1770733249`. Permanent firmware retains the
no-watchdog policy.

The return from unlocked Vial used LEFT migration bridge, RIGHT canonical ZMK,
then LEFT canonical ZMK. Each LEFT installation was acknowledged once but
needed a user-confirmed joined-left power cycle after the USB restart timeout.
RIGHT returned automatically. No INSTALL was replayed, and no resident was
written. Independent INFO verified both final images and physical roles.

Native Studio RPC on COM13 verified the new physical geometry and all four
74-position saved layers against the previous clean backup, with no unsaved
changes. The user confirmed normal typing on both halves. The Vial configuration
was backed up privately before the transition. Frozen packages, installation
markers and private snapshots remain in ignored `.work/zmk-return/` storage.
Both halves passed a 70.546-second continuous health observation with zero
watchdog feeds, transport/relay retries or reconnects; exact image and resident
identities were unchanged across the observation.

Earlier 2026-11-06 checkpoint: both halves ran the
[unlocked Vial updater](VIAL-PORT.md). The four-layer Studio map was backed up
privately and preserved through that transition. The installation checkpoints
below are historical.

Earlier checkpoint: [the universal Studio image](ZMK-UNIVERSAL.md) was installed
on both halves at 79,624 bytes/CRC32 893851802, with both residents unchanged at
1770733249. PB9 selects runtime roles; LEFT owns saved settings and RIGHT bypasses
central storage. The four-layer saved map is preserved exactly and normal Studio
is connected. The checkpoints below record previous installations.

Earlier checkpoint: [right RGB restoration](ZMK-RIGHT-RGB.md) installs native split RGB
on both halves (LEFT 70,672/1286125312; RIGHT 43,752/2368050678). Both returned
automatically and passed 70-second zero-feed health, with unchanged residents and
the user's freshly saved Studio map preserved. The user confirmed right LEDs work
and Fn+X controls both halves together.
The earlier checkpoints below record the preceding installations.

Earlier checkpoint: [permanent watchdog removal](ZMK-PERMANENT-WATCHDOG-REMOVAL.md)
applies to canonical builds and both installed halves: LEFT 70,196/986599886,
RIGHT 38,160/1887741245, resident CRCs 1770733249. Both passed 70-second zero-feed
health; Studio saving and the original four-layer map were verified. RIGHT
needed one joined-LEFT cold cycle for the old-watchdog transition. Earlier
experiment/build/state notes below are historical; use the linked build targets.

Earlier 2026-10-29 checkpoint: [LEFT watchdog rollback](ZMK-WATCHDOG-ROLLBACK.md) passed two
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

### Windows desktop connection recovery

The desktop device list can show several identically named **USB Serial Device**
ports. Identify GMK70 by USB VID `342D` / PID `E491` in Device Manager; its COM
number can change. Disconnect the browser's Studio session and other serial
clients before connecting the desktop app. If a failed attempt leaves the port
busy, fully exit Studio and reopen it before retrying the GMK70 port. Resetting
settings or flashing firmware is not a connection-recovery step.

On 2026-11-06, the installed Windows Studio 0.3.1 connected on COM13, loaded all
four layers and 74 positions, and passed three disconnect/reconnect cycles.
A fresh launch without debugging also connected and displayed all four layers.
COM6 belonged to a different USB device. The user's earlier failure on COM13
did not recur, so its original cause remains unconfirmed. These checks changed
neither firmware nor saved settings.

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
