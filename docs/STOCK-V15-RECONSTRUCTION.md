# Stock V15 reconstruction

This change reconstructs recovered behavior from the user's `ZUOYA GMK70_via_v15.hex` in the maintained QMK board/radio sources. It is **not a verified complete reproduction of stock**. The keyboard MCU image does not contain the separate Bluetooth/RF module, receiver firmware or ROM bootloader, and stock's complete framework and hardware behavior have not been recovered.

The [2026-10-09 recheck](STOCK-V15-RECHECK.md) reproduced seven additional
power/split mismatches in the current build, including peer PRE pin handling,
clock corrections, mode-sync recovery and complete STOP dispatch. The earlier
bounded comparisons still pass within their narrower scope. All seven recheck
differences are corrected and separately validated in the 2026-10-09 follow-up.

## Public source search

Public GitHub searches on 2026-10-05 found GMK70 sources, but no repository verified to contain the exact V15 application source:

- [PandaKBLab/GMK70-QMK](https://github.com/PandaKBLab/GMK70-QMK/tree/2578314194cb1a17717e945cc401175296d13636): its board files match this repository's original vendor-archive import, `c26b7e7cc36b273ce2d44da6bc5a7db5a28860fe`. It lacks the referenced wireless implementation. Its manufacturer, RGB geometry and other behavior differ from the supplied V15 binary. Matching board blobs include `2862e6b54d38b2a8fd898ee0b6fcd9e874833d5b` (C), `998d141183358baac7dedca434c818aecf27f392` (config) and `cdacd60a2871e2211f11cd6e794fde0014c54a03` (keyboard JSON).
- [uk-taniyama/qmk_zuoya_gmk70](https://github.com/uk-taniyama/qmk_zuoya_gmk70): its README describes a custom integration of the original GMK70 archive, Epomaker Split65 and Hangsheng tri-mode sources; Bluetooth was unverified. It links the vendor archive at `http://www.mathewkb.com/wp-content/uploads/2024/11/ZUOYA-GMK70.7z`. It is useful provenance, but not a confirmed stock V15 source release.
- [hangshengkeji/qmk_firmware](https://github.com/hangshengkeji/qmk_firmware): the public master, hs_111, qmk_v3 and tri-mode branches contain related keyboards/radio code; the searched trees/code did not supply a matching GMK70 V15 source. Their public wireless code is an ancestor/reference for this port.

Searches included `GMK70`, `GMK70_via_v15`, the firmware filename and GMK70-specific source names. Global code search and repository/tree inspection were usable; some repository-search requests timed out. This establishes that an exact public source was **not found**, rather than proving that one cannot exist. Public board fragments alone cannot be treated as the source of the supplied executable.

## Evidence and method

Reference HEX SHA-256: `67818d381cb740721c10dd9d17809da603d729c974aa2017dc9e99ba8a8e9ce0`.

Decoded application SHA-256: `3048ee6390a42f6c81a28df3dc9f032a926fe843d904ea9154a0554dae2af0b0`; 80,220 bytes starting at `0x08000000`.

Baseline repository: `e281b8e334ccc38a4a98517f18e90e4077b3e2c1`. Official QMK remains unmodified at release 0.34.4, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6`.

Stock observations use Capstone disassembly, binary table extraction and bounded Unicorn execution of the actual Thumb routines, with the startup `.data` copied into simulated RAM. Clock, UART, USB/SPI output, selected GPIO inputs and split replies are intercepted or simulated. This is an instruction-level test harness, **not an official or complete keyboard emulator**. The tests do not emulate the RF stack, host, physical interrupts or electrical timing.

Raw investigation artifacts are local under `artifacts/analysis/stock-v15-sleep`, `stock-v15-comparison` and `stock-v15-full-comparison`. They describe the pre-change baseline unless named `reconstruction-*`. Durable extracted tables are in `tests/fixtures/stock-v15-tables.json`; native production-C tests exercise the reconstructed contracts without needing the proprietary HEX.

## Implemented behavior

| Area | Reconstruction and stock evidence |
| --- | --- |
| Radio reports | Keyboard, NKRO, mouse, consumer/system and Fn reports go directly to UART rather than waiting in the command ACK queue. Stock ordinary report entry: `0x080023E8`; related mouse/extra/NKRO routines: `0x08002148`, `0x080021A4`, `0x0800221C`. UART writes are serialized across the main and radio threads. |
| Radio scheduling | A separate high-priority worker processes commands/replies and sleeps five 100 kHz ticks (nominal 50 microseconds). Stock startup/worker: `0x08002ACC`/`0x080026B4`; timer conversion: `0x0800E784`. This is an intended sleep interval, not a measured hardware service cadence. Stack size is adapted for the newer QMK code. |
| ACK handling | Preserve the retry count across timeouts; ordinary commands expire after 51 sends, voltage queries after two. ACK clears retry history. Stock task: `0x080119C0`. The old implementation reset retries on every timeout and could block the queue indefinitely. |
| Connection handling | Apply radio status only when its nonzero dispatched selection context matches the current channel. Pairing inhibits reports; basic wireless key events are rejected while disconnected, without reselecting the radio on each discarded report. Stock board gate: `0x08005420`. |
| Radio identity/startup | BT names 1–3 are GMK70-1/2/3 and 4–5 are WB KB 4/5. Naming packets use their actual length, as captured from stock command dispatch. Startup waits two seconds and sends 55, 57, 70 in that order. |
| Idle sleep | Remove the additional connected-wireless one-minute sleep request. Stock has eight selectors: disabled, 1, 3, 5, 10, 20, 30, 5 minutes; default 7 means five minutes. Track stock board activity separately from QMK's release activity. |
| Sleep preparation and split wake | Recovered ordinary/USB-special/critical classes, held-key gates, zero PRE delay, early split restoration, wake inputs and per-group interrupts. Always send ordinary peer AA with zero body and preserve the peer's own critical flag. See [the detailed power follow-up](STOCK-SLEEP-WAKE-MATCH.md). |
| State assignment/wake | Clear pending manual requests and refresh activity/state timestamps on assignment; request voltage on assignment. Repeated host resumes while already waking leave the wake deadline unchanged. NONE/UART returns retain STOP; ordinary STOP/wake delays remain 200 ms. Stock assignment/host callback: `0x08002890`/`0x08002B2C`. |
| Input defaults | One-ms global deferred debounce; exact stock four-layer keymap and four volume encoder assignments, with only the left encoder configured. Includes transparent Fn encoder press, reserved 7E19, hue/speed direction and legacy RGB keycode bytes. Tables: `0x080127A4` and `0x08012794`. |
| LED output/geometry | Keep asynchronous SPI output; emit R,G,B. Match all 77 points/flags and the matrix lookup, including decorative LEDs and the five unwired zero slots. Stock SPI routines: `0x0800CCA4`/`0x0800CC98`; tables: `0x080133F0`, `0x0801344A`, `0x080134E4`. |
| Secondary lighting | Restore the separate RGBLight engine and its 42 mode numbers. Its six decorative positions share the matrix SPI frame. Custom(9) follows the recovered 11-step cycle and sends peer A6; remove per-key recorder integration. Stock cycle: `0x080050A0`. |
| Lighting controls | VIA matrix brightness uses `(value * 80) >> 8`; effect values above 44 clamp to enabled mode 44. The JSON's “Close All” label does not match stock's actual effect-45 behavior. Restore brightness bound indications at >=64/<=16 and speed floor 51. Stock VIA/setter branches: `0x0800D0E4`, `0x08005C46`–`0x08005CAC`. |
| Indicators/split | Restore A6, AE/AF GUI lock, C1 enable/C2 disable send/receive behavior. Store flip/GUI flags in the stock user word. Caps/GUI and charging/full indicators use intensity 80. Draw the battery bar before overlays. Stock indicator/receiver: `0x08004F34`/`0x080051CC`. |
| VIA/raw HID | Protocol 12, sleep channel 0/value 1, live NKRO command 21, activity refresh/wake prehook, deferred settings reset 0A and deferred bootloader request 0B. The 0B echo sets byte 1 to 1; it is supported in both distributions, including radio delivery. Stock entries: `0x0800306C`, `0x080032A0`, `0x08004964`. |
| Persistence | Translate the QMK NVM interface to stock packing/offsets, fixed VIA marker and default maps. Do not reserve recorder storage. Runtime RGBLight speed is not persisted. Stock matrix write: `0x0800AF08`; key addressing: `0x0800DA14`; validity: `0x08007790`/`0x0800CEDC`. |
| USB metadata and suspend | VID 342D, PID E491, manufacturer HS, product ZUOYA GMK70, bcdDevice 000F. `DEVICE_VER` supplies 000F because the pinned JSON schema cannot express decimal patch component 15. Recovered seven-second ordinary host-suspend gate, separate special class and hidden 7E11–13 controls. Modern QMK still supplies the USB driver/event framework. |

The original VIA definition remains unchanged. Standard QMK rendering/framework code is retained; restoring its tables, defaults and controls does not establish identical animation frames or framework semantics across QMK versions.

## Logical EEPROM layout

| Offset | Data |
| --- | --- |
| 0 | Core enabled marker FEE7; disabled marker FFFF |
| 4 | Keymap configuration word, default 1400 |
| 8–11 | RGBLight word: enable + seven-bit mode, hue, saturation, value; default 1D 00 FF 50 |
| 15–18 | Radio word: selected device, previous wireless device, last BT slot, flag/sleep selector; default 00 01 01 0E before the runtime flag |
| 19–22 | Board user flags: GUI lock bit 2 and flip bit 4 |
| 28–33 | Six RGB Matrix bytes; default 35 00 FF 50 CC FF |
| 34–36 | Fixed VIA marker 26 08 19 |
| 37 | VIA layout options |
| 38–757 | Four 10×9 keymaps, stock VIA byte order |
| 758–773 | Four encoder assignments |
| 774–4095 | Sixteen macros; capacity 3322 bytes |

**Existing project EEPROM layouts are not interchangeable with this one.** The previous project reserved a large per-key recorder area and packed its radio/board settings differently. Export VIA mappings before a later installation and restore appropriate stock-format mappings after initializing defaults. This task does not migrate, reset or inspect a connected keyboard. Physical SPI-flash wear leveling and crash recovery have not been proven compatible with stock, so matching logical offsets is not a raw-flash migration guarantee.

## Verification and remaining work

`make test` runs ASan/UBSan native tests for the actual radio queue/parser, power state machine, held-key counter, stock EEPROM wrappers using the pinned QMK types, secondary lighting cycle/frame routing, VIA behavior, reports, battery, split roles, diagnostics and updater. Repository tests compare all 360 compiled default assignments, eight encoder values, all 77 LED points/flags and the lookup against binary-derived fixtures. `make check-tools` checks formatting, lint, strict typing and tooling tests. Both normal release and debug must build, and exported HEX/BIN payloads must agree.

Validation on 2026-10-05 passed `make test`, `make check-tools` and both normal builds (release: 72,366 bytes; debug: 74,110 bytes). Each build verified its HEX against its BIN. Twelve additional bounded checks of the actual release/debug ARM instructions passed: all 256 brightness values without premature persistence, all 256 effect values, raw-HID protocol/selectors/live NKRO/deferred control, power assignment and repeated resume, complete default keymap/encoder/RGB tables and USB identity, and GUI/matrix peer commands. These checks simulate EEPROM, clocks and external I/O; debug RPC logging is intercepted at the I/O boundary. Logs and the local execution harness are in `artifacts/analysis/stock-v15-full-comparison/reconstruction-*` and `verify_reconstruction_elf.py`.

The official locked compiler download redirected to a host blocked by the cloud network policy. Validation uses checksum-verified xPack ARM GCC 14.2.1-1.1 (the same GCC version, a different distribution); the dependency lock remains unchanged. Actual compiler information is recorded in each build's `build-info.txt`. This is not a claim that the locked official compiler archive was used.

Remaining equivalence gaps include:

- The complete stock QMK version, HID descriptor/framework processing, every tap/hold/mouse-key/encoder sequence, animation math and timing have not been reconstructed.
- The recovered power classes, board GPIO/IRQ contracts and USB request gates are implemented in the [power follow-up](STOCK-SLEEP-WAKE-MATCH.md). Physical STOP, electrical timing and the modern USB driver/event delivery still require hardware tests. Old private key-record buffer reset semantics in USB-special entry remain unreproduced.
- Precise charging/state transitions and every overlapping channel/pairing/reset sequence need further differential and hardware tests.
- Link-level interoperability with a stock half is unproven: stock registers custom transaction 15, while current QMK derives transaction IDs/layout from its enabled features. Installations need a consistent firmware pair, not an assumed mix-and-match split protocol.
- Hidden manufacturing/test paths and all raw-HID commands are not comprehensively recovered. Reserved custom slots are not assigned speculative functions.
- Debug diagnostics and the opt-in resident split updater are repository additions. The normal release contains no diagnostic recorder; the opt-in updater is not the supplied stock application layout.
- Continuous-typing Bluetooth interruptions require UART, split and power traces on hardware to confirm the actual cause. The queue defect and sleep differences were reproduced in software, but that does not prove which path caused the observed simultaneous one-second blackout.

No connected-device writes, resets, flashes or physical tests are part of this change. “Exactly stock” remains a verification goal, not a supported claim for these builds.
