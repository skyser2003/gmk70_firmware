# Live battery diagnostics, 2026-09-28

The user installed the diagnostic firmware on both halves, connected them, and confirmed normal typing. The direct left USB interface returned diagnostic protocol 1. Only fixed diagnostic getters and six VIA keymap getters were sent during this investigation. No settings, mappings, resets or firmware writes were performed by the analysis tools.

## Findings

### Custom(27): left battery bar works; right request is missing

With the physical selector in 2.4 GHz mode, the user held left Fn+B and reported a visible battery bar on the left number row. The right half continued its normal lighting.

This matches a specific source limitation. `HS_BATQ` / `0x7E1B` sets only the master's local `rk_bat_req_flag`. `query()` draws the bar when that local flag is set. The split slave dispatcher has no corresponding request/release messages, so the right half does not enter battery-display mode. The factory firmware does propagate these requests, as documented in [BATTERY-DIAGNOSTICS.md](BATTERY-DIAGNOSTICS.md). The gap also existed in the earlier project builds; see [the historical comparison](BATTERY-REGRESSION-COMPARISON.md).

The complete source path explains why a ten-key bar still lights only the left side:

1. In `gmk70.c`, `HS_BATQ` assigns `(confinfo.devs != DEVS_USB) && record->event.pressed` to the local request flag. That case sends no split transaction.
2. In `rgb_record.c`, `query()` runs the bar only while that flag is true. Its configured indices are `{5, 6, 7, 8, 9, 39, 40, 41, 42, 43}`: five LEDs on each half.
3. The board declares `RGB_MATRIX_SPLIT` as `{36, 41}`. Upstream `rgb_matrix_led_index()` accepts indices below 36 on the left and returns `-1` for the right-side indices. `rgb_matrix_set_color()` ignores that negative result. These calls do not send individual LED colors to the other MCU.
4. The right half renders its own LEDs, but its separately allocated request flag remains false. Its `query()` therefore does nothing, leaving its ordinary animation visible. Flashing the same firmware to both halves does not share that RAM flag.
5. The existing command `0x55` sends connection modes and a percentage every two seconds. Its receiver stores the percentage in `pov`; it does not set the request flag. Factory A4/A5 battery-request messages are missing from both the project's sender and receiver.

There is also a value-selection detail to address in a complete correction: `query()` reads local `md_getp_bat()`, not the received `pov`. Merely enabling the right-side flag does not ensure that both portions use the same percentage. The display state and intended percentage must be handled together, with release clearing the display on both halves. These findings explain the current right-half failure without attributing the earlier unidentified build's missing left bar to the same cause.

The wireless press could not be recovered from RAM afterward: the first saved USB baseline had uptime 110,390 ms, while the next successful read had uptime 27,192 ms, empty press history and reset counters. This is consistent with a reboot between reads; its cause was not established. The left-bar finding therefore rests on the user's visual confirmation, not a claimed retained wireless trace. The earlier total absence of the left bar on an unidentified older firmware is not reproduced on this diagnostic build.

### Custom(7): key handling and charging draw attempts work; visible output is unresolved

Two subsequent wired-mode tests retained their diagnostic history. The user reported no visible change, including when specifically watching the Fn-position LED.

| Reading | First charging test | Second charging test |
| --- | ---: | ---: |
| Read uptime | 170,917 ms | 465,177 ms |
| Last tracked keycode | `0x7E07` | `0x7E07` |
| Last press uptime | 72,761 ms | 293,188 ms |
| Last handler stage | `charging_handler_completed` | `charging_handler_completed` |
| Active layer mask at press | `0x00000002` | `0x00000002` |
| Charging detected at press | Yes | Yes |
| Charging request at press | Yes | Yes |
| RGB enabled / suspended at press | Yes / No | Yes / No |
| RGB recording / inquiry block / white test | All false | All false |
| Charging draw calls since restart | 3,235 | 6,725 |
| Tracked presses / releases since restart | 6 / 6 | 10 / 10 |

The additional test produced 3,490 additional charging draw attempts. This rules out the tested key being ignored, the wrong custom-key number, a wrong active layer for this binding, absent charging detection, and interception by RGB recording for the recorded presses. The draw counter is incremented inside `bat_indicators()` after its master check and only when charging and the request flag are both true. It is not a measurement of emitted light or a completed LED transfer.

The final normal state has both request flags cleared after release. It reports RGB mode 13, brightness 80, all LED flags enabled, both LED-enable GPIOs high, and no RGB suspend. The reported battery value of 100 is firmware telemetry, not an independently verified capacity measurement.

## Charging LED identity and compiled code

The source targets LED 33, whose declared matrix coordinate is row 4, column 2. Live VIA getters returned:

| Layer | Matrix coordinate | Stored keycode |
| --- | --- | --- |
| 0 | 4,0 | `0x7E1F` |
| 0 | 4,1 | `0x00E0`, Left Ctrl |
| 0 | 4,2 | `0x5221`, momentary layer 1 / Fn |
| 0 | 4,3 | `0x00E2`, Left Alt |
| 1 | 4,5 | `0x7E07`, charging request |
| 1 | 3,6 | `0x7E1B`, battery percentage request |

Thus the declared target is the Fn-position key between Left Ctrl and Left Alt in this saved layout. The initial instruction to watch Left Win was inaccurate; the user subsequently checked the corrected Fn position and still reported the normal animation. The matrix declaration does not independently establish the PCB's physical LED-chain order.

Static inspection of the retained diagnostic ELF confirms that `bat_indicators()` at `0x0800252C` calls the charging draw counter and then selects red or green. With charging true and full false, it calls `rgb_matrix_set_color(33, 0, 255, 0)`. The compiled split LED-index function accepts index 33 on the left half, below its boundary of 36. The source uses the WS2812 driver for that write.

This narrows the remaining charging problem to the actual LED target/output path. This diagnostic protocol does not expose the final LED color buffer or transfer completion. It cannot prove whether a later write replaces the color, whether the declared LED position differs from the physical wiring, or whether another output-path problem prevents the expected light. No one of those possibilities is established as the cause.

### Factory charging colors differ from the current source

A subsequent check of the retained factory V13 disassembly establishes the normal charging indication: while Custom(7) is held, charging selects red on LED 33; full selects green. Release clears the request and returns that LED to ordinary rendering. The charging indication requires cable/charging status and does not show the ten-key percentage bar. The factory low-battery blink is a separate automatic condition.

The factory code reads GPIO A15 into byte `0x2000427E` at `0x08004C5C`–`0x08004C6A`. With cable status set, A15 high selects radio command `0x66` (charging done) at `0x08004CA8`. The indicator-state updater at `0x08004998` sets state 2 for that high/full condition and state 1 otherwise. In the RGB callback, state 1 plus the Custom(7) request calls the color function with `(33, 80, 0, 0)` at `0x08004EBC`–`0x08004EC4`; state 2 selects `(33, 0, 80, 0)` through `0x08004F80`–`0x08004F8E`. Literal-pool addresses were treated as data. The corresponding indicator instructions in factory V15 were already verified unchanged after relocation in the official-firmware comparison.

Our current `bat_indicators()` does the opposite: full selects `(33, 255, 0, 0)`, and charging/not-full selects `(33, 0, 255, 0)`. Its colors and fixed channel intensity therefore differ from factory behavior. Earlier instructions to expect green during charging described this project's current implementation, not the factory convention. Correcting the colors alone would still not explain or resolve the user's report that the target keeps its normal animation.

## Next changes justified by the evidence

These recommendations led to [the subsequent battery repairs](BATTERY-FIXES.md). The observations in this report are retained as evidence from the earlier diagnostic build.

1. Propagate the held battery-display state to the other half, including release and recovery from a missed update. Use a consistent battery value for the two portions of the bar. This addresses the confirmed right-half display gap.
2. For the charging indication, trace LED 33 immediately after the charging write, at the end of the indicator callback, and immediately before the driver transfers the frame. Compare that with a physical LED identification test. Counting calls earlier in the routine is no longer sufficient.

The existing diagnostic getter protocol cannot perform those additional observations, so they would require additional instrumentation and a separate manual installation. No firmware behavior was changed for this investigation.

## Evidence and validation

Evidence is retained under `artifacts/analysis/battery-live-20260928/`. Successful records are `01-usb-baseline.txt`, `04-wireless-battery-retained.txt`, `06-usb-charging-retry.txt`, `07-keymap-getters.txt`, and `09-final-charging-readback.txt`. The name of record 04 describes the attempted recovery of the wireless history; its actual contents show that history was cleared. Records 02, 03, 05 and 08 contain no diagnostic payload because the reader could not find a matching USB interface at those attempts. Subsequent reads succeeded. These intermittent failures do not establish that a cable was defective.

`charging-disassembly.txt` contains the compiled charging and LED-index paths. The diagnostic reader's four offline tests pass. Its first test invocation from the repository root could not import `read_battery`; rerunning from the documented tool directory passed. No firmware source was edited, so no new firmware build was required.
