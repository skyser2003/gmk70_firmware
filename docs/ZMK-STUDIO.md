# GMK70 ZMK Studio

The universal `wired-studio-updater` supports Studio on LEFT, with four
Windows/Mac layers, 72 physical keys and two virtual knob-rotation positions.
RIGHT supplies its keys and RGB through the joined split connection.
See [knob remapping](ZMK-KNOB-KEYS.md) and
[the universal image contract](ZMK-UNIVERSAL.md).

## Build and connect

Run `make setup-zmk` to initialize locked dependencies, then
`make build-zmk-wired-studio-updater`. Build/test commands never access the
keyboard. The first-install and application-update package is in
`artifacts/zmk/wired-studio-updater/`, with profile
`zmk-studio-universal`. First installation uses `install.bin` at `0x08000000`;
later application updates retain the pinned resident. See the
[installation guide](ZMK-PERMANENT-WIRED.md#first-installation).

With the halves joined and USB on LEFT, open ZMK Studio and select the keyboard's
USB serial port. The keyboard identifies as **GMK70 ZMK Studio**. Changes take
effect while editing; use Save to persist them. Existing saved bindings and
layer names override compiled defaults. Close other Studio/serial connections
before opening the port in another application.

### Windows desktop connection recovery

Several devices can appear as **USB Serial Device**. Identify GMK70 by USB VID
`342D` / PID `E491`; COM numbers can change. Disconnect the browser's Studio
session and other serial clients before connecting the desktop app. If a failed
attempt leaves the port busy, fully exit Studio and reopen it before retrying.

The updater remains available alongside Studio. It uses the vendor Feature
collection on keyboard HID0: report 4, page FF60/usage 62. The standard CDC
interface needs no extra driver.

## Storage and update contract

Studio applications are limited to 104 KiB at `0x08002000..0x0801bfff`.
Two 8 KiB settings banks occupy `0x0801c000..0x0801ffff`. Staging, the
commit page, retained RAM and resident retain their locations. The external
SPI Vial configuration is not used or erased.

Compatible Studio application updates preserve the settings banks. A larger
non-Studio application can overwrite them; the reservation does not change
the common resident ABI. Keep each package with its matching image report.
Use an explicit target when updating either half.

Each acknowledged setting is durable independently. A Studio Save can update
several settings; interruption may retain a durable prefix of that Save.
Native tests exercise interrupted appends and bank compaction. Real brownout
timing remains a hardware concern.

Unknown nonblank settings storage is never silently formatted. Studio mutations
stay locked when the backend cannot mount or safely write its banks. Interrupted
first provisioning can require explicit settings recovery because no prior
committed bank exists. Keyboard operation and the updater remain separate from
the Studio storage lock.

Permanent firmware never initializes or feeds a hardware watchdog. If an
acknowledged installation does not return after restart, follow the
[updater recovery workflow](ZMK-PERMANENT-WIRED.md): inspect first, power-cycle
the joined halves if needed, then verify image/resident identities.
Never replay INSTALL to resolve a restart-verification timeout.
