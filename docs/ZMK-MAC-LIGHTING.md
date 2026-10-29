# GMK70 Mac layers and lighting candidate

Implemented offline on 2026-10-28/29. **Not installed or hardware-qualified.** The
keyboard remains on the verified two-layer Studio LEFT and original wired RIGHT
documented in [Studio validation](validation/2026-10-28-zmk-studio/README.md).
No device query, write, reset, reconnect or resident change was made for this task.

## Four layers and OS selection

Current wired builds compile four 72-position layers: **Windows**, **Windows Fn**,
**Mac**, **Mac Fn**. Mac uses stock Option/Command placement: left Alt becomes
left GUI, right Alt becomes right GUI and right GUI becomes right Alt. Its Fn key
selects Mac Fn. Other ordinary base keys retain the stock physical mapping.

- **Fn+S:** select Mac.
- **Fn+A:** select Windows.
- **Fn+number:** F1–F12, on either OS pair.

These use standard ZMK `&to`/`&mo` behaviors, so Studio can reassign them. The
runtime OS selection is not persistent: power-on starts on Windows layer 0.
Stock QMK intercepts its OS-mode bindings to save a persistent default; that
board-specific interception is not part of standard ZMK `&to`.

Pinned ZMK has no native USB host-OS detector or per-OS initial-layer table.
The upstream [OS detection request](https://github.com/zmkfirmware/zmk/issues/2553)
remains open. QMK offers optional
[USB fingerprint detection](https://docs.qmk.fm/features/os_detection), which is
a best guess rather than a reliable host identity. Community ZMK modules such
as [cormoran's OS detector](https://github.com/cormoran/zmk-feature-os-detection)
and its linked default-layer extension provide additional options. They were
researched, not added as unpinned dependencies or enabled speculatively here.

Existing saved Base/Fn bindings and names override updated defaults, as ordinary
Studio settings do. New IDs 2/3 remain visible when an existing two-entry layer
order is loaded: pinned ZMK initializes all compiled IDs before applying the
stored prefix. No factory reset is required to expose the Mac pair. Existing
user bindings at Fn+A/S or lighting positions are preserved; if such a position
was already saved, assign the desired new behavior in Studio to replace it.

## Lighting controls

The port now enables upstream ZMK RGB underglow on the GMK70's addressable LEDs.
ZMK calls the RGB system “underglow” even when LEDs are under keys. One virtual
77-pixel strip spans 36 left and 41 right LEDs, including the six decorative
positions. Supported native effects are solid, breathing, spectrum and swirl.
This does not reproduce QMK's reactive RGB-matrix effects or separate decorative
engine. Brightness has a 32% physical ceiling; initial logical brightness is 20%.

| Binding on either Fn layer | Action |
| --- | --- |
| Fn+X | On/off |
| Fn+Tab / Fn+backslash | Next / previous effect |
| Fn+[ / Fn+] | Hue down / up |
| Fn+; / Fn+' | Saturation down / up |
| Fn+Down / Fn+Up | Brightness down / up |
| Fn+Left / Fn+Right | Animation speed down / up |

Fn+Left/Right now control lighting speed, matching the stock lighting positions;
they no longer provide the earlier prototype's Home/End shortcuts. Fn+H/J still
provide Home/End and Fn+Enter remains End.

ZMK's [RGB behaviors](https://zmk.dev/docs/keymaps/behaviors/underglow) are exposed
to Studio as **RGB Underglow** binding choices, with command metadata for on/off,
brightness, hue, saturation, effect and speed. Native
[Studio capabilities](https://zmk.dev/docs/features/studio) do not include a
Vial-style live lighting panel. The pinned protocol exposes core, behaviors and
keymap RPCs, with no RGB-state/control subsystem. Editing a key's RGB behavior
and pressing that key is the supported manual control path; this task does not
claim a new Studio menu. The local HSB adapter also schedules native debounced
persistence after hue/saturation/brightness commands; upstream sources are intact.

## Integration and bounds

`lighting/wb32_pixels.c` owns SPIM2/B15 AF5 and DMAC2 channel 0. It uses RGB byte
order, 96 MHz/32 SPI, four-bit WS2812 symbols and a 280 µs GPIO-low reset. A5/A8
enable LED power. The maximum encoded buffer is 492 bytes. A synchronous DMA
transfer holds scheduling but leaves interrupts enabled, keeping flash clock
changes from running during the waveform without masking USB/UART service.
Completion/error polls have a 5 ms deadline and a finite iteration fallback.
All transfer exits stop DMA/SPI and return the data pin low; an unquiescent DMA
channel prevents buffer reuse.

Wired transaction 11 retains its original 16-byte input and transaction count.
Native packets carry magic, frame ID, chunk index, 12 RGB bytes and CRC8. Eleven
chunks supply the right frame; incomplete/malformed frames never become ready.
The scanner sends one chunk after a successful matrix poll. It displays a claimed
right frame outside the wire/spin locks. New frames supersede queued animation
data, and cached frames replay every 200 ms after completion/failure so “off”,
packet loss and a restarted right half recover without another key press.
Lighting never feeds the watchdog or enters the updater packet handler.

The current installed RIGHT safely ignores this transaction. **Both halves need
the new application for both-half lighting.** The normal wired image is the
RIGHT candidate; Studio remains LEFT only. No right-only flashing procedure is
introduced, and no device operation has been performed.

## Settings and memory

The existing `0801c000..0801ffff` reservation and two 8 KiB banks are unchanged.
Version-2 pages encode canonical binding names in two bytes and index 320 record
offsets. Version-1 pages remain readable; mounting performs no migration writes.
Compaction upgrades the inactive bank and commits last. Tests cover all 288
12-byte bindings plus 16 behavior IDs, four names, layer order and RGB settings,
then further compactions. Legacy migration is cut at every byte of both written
pages, alongside the existing journal/compaction fault tests.

Do not downgrade a modified v2 settings store to an old image expecting v1:
that image cannot read new records and may fall back to an older bank. Keep a
configuration backup and a compatible four-layer image for future updates.

Studio retains the 104 KiB image bound. Matched CDC/RPC rings are 128 bytes and
retain real framed/escaped maximum-width request regression coverage. Studio
RPC has 2560 bytes and main has 1024 bytes; scanner/serial/update/interrupt stacks
are unchanged. Main loads settings and returns; flash writes execute on the
separate 2048-byte update worker. The native RGB work queue retains its 768-byte
stack. ARM `-fstack-usage` output was inspected: RPC main 168 bytes, largest
nanopb decoder frame 144, backend load 104, store mount 56, snapshot scan 64 and
checksum 264. These are individual frames, not measured worst-case call chains.
Hardware stack watermarks remain required during supervised installation qualification.

## Verification and installation boundary

Run `make test`, `make test-zmk`, and the Studio, normal wired and qualification
build targets after loading `scripts/project.sh`. All are device-independent.
Native tests exercise encoding, DMA error/timeout/cleanup, clock wrap/frozen
timer, exact half slicing, claimed-buffer ownership, CRC, loss/reordering,
sequence wrap, “off” supersession and replay. They do not measure the actual
LED waveform, GPIO power rails, USB latency, bus contention or stack high-water
marks on this keyboard. No hardware lighting or four-layer Studio success is
claimed yet.

The [restart investigation](ZMK-WARM-RESTART-ANALYSIS.md) has no established fix.
An installation now could again require a physical power cycle. Preserve the
working keyboard until a supervised validation window; do not turn this offline
candidate into an unattended flashing loop.
