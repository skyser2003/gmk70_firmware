# Battery and split diagnostics

QMK/VIA debug builds include a RAM split/sleep recorder. Release/default and
normal Vial builds exclude it. Use `make build-debug` and
`artifacts/qmk/debug/zuoya_gmk70_debug.bin`. Build/test commands never flash
or reset a keyboard. See [debug/release behavior](DEBUG-RELEASE.md).

## Current split recorder

The current split logger uses protocol 2. Four peer-status bytes occupy the
unused body of the existing two-second `0x55` mode reply. Its acknowledgment
remains zero; logging adds no split transactions or request bytes. Sampling
can miss transitions between replies.

Responses begin `02 81 <page> 47 53 02`. The protocol-1 page layout below is
retained, except SPI fields are reserved/unavailable: status bit 5, page 0 bytes
26–29, page 1 bytes 18–29, peer byte 3 and event 14. Zeroes there are not
measurements. `read_split.py` accepts both versions and labels unavailable SPI
telemetry. It cannot attribute a stalled main loop to SPI in protocol 2.

The recorder keeps the latest 128 significant events per half, counters and a
peer sample. Events include power-state changes, sleep reasons, device changes,
LED/sleep RPCs, split failures and long main-loop gaps. It records no ordinary
key identities or typed text. Repeated failures increment counters without
filling the ring. A completely inaccessible peer cannot deliver new samples.
History is lost on reset/power loss; use the
[persistent recorder](PERSISTENT-SLEEP-LOG.md) when that is required.

## Capture procedure

Use matching debug images on both halves. Keep USB power on LEFT and the halves
joined. Take an initial wired read, reproduce the failure, then return LEFT's
selector to USB without resetting or removing power. Close other configuration
clients and read promptly. The getter never changes settings or clears history.

Load root environment files through `scripts/project.sh`. Use the OS/Python
backend that owns HID. WSL requires explicit USB access; a separate Windows uv
environment avoids replacing Linux dependencies.

```sh
source scripts/project.sh
uv run --frozen --project tools/battery-diagnostics python \
  tools/battery-diagnostics/read_split.py
```

Interpret the event sequence rather than a single flag. Sleep/STOP/LED-off
before an outage points toward the power path. Increasing loop ages with
successful peer replies show the split worker is still running. Communication
failure alone cannot distinguish a cable fault from an MCU hang. STOP naturally
creates a long main-loop gap. Logging can alter timing slightly.

## Split getter wire format

Requests are 32-byte VIA payloads `02 81 <page>`, zero-filled. Page 3 alone accepts a big-endian event sequence in bytes 3–6. Replies start `02 81 <page> 47 53 01`; all multibyte integers are big endian. The existing `02 80` battery protocol is unchanged. No clear/freeze/setter command exists.

| Page | Response bytes |
| --- | --- |
| 0 | 6–9 uptime; 10–13 total events; 14 capacity; 15 status; 16 device; 17 radio state; 18 wake cause; 19 pins; 20 left; 21 split-connected flag; 22–25 main-loop age; 26–29 active SPI age |
| 1 | Six u32 values at 6: RPC calls, failures, last success time, SPI starts, completions, maximum completed SPI duration; 30 last RPC command; 31 RPC active |
| 2 | 6–9 local time of last peer sample; 10–13 peer bytes; 14 sample valid |
| 3 | 6–9 requested sequence; 10–13 timestamp; 14 event ID; 15–17 event arguments; 18 record available |

Status bits: 0–1 low-power state (`NORMAL`, `PRESLEEP`, `STOP`, `WAKEUP`); 2 pending manual sleep; 3 A5 LED power; 4 A8 LED power; 5 SPI active; 6 low-battery sleep latch; 7 master role. Pins bits 0–4 correspond to A7, A15, C15, C14, B9. Event IDs are declared in `split_diagnostics.h` and decoded in `read_split.py`. Pages are independent snapshots; missing/overwritten event sequences are explicitly reported rather than silently replaced. The split-connected flag is meaningful on the master.

## Legacy battery getter compatibility

The retained `tools/battery-diagnostics/read_battery.py` reads archived battery
diagnostic images. Current firmware does not implement its `02 80` getters;
an unsupported response does not indicate a keyboard fault.
The tool sends fixed getters only, without settings/reset/flash operations.

## Getter wire format, version 1

Send a 32-byte VIA payload `02 80 <page>` followed by zeros (hidapi prepends report ID 0). Replies start `02 80 <page> 47 42 01`. Integers are big endian. No setters or counter-clear command exist. Pages are independent snapshots, not an atomic multi-page transaction. Original diagnostic builds accept pages 0–2. Capability bit 0 of page 2 byte 30 advertises page 3; bit 1 advertises output-probe pages 4–7; bit 2 identifies synchronous transmission. The reader requests optional pages only when advertised, so older builds remain readable.

| Page | Bytes after the six-byte header |
| --- | --- |
| 0 | Uptime u32; current snapshot (19 bytes); 3 reserved zeros |
| 1 | Last keycode u16; stage u8; press uptime u32; saved snapshot (19 bytes) |
| 2 | Six u32 values: presses, releases, indicator calls, battery draws, charging draws, last battery-draw uptime; capability byte; reserved zero |
| 3 | Frame count u32, overlay-frame count u32, last overlay uptime u32, last overlay kind u8 (1=level, 2=charging), last overlay before-RGB (3 bytes), after-RGB (3 bytes), latest transfer RGB (3 bytes), 4 reserved zeros |
| 4–7 | Per probe phase 0–3: transfer count u32, busy-start count u32, last sample uptime u32, packet length u16, LED 5 RGB (3 bytes), LED 33 RGB (3 bytes), sampled symbols valid u8, left pixels matching pixel 0 u8, previous TX remaining u16, previous RX remaining u16 |

The later archived implementation uses page 4 for normal charging output; pages 5–7 return zero-filled samples for compatibility. The reader retains their old phase labels to support archived optical diagnostic builds. Those later archived builds have no timed optical sequence.

Page 3 measures global LED 33 on the left half only. The last overlay sample survives key release, while the latest transfer color follows subsequent frames. It records the WS2812 buffer immediately before the unmodified driver copies it for transmission, not a voltage or physical light measurement. New battery/charging draw counters count final-frame overlay applications; the advanced-indicator counter can still increment several times per frame.

A snapshot contains active layers u32, default layers u32, flags u16, then nine u8 values: board device, radio device, radio state, battery percent, shared cached percentage (`pov`), RGB mode, brightness, LED flags and GPIO bits. Device 0 is USB, 1–5 are Bluetooth slots, and 6 is 2.4 GHz. The shared percentage is the master's last synchronized value on either half, not necessarily a separate right battery measurement.

Flag bits 0–11: master, left, charging, full, RGB recording, battery request, charging request, battery indicators enabled, inquiry block, white test, RGB enabled, RGB suspended. GPIO bits 0–6: A7 (USB/charging detection), A15 (full), C15 (BT selector), C14 (2.4 GHz selector), A5 and A8 (LED power), B9 (half selection). GPIO values are raw pin reads; output-pin readings do not measure power-rail voltage.

Stages 0–4: board hook entered, consumed by recording, consumed by device handler, battery handler completed, charging handler completed. A zero press count means the initial stage/key snapshot is empty. All counters and millisecond times wrap at 32 bits. Render counters count function calls, which can occur more than once per RGB frame.
