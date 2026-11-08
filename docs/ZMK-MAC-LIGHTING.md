# GMK70 Mac layers and lighting

## Four layers and OS selection

Current wired builds compile four 74-position layers: **Windows**, **Windows Fn**,
**Mac**, **Mac Fn**. Mac uses stock Option/Command placement: left Alt becomes
left GUI, right Alt becomes right GUI and right GUI becomes right Alt. Its Fn key
selects Mac Fn. Other ordinary base keys retain the stock physical mapping.

- **Fn+S:** select Mac.
- **Fn+A:** select Windows.
- **Fn+number:** F1–F12, on either OS pair.

These use standard ZMK `&to`/`&mo` behaviors, so Studio can reassign them.
The board controls integration observes OS selection and persists it through
native settings. See [basic feature parity](ZMK-BASIC-PARITY.md).

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

RGB runtime persistence retains the native 60-second debounce and opaque saved
representation. The latest snapshot remains pending through storage contention
and failed writes. RGB is in the narrow wireless runtime-save allowlist;
Studio keymaps and behavior-ID writes retain their wired gates. Execution
rechecks role, power and service/storage conditions; retries are bounded to
once per second. Power loss can discard unsaved RAM; latched backend faults
retain their fail-closed policy. See the
[native-path acceptance checklist](CUSTOM-BEHAVIOR-CHECKLIST.md).

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

## Settings and memory

The existing `0801c000..0801ffff` reservation and two 8 KiB banks are unchanged.
Version-2 pages encode canonical binding names in two bytes. Version-1 pages
remain readable; mounting performs no migration writes. Compaction upgrades
the inactive bank and commits last. The store uses bounded flash traversal and
a 344-entry logical limit; see [storage constraints](ZMK-BASIC-PARITY.md).

Do not downgrade a modified v2 settings store to an old image expecting v1:
that image cannot read new records and may fall back to an older bank. Keep a
configuration backup and a compatible four-layer image for future updates.

Studio retains the 104 KiB image bound. Matched CDC/RPC rings are 128 bytes and
retain real framed/escaped maximum-width request regression coverage. Studio
RPC has 2560 bytes and main has 1024 bytes; scanner/serial/update/interrupt stacks
are unchanged. Main loads settings and returns; flash writes execute on the
separate 2048-byte update worker. The native RGB work queue retains its 768-byte
stack. Build-time frame sizes do not establish worst-case call chains or hardware
stack watermarks.
