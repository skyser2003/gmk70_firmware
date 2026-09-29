# Battery indicator diagnosis

**Repair update:** [Battery indicator repairs](BATTERY-FIXES.md) describes the subsequent split synchronization, factory RGB order, final-frame overlay, and synchronous LED transmission. The user confirmed the complete battery bar on both halves and the red charging indication on left MO(1). The temporary optical sequence has been removed; the current diagnostic build observes normal output without changing it. [The output investigation](CHARGING-OUTPUT-2026-09-28.md) retains the test results. The historical investigation below describes the preceding firmware.

The live tests on September 24–25 have not established why Custom(27) produces no battery bar. RGB was enabled, both `0x7E1B` and the historical `0x7E05` were tested in wireless mode, and direct USB matrix capture confirmed the held Fn+B switches. Windows reported neither B nor Ctrl during the confirmed hold, making the wrong Mac-layer explanation unlikely. Direct USB identifies the installed firmware as an older VIA protocol-12 build, not the currently maintained QMK 0.34.4 build. The exact older binary remains unknown.

**September 28 update:** the user installed the diagnostic build on both halves. The left battery bar now appears in 2.4 GHz mode, while the right remains in normal lighting, matching the missing split-request handling. Custom(7) is confirmed to reach its handler with charging detected and to execute thousands of charging draw attempts, but the user still sees no change at the declared target. The saved mapping identifies that target, LED 33 / matrix 4,2, as Fn in this layout. The final RGB buffer is not exposed by the current diagnostics, so the charging-output cause remains unresolved. See [the live findings and raw-readback references](BATTERY-LIVE-2026-09-28.md).

Custom(7) is a charging-status request, not a second battery-percentage display. In the checked source it only selects a charging indication while a charging cable is detected. Custom(27) deliberately suppresses its percentage display when the selected connection is USB. USB observations therefore verify key handling, not the reported wireless display failure.

## Separate diagnostic build

`make build-diagnostic` builds `artifacts/qmk/via_diagnostic/zuoya_gmk70_via_diagnostic.{bin,hex}` from the current official-QMK pin and shared board sources. It retains the original JSON, numeric keycodes, shared default map and normal lighting behavior. No diagnostic code is enabled in the normal `via` or `default` variants. The build does not flash or access the keyboard.

This instruments the current sources; it does not reconstruct the unidentified older binary. Moving from the older framework to QMK 0.34.4 can itself change behavior and resets the incompatible EEPROM layout. Preserve the current VIA export and follow [the migration notes](QMK-0.34-MIGRATION.md) before any manual installation. Existing getter-only analysis snapshots contain the layers, but are not complete VIA backups of macros and encoders. No manual installation is required merely to inspect this change.

For an initial manual hardware test, use the left half alone with the inter-half cable disconnected, so the test does not depend on a right half running a different QMK version. The observed saved map mixes stock Windows custom-key numbers with older Mac-layer numbers; do not apply the legacy converter blindly to the entire export. Check the restored test key against `0x7E1B` and the actual Fn position after any EEPROM initialization.

Instrumentation records only `0x7E1B`, `0x7E07` and `0x7E05`. It stores the most recent tracked press, handler outcome, active/default layers, connection mode, RGB state, request flags and GPIO readings in RAM. It also counts indicator callbacks and attempted battery/charging draws. Getters do not clear history, change mappings, reset the keyboard or write EEPROM. Ordinary typing is not recorded. History is local to each half, is not split-synchronized, and disappears on power loss or reboot.

If manually installed later, test one battery key at a time in wireless mode, then return to USB **without power cycling** and read the saved press. Reading USB state alone does not reproduce wireless state; use the saved press snapshot. Capture counter readings before and after each test because counters accumulate. Device-selection key `0x7E05` is also tracked and replaces the saved battery press, so avoid it between the battery test and the readback. The physical connection selector does not replace the saved key press. If returning to USB resets the MCU, the history is lost and a read while still powered in wireless mode is needed.

## Read-only host tool

On Windows with `uv`, from the repository root:

```powershell
$env:UV_PROJECT_ENVIRONMENT = Join-Path (Get-Location) '.work\venvs\battery-diagnostics-windows'
uv run --frozen --project tools/battery-diagnostics python tools/battery-diagnostics/read_battery.py
```

The separate Windows environment avoids overwriting the Linux environment used by WSL's tooling checks. Connect one half directly by USB. The reader rejects the receiver interface and multiple direct halves. It sends three fixed getter requests, plus advertised final-buffer/output-probe getters, reports readable state and raw replies, and closes the handle. It does not poll keystrokes, change settings or expose serial numbers. Running it on the existing normal/older firmware returns an unsupported-response error; that does not indicate a keyboard fault. Linux requires hidapi's native prerequisites and USB permissions; WSL requires explicit USB access. Firmware compilation remains Linux/WSL as documented in the main README.

The host tool has its own uv environment. Its offline tests use a fake connection and do not open HID devices:

```sh
cd tools/battery-diagnostics
uv run --frozen ruff check .
uv run --frozen ruff format --check .
uv run --frozen mypy --strict read_battery.py tests
uv run --frozen python -m unittest discover -s tests -v
```

## Interpreting results

- No tracked press: the tested key did not reach the board hook with one of the three monitored keycodes. Check the actual saved layer/key assignment.
- `consumed_by_RGB_recording`: the recording handler intercepted the key before battery handling.
- `battery_handler_completed`, USB device 0 and no battery request: the board's USB-mode guard suppressed the request. Compare board mode, radio mode and selector pins.
- Battery request present but no increase in battery draw calls: rendering did not reach the battery bar. Inspect RGB enable/suspend, recording and white-test state, then indicator-call counts.
- Battery draw calls increase but nothing is visible: the handler and draw routine ran. Inspect LED power, driver output and the relevant half; the counter proves an attempted draw, not light emitted or a completed LED transfer.
- Charging request while `charging=False`: Custom(7) has no special charging indication in that state by design.

The existing bar also depends on RGB being enabled and does not synchronize its request across both halves. Those are separate source limitations; neither alone explains the user's completely unchanged left-half lighting with RGB on.

### Stock binary evidence for split synchronization

The retained factory V13 binary (SHA-256 `857a669c1fc739cd18ccd9a5526e80db10b150fbdea344b36031105e72e661f3`) does propagate these requests. Its `0x7E1B` handler at `0x08005B96` sets/clears local byte `0x20001494` and sends command `0xA4`/`0xA5` through the split RPC function at `0x080084D0`, using transaction 15 and a five-byte request. Its `0x7E07` handler at `0x08005BCE` similarly uses byte `0x20001493` and command `0xF0`/`0xF1`.

The slave dispatcher at `0x08005114` has jump-table entries for A4/A5 leading to `0x0800527E`/`0x08005286`, which set/clear the battery flag; F0/F1 branches set/clear the charging flag. The RGB callback reads those same flags through literals at `0x08004FC8` and `0x08004FD0`. In the current port, the handlers only write the local flags and the slave dispatcher has no corresponding A4/A5/F0/F1 cases. This is a verified missing part of factory cross-half behavior, not proof of the live left-half failure's cause. Jump-table and literal-pool bytes are data, not Thumb instructions.

The [September 27 comparison with the earlier project builds](BATTERY-REGRESSION-COMPARISON.md) further narrows this finding: those split commands were also absent from the builds with broken right-half recovery, where the user reports that battery indication worked. The recovery fix preserved the battery handlers and drawing routines. Missing split commands therefore predate the reported regression and are not a sufficient explanation for it.

## Getter wire format, version 1

Send a 32-byte VIA payload `02 80 <page>` followed by zeros (hidapi prepends report ID 0). Replies start `02 80 <page> 47 42 01`. Integers are big endian. No setters or counter-clear command exist. Pages are independent snapshots, not an atomic multi-page transaction. Original diagnostic builds accept pages 0–2. Capability bit 0 of page 2 byte 30 advertises page 3; bit 1 advertises output-probe pages 4–7; bit 2 identifies synchronous transmission. The reader requests optional pages only when advertised, so older builds remain readable.

| Page | Bytes after the six-byte header |
| --- | --- |
| 0 | Uptime u32; current snapshot (19 bytes); 3 reserved zeros |
| 1 | Last keycode u16; stage u8; press uptime u32; saved snapshot (19 bytes) |
| 2 | Six u32 values: presses, releases, indicator calls, battery draws, charging draws, last battery-draw uptime; capability byte; reserved zero |
| 3 | Frame count u32, overlay-frame count u32, last overlay uptime u32, last overlay kind u8 (1=level, 2=charging), last overlay before-RGB (3 bytes), after-RGB (3 bytes), latest transfer RGB (3 bytes), 4 reserved zeros |
| 4–7 | Per probe phase 0–3: transfer count u32, busy-start count u32, last sample uptime u32, packet length u16, LED 5 RGB (3 bytes), LED 33 RGB (3 bytes), sampled symbols valid u8, left pixels matching pixel 0 u8, previous TX remaining u16, previous RX remaining u16 |

The current build uses page 4 for normal charging output; pages 5–7 return zero-filled samples for compatibility. The reader retains their old phase labels to support archived optical diagnostic builds. There is no timed sequence in current firmware.

Page 3 measures global LED 33 on the left half only. The last overlay sample survives key release, while the latest transfer color follows subsequent frames. It records the WS2812 buffer immediately before the unmodified driver copies it for transmission, not a voltage or physical light measurement. New battery/charging draw counters count final-frame overlay applications; the advanced-indicator counter can still increment several times per frame.

A snapshot contains active layers u32, default layers u32, flags u16, then nine u8 values: board device, radio device, radio state, battery percent, shared cached percentage (`pov`), RGB mode, brightness, LED flags and GPIO bits. Device 0 is USB, 1–5 are Bluetooth slots, and 6 is 2.4 GHz. The shared percentage is the master's last synchronized value on either half, not necessarily a separate right battery measurement.

Flag bits 0–11: master, left, charging, full, RGB recording, battery request, charging request, battery indicators enabled, inquiry block, white test, RGB enabled, RGB suspended. GPIO bits 0–6: A7 (USB/charging detection), A15 (full), C15 (BT selector), C14 (2.4 GHz selector), A5 and A8 (LED power), B9 (half selection). GPIO values are raw pin reads; output-pin readings do not measure power-rail voltage.

Stages 0–4: board hook entered, consumed by recording, consumed by device handler, battery handler completed, charging handler completed. A zero press count means the initial stage/key snapshot is empty. All counters and millisecond times wrap at 32 bits. Render counters count function calls, which can occur more than once per RGB frame.
