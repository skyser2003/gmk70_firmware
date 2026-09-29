# Custom(7): output investigation

Custom(7)'s red charging indication now works in the RGB-order-corrected, synchronous diagnostic. The user confirmed the left MO(1) key stays red during normal charging-overlay stages. Custom(27)'s complete bar was already confirmed on both halves.

**Release update:** the working RGB wire order and synchronous transmission are now common settings for VIA, default and diagnostic builds. The temporary optical sequence is removed from current firmware. Read-only output sampling remains in the diagnostic variant. The history below distinguishes observed recovery from the still-unproven mechanism behind the earlier asynchronous failure.

## Observations with the previous diagnostic image

The left half was manually flashed with diagnostic BIN SHA-256 `97891b167ce0191cacdacd9553f2aec2d1db87779a10f5c140c63088a7c838e5`. The right half retained the working normal VIA image. Read-only captures are under `artifacts/analysis/charging-followup-20260928/`.

- `01-after-hold.txt`: Custom(7), `0x7E07`, reached the charging handler. USB detection was true, full was false, RGB enabled, and no recording/suspend/white-test block was present. The final WS2812 array contained LED 33 red `(80,0,0)` for 477 overlay frames. The latest animation color after release was different.
- The user then examined the entire left half, including knob and bottom row, while holding the request and reported that all LEDs kept animating.
- `02-after-full-left-check.txt`: six tracked presses/releases, 1,892 charging frames, last charging frame at uptime 157,139 ms. LED 33 again changed from the animation `(18,17,17)` to `(80,0,0)`. At the last captured press the selected RGB mode was 1. These counters accumulate; they do not measure emitted light.

Disassembly of that exact ELF confirms that the final-frame wrapper calls the real encoder before restoring the animation buffer. The encoder reads the same array at `0x20004CC5` and copies it to a separate 1,040-byte SPI buffer at `0x200033C7`; the restored animation array does not alias that buffer. The WB32 driver sends through interrupt-driven FIFO refills. Static inspection has not established a transfer failure, a physical LED mapping error, or a hardware fault.

Factory V13 calls its RGB setter with decimal index 33 for the charging indicator. Its left-half setter accepts that local index without a further remap. The public board definition also uses index 33. That is source/binary evidence for the intended index, not proof of where that index lights on this particular board.

## Archived optical probe sequence

The experimental `via_diagnostic` builds added one sequence per continuous Custom(7) hold, while USB charging detection and RGB were enabled, on the left half only:

| Time held | Left-half output |
| --- | --- |
| 0–3 seconds | Normal charging overlay |
| 3–6 seconds | All left LEDs solid red |
| 6–9 seconds | Black background, only LED 5 white (number-row control) |
| 9–12 seconds | Black background, only LED 33 white (charging target) |
| After 12 seconds | Normal charging overlay until release |

Release immediately restores the animation. It does not loop, save settings, or run on the right half. It respects RGB off, suspend, recording and white-test restrictions. A simultaneous Custom(27) bar takes priority and stops the probe. The normal `via` and `default` variants do not include the probe or its SPI wrapper.

The diagnostic SPI wrapper observes the real driver's encoded buffer immediately before the send, then forwards the same driver, length and pointer. It retains one sample/counters per phase, including decoded LED 5/33 RGB, packet length, count of the first 36 pixels matching pixel 0, and whether a transfer was requested before the preceding transfer became ready. Remaining TX/RX counts are observational readings and are not an atomic hardware snapshot. Successful encoding still does not prove that the waveform reached the LEDs. The first probe used asynchronous `spiStartSend`; the follow-up described below uses QMK's supported synchronous `spiSend` option in the diagnostic variant only.

Only the diagnostic artifact is needed on the left half for this experiment. Keep USB power attached after the hold so the read-only tool can retrieve RAM history. Manual flashing remains separate from builds/tests; no host tool performs it. Older diagnostic artifacts are preserved under `artifacts/backups/2026-09-28-before-output-probe/via_diagnostic/`.

## Validation

The dedicated native output test compiles the pinned upstream `ws2812_spi.c` unchanged. The current test verifies synchronous sending, actual encoded bytes for the red target, packet framing, sustained charging with no timed patterns, release and repeat holds, timer wraparound, right-half exclusion, malformed samples, and unmodified transfer forwarding. The archived source contains the earlier optical-stage tests. Existing split/battery, keycode, radio and recovery tests remain required, along with strict Python checks and affected firmware builds.

## First optical probe result

The tested diagnostic BIN was 70,036 bytes, SHA-256 `b3a379b08d32b818d2823f493b1c80b0a8988166b35b8b55f783987e1bb00cbf`. Its exact artifacts are retained under `artifacts/backups/2026-09-28-before-rgb-order-fix/via_diagnostic/`. The read-only result is `artifacts/analysis/charging-followup-20260928/03-optical-probe-result.txt`.

| Phase | Captured transfers | LED 5 logical RGB | LED 33 logical RGB | Left pixels matching pixel 0 |
| --- | ---: | --- | --- | ---: |
| Normal charge | 1,105 | (2,0,18) | (80,0,0) | 10 |
| All red | 154 | (80,0,0) | (80,0,0) | 36 |
| LED 5 white | 153 | (80,80,80) | (0,0,0) | 35 |
| LED 33 white | 154 | (0,0,0) | (80,80,80) | 35 |

All packets were 1,040 bytes and sampled symbols were valid. Every phase had zero starts while busy, with zero previous TX/RX remaining at its last sample. The last tracked press began at 9,139 ms; the single-LED stages continued through 21,134 ms, followed by normal charging frames through 39,969 ms. The missing stages therefore cannot be explained simply by releasing the key before six seconds. The user's clarification was that the left half returned to normal animation, not that it stayed green or became mostly dark.

## Confirmed RGB order difference and follow-up

In factory V13, `0x0800CA44` stores the RGB setter's red argument at pixel offset 0, green at 1 and blue at 2. The encoder at `0x0800CBBC` sends offsets 0, 1, 2 in that order, four encoded bytes per channel. Its symbol encoder at `0x0800CB58` uses the same 8/E zero/one nibbles as current QMK. The factory wire order is therefore RGB.

The migrated board omitted `WS2812_BYTE_ORDER`, so official QMK defaulted to GRB. A logical red sample `(80,0,0)` therefore transmitted channel bytes `(0,80,0)` before bit encoding. That is consistent with the user seeing green. The board now explicitly selects `WS2812_BYTE_ORDER_RGB`, and the native test checks the actual upstream encoder's first channel bytes against an RGB golden packet. This correction applies to all lighting colors in all three QMK variants. Upstream source remains unchanged.

Color order cannot explain the missing black/white single-LED stages, because equal-channel white and black are invariant under channel permutations. The follow-up experiment enabled `WS2812_SPI_SYNC` only in `via_diagnostic`: the unchanged QMK driver waited for transfer completion before the final-frame wrapper restored the animation buffer. Existing `SPI_USE_WAIT` support was already enabled. Normal variants still used asynchronous sending at that point. The earlier busy-start counters were all zero, so a busy transfer restart was not established.

The synchronous diagnostic used the same visual sequence. It can pause the keyboard task for approximately 2.8 ms per LED frame at the configured nominal SPI clock. The getter advertises synchronous transmission with capability bit 2. Both driver paths were compiled in the native regression tests before the physical experiment.

## Successful synchronous result and promotion

The tested BIN was 70,020 bytes, SHA-256 `b13a21f0e60619de056b6617b30108755fa5f730b2b4ca45f3877b85380c07c9`. Its complete artifacts and source archive are preserved under `artifacts/backups/2026-09-28-before-synchronous-release/via_diagnostic/`. The read-only capture is `artifacts/analysis/charging-followup-20260928/04-synchronous-probe-result.txt`.

The user reported all left LEDs red at 3–6 seconds, number 1 white at 6–9 seconds, and left MO(1) white at 9–12 seconds. They separately confirmed MO(1) stayed red during the normal charging-overlay stages before/after the sequence. This physically identifies LED 5 as number 1 and LED 33 as left MO(1).

| Phase | Captured transfers | LED 5 RGB | LED 33 RGB | Left pixels matching pixel 0 |
| --- | ---: | --- | --- | ---: |
| Normal charge | 1,214 | (0,8,18) | (80,0,0) | 10 |
| All red | 323 | (80,0,0) | (80,0,0) | 36 |
| LED 5 white | 320 | (80,80,80) | (0,0,0) | 35 |
| LED 33 white | 317 | (0,0,0) | (80,80,80) | 35 |

Every sample had 1,040 bytes, valid symbols, zero busy starts, and zero previous TX/RX remaining. Charging detection was true and full detection false. The last normal charging sample contained LED 33 `(80,0,0)`; the later released frame contained animation `(17,0,18)`.

Common board configuration now enables synchronous sending. Diagnostic code no longer writes test patterns or tracks elapsed hold stages; it only observes normal charging transfers. Legacy optical pages return zero-filled samples. The upstream dependency remains unchanged.

The hardware observation supports this working configuration, but does not establish a precise interrupt/FIFO/electrical failure in the old path. No alias exists between the restored color array and the encoded SPI buffer. Full-charge green still requires a physical full signal and has not been observed in this test; its policy is covered by native tests. The newly packaged normal build still needs the user's separate manual installation and final hardware check.
