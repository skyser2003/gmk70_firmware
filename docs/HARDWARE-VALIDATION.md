# Hardware validation

The binaries are build candidates. Compilation, host tests, and ELF inspection do not validate the physical radio, matrix wiring, charging signals, or low-power timing. Build and test commands do not flash a device.

Record PCB revision, stock keyboard version, receiver version, host OS, and the candidate SHA-256 before a device test. Preserve the known-good original image. The external radio and receiver retain their existing firmware; this build only targets the WB32 keyboard MCU. Similarity to other keyboards is not proof that firmware is interchangeable across revisions.

## Acceptance tests

| Area | Required result | Status |
| --- | --- | --- |
| USB enumeration | VID 342D, PID E491; reconnect works | Pending |
| Matrix | Every key on both halves, including both B keys and both spaces | Pending |
| Encoder | Volume down/up and mute; both Windows and Mac layers | Pending |
| Layers | Fn, Mac/Windows switching, media/function flip | Pending |
| Rollover | Modifiers and simultaneous keys; releasing in different orders leaves no stuck keys | Pending |
| VIA | Load provided JSON; change keys on all four layers; survive unplug/replug | Pending |
| RGB | On/off, brightness, effects, recording channels, indicators on both halves | Pending |
| Bluetooth | Pair all three slots with Fn+Q/W/E held for 3 seconds; reconnect and switch slots | Pending |
| 2.4 GHz | Pair with Fn+R held for 3 seconds; type through the existing receiver | Pending |
| Mode switch | USB/BT/2.4 GHz transitions while keys are held; no stale reports | Pending |
| Radio raw HID | VIA through receiver if supported by its firmware; not assumed over Bluetooth | Pending |
| Wired idle | More than 5 minutes idle, then first key from either half works | Pending |
| Wireless idle | 1 minute idle, then wake separately from each half; no lost/stuck keys | October 3: user confirmed idle stayed dark and a right-half key woke both halves with Bluetooth selected and USB power attached. Capture verifies NONE no longer triggers WAKEUP; cached radio status remained disconnected. Connected-idle, left-key wake, 2.4 GHz and longer runs pending |
| Long idle | Radio's 30-minute sleep and host suspend/resume | Pending |
| Battery/charging | Plausible level, low-battery indication, charging transitions | October 1: user confirmed Custom(27) bar across both halves in 2.4 GHz and Custom(7) hold/release charging indication with USB power. Percentage accuracy, Bluetooth, low battery and charging/full transitions pending; see BATTERY-FIXES.md |
| Recovery | Existing WB32 DFU recovery remains accessible | Vendor pad procedure confirmed September 24. October 3: debug USB bootloader entry, update, exact firmware readback and application restart confirmed separately on both halves; all 360 matrix assignments per half preserved |

The original port reports a right-half idle problem. This fork fixes specific source defects in that path, including USB slave timeout, split pin shutdown before RPC completion, missing split-RX wake event, wrong sleep column pins on the right, an extra five zero-initialized row pins, and a pending-interrupt bitmask typo. Wake behavior on both halves still requires physical testing; it is not established by these changes.

## October 16 right-key wake candidate

The user reports that, during connected Bluetooth idle sleep, a right-side key
wakes only the right half. A left-side key then restores both halves and typing.
VIA and Vial share the board wake implementation; their relevant split protocol,
WB32 serial/GPIO drivers and compiled UART restoration callbacks match.

The shared wake callback now emits a single 100-microsecond low pulse on split
TX A9 after a local row/encoder wake on a wireless non-master in ordinary WAKEUP.
Restoring UART mode releases TX high, ensuring a transition for the sleeping
master's A10 wake input even when the UART retained its idle-high level.
The pulse is skipped if a split-RX edge was observed, preventing an echo of a
peer-triggered wake. The request is consumed once and cleared before each STOP
arming. Master, USB, special-class and normal-operation callbacks do not pulse.

The native regression compiles the actual board wake callback and IRQ hooks
against simulated retained-idle-high GPIO. It fails against the preceding
callback and passes with the pulse, covering every row, encoder wake, wireless
selections, repeated cycles and the suppression gates. This is a software model,
not a physical STOP/line-timing measurement. Confirm right-only wake after
connected Bluetooth idle on matching applications on both halves, then repeat
left-key wake and 2.4 GHz checks. Physical confirmation is pending.

## October 13 Vial Bluetooth indicator routing

The user reports continuous right-side P flashing on BT1 and left-bracket
flashing on BT2 while Bluetooth typing works. Stock global LED indices 14/13
are Q/W on the left; passing them directly to the right local chain selects
P/left bracket. The pinned Vial RGB implementation does not reject these
cross-half writes, whereas the pinned official QMK implementation does.

VIA and Vial now select separate framework rules and RGB integration modules.
VIA retains native QMK routing. Vial rejects invalid and foreign-half indices
before its native global-to-local conversion. Regression tests compile each
framework's actual index/setter functions with its integration module and cover
Bluetooth indicators and all 77 LED indices on both halves. This corrects the
RGB write boundary without adding radio-state synchronization to the split ABI.
Physical confirmation after installing the corrected Vial build is pending.

## October 11 intermittent wired key pauses

The user reports pauses of one second or less affecting typing from both halves
over USB while RGB animations continue, including with the Vial app closed.
The recurrence interval and USB driver state during a pause have not been captured.

Native execution reproduced a 200 ms main-loop delay every 30 seconds when USB
is active but the radio remains DISCONNECTED after a mode change. The board's
radio recovery hook now skips USB and clears its reconnect timer. Regression
tests cover 90 seconds of active wired typing, unchanged Bluetooth/2.4 GHz
disconnected deadlines, and restarting the wireless deadline after leaving USB.
The wired test fails against the preceding source and passes with the guard.

This establishes removal of that software stall, not resolution of the physical
symptom. A brief USB suspension can also reject typing while RGB continues;
split-link retries can stall the shared scan loop. Neither has been captured
during the reported pauses. Hardware validation of this correction is pending.

## October 6 Bluetooth wake failure

After installing the recent V15 reconstruction, the user reported approximately
one-minute idle RGB shutdown, followed by a left-only wake. The right half loses
both typing and lighting and cannot wake from its own keys until repowered.
No settings or runtime capture of this occurrence is available yet. A native
regression reproduces a sleep-entry interrupt race in the preceding source;
the correction and capture procedure are documented in
[Sleep-entry wake regression](SLEEP-WAKE-REGRESSION.md). The one-minute sleep
trigger and physical recovery with the correction remain unverified.

## October 3 captured false wake

The user reported repeated LED-off, brief wireless-indicator flashing, then LED-on cycles in Bluetooth/2.4 GHz with debug firmware and USB power attached. A read-only capture from the directly connected left half is saved at `artifacts/tests/2026-10-03-idle-wake-initial.txt`. Both halves reported diagnostic protocol 2. No reset, flash or setting write was performed during capture.

The retained Bluetooth history repeatedly shows `MCU STOP return ... wake=NONE`, followed immediately by `STOP -> WAKEUP` and then `0xCC(LED on)`. For example, events 433–439 return with NONE at uptime 1,200,498 ms and restore NORMAL/LED power at 1,200,705 ms. In the captured history the radio was disconnected, so the sleep requests are approximately 31 seconds of recorded MCU uptime apart (the existing 30-second reconnect timeout). SysTick is disabled during STOP: these timestamps do not measure elapsed wall time or establish how long WFI slept. The user's approximately one-minute observation also matches the existing 60-second connected-idle timeout, but a connected-idle failure was not captured.

The current correction adds only `LPWR_WAKEUP_NONE` to the existing UART branch that retains STOP in `lowpower.c`. This matches the verified stock V13/V15 ordinary wake filter. Both cached public ports accepted NONE as a full wake; this defect predates the debug/release split. The other two withdrawn October 1 changes remain withdrawn: preparation timing and pending-manual-request lifetime are unchanged. No timer, polling, retry, interrupt configuration, battery-key behavior, debounce, USB recovery behavior or framework change is added.

The regression test fails against the preceding source. It exercises repeated NONE returns across BT1/2/3 and 2.4 GHz with RGB previously on/off, verifies that lighting stays disabled and activity is not reset, and then verifies a real matrix wake. Existing tests cover UART, all other recognized wake causes, USB suppression, timeout and baseline preparation/manual-request behavior.

This capture establishes the erroneous software wake transition, not which hardware interrupt caused WFI to return. In particular, it does not prove sustained low-power residency with USB attached. Further checks should cover wake from either half, mode switching, and battery/charging indicators. If another cycle remains, read its wake cause before making another change. Earlier artifacts are preserved under `artifacts/backups/2026-10-03-before-none-wake-fix/`.

### Installation and initial hardware result

With separate user authorization for each half, the debug image with SHA-256 `a82d93e789e54a3e1bae885573c11dab3a6476d90c2a5e336091a0af08afd89b` was installed on both halves on October 3. Both software bootloader requests succeeded. Full application readbacks matched the image, both applications restarted, and the 360 saved matrix mappings on each half were verified unchanged. No EEPROM reset or read-protection change was performed. The installation record and backups are in `artifacts/recovery/2026-10-03-none-wake-installation.json` and the corresponding per-half files.

The user confirmed that the keyboard stayed asleep during the requested 90-second test with Bluetooth selected and USB power attached, and both halves woke normally when a right-half key was pressed. The subsequent capture, `artifacts/tests/2026-10-03-none-wake-bluetooth-switching-confirmed.txt`, shows NONE returns at events 23 and 61 retaining STOP. Later ONEKEY (split-line) and SWITCH wakes enter WAKEUP, as intended. At readout both halves report NORMAL, LED power on, no pending manual sleep, and a connected split link. Five RPC failures occurred among 185 calls around the sleep transitions, with subsequent successful communication; the log does not show a persistent link failure.

The user subsequently clarified that no Bluetooth host was available: they had selected Bluetooth and waited for sleep without connecting to an actual BT device. This matches the captured USB/BT1 transitions and cached radio state DISCONNECTED, with no BT2/BT3 selections or CONNECTED radio state. The earlier affirmative switching response must therefore not be counted as paired-host switching or connected typing validation. Connected-idle, paired-device switching, 2.4 GHz, left-key wake and longer stability checks remain untested. Runtime timestamps exclude time spent in STOP; neither sleep duration nor current consumption is established by this log. Record the visible result as a passed disconnected-Bluetooth sleep/right-key-wake check, not proof that every intermittent blackout is resolved.

## October 2 scope correction

On October 2, the normal firmware was returned to the pre-battery sleep policy at `c5c24d42`. The earlier board integration described above was retained. All three October 1 changes below were removed because they altered global sleep behavior outside the requested battery-key/debounce scope. The October 3 device evidence above now justifies restoring only the NONE wake filter. Tests preserve the other baseline transitions; they do not assert that inherited idle issues are resolved. See [current scope](BATTERY-FIXES.md).

## October 1 idle-sleep candidate (withdrawn)

After the stock battery-event build, the user reported that both halves turn their LEDs off after idle, immediately come back on, and flash the selected wireless-device indicator once. The connection mode and triggering interrupt have not been captured. The earlier USB diagnostic captures did not include this event.

The subsequent firmware change is confined to `drivers/wireless/lowpower.c` and corrects three independently reproducible discrepancies from the official V13/V15 images:

- Keep STOP for wake cause NONE (0), as already done for UART (2), instead of restoring lighting without a recognized wake source. Stock V13 `080028DC..080028FA`; V15 `08002C5C..08002C7A`.
- Invoke the board's preparation hook only after the existing 200 ms delay. This sends one `0xAA` peer sleep request when preparation is due instead of sending it on every scan during that delay. Stock V13 `08002AB4..08002AC2`; V15 `08002E2C..08002E3A`.
- Clear pending manual-sleep requests on every state assignment, including STOP to STOP, so an old request cannot cause another sleep immediately after waking. Stock V13 `08002518..0800253C`; V15 `08002890..080028B4`.

`tests/qmk/lowpower_test.c` runs the maintained state machine with simulated time and wake sources. Four regression scenarios fail against the preceding source. Preservation cases cover all six accepted wake causes, BT1/2/3 and 2.4 GHz, RGB previously enabled/disabled, the existing wake delay, active USB, the idle timeout, and a fresh sleep request after waking. These are host tests, not measurements of the connected hardware.

The inter-half wake interrupt, pin shutdown, existing two-second status exchange, radio commands, low-battery policy, battery/charging displays, 5 ms debounce, keymaps, original VIA JSON, EEPROM format, and bootloader paths are unchanged. This is not a complete port of stock power management. In particular, it does not establish whether the observed event came from a real split-line edge; that still needs a diagnostic capture. Do not describe the reported hardware symptom as resolved until an idle test confirms it.

The preceding artifacts and source are preserved under `artifacts/backups/2026-10-01-before-idle-sleep-fix/`. Before/after test output is in `artifacts/tests/2026-10-01-idle-sleep-{before,tests}.txt`. This change requires no VIA JSON reload or EEPROM reset.

The radio protocol has six ordinary key slots plus a 14-byte overflow bitmap. The host tests cover all bitmap usages, but this protocol cannot represent arbitrary USB NKRO usages beyond the radio's bitmap when all six slots are occupied.
