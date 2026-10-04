# Stock battery synchronization and historical repairs

## Current scope — October 3, 2026

The normal firmware uses official QMK 0.34.4 unchanged, with the existing GMK70 board/radio integration derived from uk-taniyama and Hangsheng. The comparison baseline for the battery repair is `c5c24d42d13f424a4c007a325f2579dbd21bca4e`, the integrated build before that work. Relative to that baseline, runtime changes in normal `release`/`default` are Custom(27)/Custom(7) handling/rendering, global `sym_defer_g` debounce increased from 1 to 5 ms, and the separately evidenced October 3 correction that retains STOP for wake cause NONE. The debug distribution adds the RAM recorder and direct-USB bootloader command; see [debug/release](DEBUG-RELEASE.md).

The October 1 sleep-policy experiment was removed on October 2. State assignments still do not clear manual requests, and the preparation hook retains its original ordering. Only the NONE wake filter is restored on October 3, after a physical-device capture showed repeated unclassified STOP returns immediately becoming full wakes. This preserves the earlier board integration, including original VIA JSON compatibility, USB-first master selection, radio report adaptations for official QMK, and the pre-battery sleep/wake integration. It does not revert the whole board to an unmodified public port or establish factory-equivalent power management. The capture, stock evidence and outstanding checks are listed in [hardware validation](HARDWARE-VALIDATION.md).

No 250 ms battery refresh, retry, expiry, global LED flush wrapper, SPI synchronization override, or RGB byte-order override remains. The existing two-second mode/percentage exchange and automatic low-battery warning are unchanged. The normal build has no diagnostic recorder. The previous artifacts and working source are saved in `artifacts/backups/2026-10-02-before-minimal-scope/`.

Reverting the sleep experiment was a scope correction, **not proof that the reported sleep/wake symptom was fixed**. The host sleep tests now cover the captured NONE wake defect and preserve other baseline behavior, including inherited quirks. On October 3, both halves were separately updated with user authorization and exact readback verification; saved mappings were unchanged. The user then confirmed that idle lighting stayed off and a right-half key woke both halves. The new log confirms NONE retains STOP, but records USB/BT1 selection and disconnected radio status rather than paired-host switching. Broader stability checks remain pending; see [hardware evidence and limits](HARDWARE-VALIDATION.md). Diagnostic getter reads themselves do not reset, flash or change settings.

October 2 scope verification compiled the baseline and restored `lowpower.c` with the same real QMK VIA headers, ARM toolchain and CFLAGS. Their object files were byte-identical (SHA-256 `47de2b325b106ecf6f45e30ba7956e1271437635738c81dcde2a50442c357007`); normal-build diagnostic hooks added no instructions to this unit. This historical comparison predates the October 3 NONE correction and isolates the sleep unit, not the whole firmware. Evidence is in `artifacts/analysis/minimal-scope-20261002/` and `artifacts/tests/2026-10-02-minimal-scope-qmk-sleep.txt`.

## Stock event implementation and observations — October 1, 2026

After the rollback below, the user confirmed that both custom keys work locally and requested stock behavior for the missing right-half bar, with no speculative recovery additions.

`stock_battery.c` implements the observed V13/V15 A4/A5 battery and F0/F1 charging press/release messages. Each event changes the local flag and attempts one five-byte split RPC using the current QMK `USER_SYNC_MMS` ID. The receiving half changes its flag; stock response values are retained, including F1's response of 1. Key handlers do not check the result or schedule a retry. The original USB-mode guard applies to both Custom(27) press and release. No extra mode-transition clearing, reconnect recovery, expiry, held-state packet field, or periodic battery refresh is added.

The original bar drawing has moved out of the RGB recorder into this module and takes `pov`, the shared master percentage already updated by the existing two-second `0x55` exchange. It retains the same LED indices, colors and percentage thresholds. This fixes the current source's use of the receiving half's local radio percentage. The advanced indicator callback still performs the drawing; no final-frame wrapper, synchronous SPI or RGB-order override is restored. Charging drawing, sleep/wake policy, 5 ms global debounce, VIA ABI, and recovery code are unchanged by this step.

The last rollback packages are backed up at `artifacts/backups/2026-10-01-before-stock-battery-events/`. Fresh source/binary evidence is in `artifacts/analysis/stock-battery-events-20261001/`. Native tests exercise exact packets, local updates on transport failure, stock USB guarding, receiver acknowledgements, absence of traffic during repeated drawing, release, and both halves' number-row output at battery boundaries. All three QMK variants compiled successfully; host tests, tooling checks, BIN/HEX agreement, source archives and checksums passed. This is not a claimed fix for idle blackout or flashing.

**Hardware confirmation, October 1:** after reporting installation of the latest diagnostic firmware, the user confirmed that Custom(27) displays the battery bar across both halves and Custom(7) displays charging status and returns to normal lighting after release. The battery test selected 2.4 GHz (device 6), then returned to USB without unplugging. The final read-only capture reports both halves in `NORMAL`, both LED-power outputs on, a connected split link, and 154 RPC calls with zero failures. The logger does not record every successful battery event, so the visible indicator results come from the user's observations. Bluetooth battery behavior, charging/full transitions, idle wake, repeated Bluetooth switching, and long-term stability remain unverified in this build.

Captures: `artifacts/tests/2026-10-01-stock-battery-flashed-baseline.txt`, `2026-10-01-stock-battery-both-halves-confirmed.txt`, and `2026-10-01-stock-battery-charging-confirmed.txt`. The distributed diagnostic BIN has SHA-256 `0774087780a8c5078f9254106f143be5078a9d363b0f464470efc6c8f90f8e0c`; the getter identifies protocol 2 but does not independently attest the installed image hash. No firmware or settings were changed during these reads.

## Earlier rollback — October 1, 2026

The following describes the baseline preceding stock event restoration.

At the user's request, Custom(27)/Custom(7) behavior and LED output are restored to the pre-repair source at `c5c24d42d13f424a4c007a325f2579dbd21bca4e`. The purpose is to compare right-half stability without this repository's battery fixes. This does not establish that those fixes caused the intermittent blackout, and it does not recreate the vendor binary.

- Removed the `battery_indicator` policy/adapter, command `0x56`, its 250 ms refresh/retry/expiry logic, and the final `ws2812_flush` overlay.
- Restored the local `rk_bat_req_flag`/`query()` percentage display and the original `im_bat_req_charging_flag` charging path. The existing `0x55` mode/percentage exchange remains at two-second intervals, without battery-key synchronization. Custom(27) remains suppressed in USB mode.
- Removed explicit `WS2812_BYTE_ORDER` and `WS2812_SPI_SYNC` overrides. The pinned upstream driver again uses its default GRB encoding and asynchronous `spiStartSend`, as before the repair. Baseline charging colors/gates are restored rather than corrected independently.
- Removed battery-specific getter, frame, and SPI probes. The separately added split/sleep logger remains diagnostic-only and now uses the existing mode reply; its protocol 2 marks SPI telemetry unavailable.
- Preserved the original VIA JSON and explicit custom keycode values, saved-map compatibility, wireless/flashing recovery work, official QMK pin, and global `sym_defer_g` debounce at 5 ms.

The right-half percentage bar and charging indication may again be missing or incorrect. The pre-rollback packages for all three variants are retained at `artifacts/backups/2026-10-01-before-battery-rollback/`. Build commands do not access or flash the keyboard. Use matching new firmware on both halves for a manual comparison; reloading the JSON is unnecessary.

The baseline LED regression test compiles the real pinned SPI driver to verify asynchronous dispatch, GRB encoding, complete framing, and color updates. Existing tests cover stock mapping and recovery behavior. Hardware stability still requires a manual comparison with the new firmware.

## Historical repair — September 28, 2026

Everything below describes the removed implementation and its earlier tests/results, not current behavior.

These changes repair the missing right-half battery bar, incorrect red/green wire order, and missing charging indication. Explicit battery requests are applied after other lighting. The working RGB order and synchronous transmission tested in the diagnostic build are now shared by all three QMK variants.

**Hardware results:** the user confirmed the complete battery bar across both halves. After correcting RGB wire order and enabling synchronous transmission in the diagnostic image, the user also confirmed all three optical test patterns and the normal red charging overlay before and after the sequence. The physical charging target is the left MO(1) key, global LED 33. The normal release build uses those same settings, with the temporary optical sequence removed. See [the retained observations](CHARGING-OUTPUT-2026-09-28.md).

The factory RGB wire order is established by V13 disassembly and the user's color test. Synchronous transmission restores the observed single-LED output, but the precise mechanism behind the earlier asynchronous failure is not established. The earlier samples showed no busy transfer starts, and the color and encoded buffers are separate; a buffer race or electrical fault has not been proven. The configured 1,040-byte transfer takes approximately 2.8 ms at the nominal SPI clock, during which the keyboard task waits and interrupts remain enabled. Typing latency has not been measured.

## Expected behavior

- **Custom(27), `0x7E1B`:** hold to display one ten-segment battery bar across both number rows in Bluetooth or 2.4 GHz mode. Both halves use the master's radio-reported percentage. Each complete 10% lights another segment, with at least one lit segment; below 30% the bar is red, otherwise green. This is one shared reading, not independent measurements of two batteries.
- **Custom(7), `0x7E07`:** hold to display charging status on global LED 33, declared as matrix position 4,2 (Fn in the saved layout). Charging is red `(80,0,0)`; full is green `(0,80,0)`. Without cable/charging detection it adds no indication. This matches the factory V13/V15 binary evidence.
- Releasing the request restores the underlying animation. If both requests are held, the percentage bar takes priority. USB selection suppresses only the percentage request. Explicit RGB-off, suspend, RGB recording, and the factory white-test mode remain respected.
- The automatic low-battery warning and low-voltage sleep remain separate. The original VIA JSON and custom-key numbers are unchanged.

## Implementation

`battery_indicator.c` owns the display policy and split-state protocol; `battery_indicator_qmk.c` connects it to QMK and the existing split transaction. The board file provides the sensor/mode state and dispatches the original custom keys. Battery drawing no longer lives in the unrelated RGB-recording module.

The existing five-byte `USER_SYNC_MMS` envelope gains command `0x56`. Its four-byte body is version 1, effective display flags, percentage, and zero. The receiver validates all fields and acknowledges with `0x56`; an older peer's zero response is not accepted. State changes are sent with a 50 ms minimum attempt interval, acknowledged state is refreshed every 250 ms, and the peer discards an unrefreshed display after 1,200 ms. Release is retried after a failed transfer. This prevents stale overlays when a release is lost and allows a rebooted peer to recover. Existing mode, sleep, and reset commands keep their format.

Both halves must run the updated build for the shared display. This new protocol does not claim wire compatibility with stock firmware or older project builds. It avoids reusing the factory's incomplete press/release-only commands and carries a coherent percentage with every state update.

The board wraps `ws2812_flush` at link time. It adds the requested overlay after all effects and board indicators, records diagnostic colors when enabled, then calls the unmodified upstream SPI driver. Common configuration enables `WS2812_SPI_SYNC`, so that driver waits for transmission to finish. The wrapper then restores the animation buffer, so incremental effects do not inherit the overlay. No upstream QMK or driver source is patched. The explicit charging request no longer depends on the automatic low-battery blink's temporary suppression flags.

## Validation and installation

Native tests exercise the actual board adapter and final-frame wrapper, using a split-index/driver fixture. Coverage includes both halves at percentage boundaries, differing local radio readings, charging/full/unplugged states, animation restoration, competing overlays, RGB-off/suspend, lost releases, retry throttling, peer expiry/reconnect, malformed packets, unsupported acknowledgements, and 32-bit timer wraparound. Existing selector, right-half USB recovery, radio/report, stock keycode and original-JSON tests remain required.

Build all supported variants with `make build`, `make build-default`, and `make build-diagnostic`. The normal VIA firmware is `artifacts/qmk/via/zuoya_gmk70_via.bin`; its HEX contains the same application payload. Diagnostics use `artifacts/qmk/via_diagnostic/zuoya_gmk70_via_diagnostic.bin`. Each variant includes its exact source archive and checksums. Prior artifact directories were preserved under `artifacts/backups/2026-09-28-before-battery-fixes/`.

The native output test compiles the actual pinned upstream SPI driver and checks its synchronous send path, RGB packet bytes, framing, and sustained charging output without test patterns. Build and test commands never flash or access the keyboard. The successful diagnostic and preceding normal artifacts are preserved under `artifacts/backups/2026-09-28-before-synchronous-release/`. After a separate manual installation of the normal release on both halves, check the complete bar in a wireless mode and charging red while holding Custom(7) with USB power connected. Full green requires the hardware full signal and has not yet been physically confirmed. Check release restores the animation.

The earlier right-half bootloader/master repair remains in place. Intermittent USB enumeration/restarts were observed during prior diagnosis, but their cause was not established; this patch makes no claim to repair a cable or an unproven reset cause. Vial/ZMK implementation remains separate future work.
