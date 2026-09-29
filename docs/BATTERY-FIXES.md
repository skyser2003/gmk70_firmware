# Battery indicator repairs — September 28, 2026

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
