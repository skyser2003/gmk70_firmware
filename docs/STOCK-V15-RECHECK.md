# Stock V15 recheck, 2026-10-09

This report records seven differences in the **pre-fix** firmware. All seven
are corrected by the 2026-10-09 follow-up; the findings below preserve the
original reproductions and their scope. The review executes stock and compiled release/debug ARM code beyond the boundaries of the
earlier isolated-hook tests. The original review made no firmware changes and does not establish
the cause of the reported Bluetooth right-half failure.

Reference application: 80,220 bytes, SHA-256
`3048ee6390a42f6c81a28df3dc9f032a926fe843d904ea9154a0554dae2af0b0`.
Repository HEAD: `d4e1c2b`, with the existing uncommitted stock power follow-up
and `5E3213` logger support. The reviewed debug-updater application had SHA-256
`71fd45aeca3c20589ffdefb20165632e604c6b6f19f4398245ee21923332b8cc`.
The normal release/debug applications match the preceding power-follow-up
builds; the logger-ID change did not alter them.

The most consequential new result is a stuck software STOP state after a
rejected special wake. Ordinary classification temporarily assigns WAKE before
the special hook restores STOP. That leaves the current previous state as WAKE;
the STOP eligibility gate accepts only previous PRE or STOP, so later iterations
cannot process another STOP return. Both USB-special and critical classes
reproduce this. Stock directly calls the special hook and retains previous STOP.
An independent path that explicitly changes state, such as physical USB resume
or a raw-HID wake, can still escape the port's state. This result does not
establish that the user's right half entered either special class.

## Confirmed differences

| Area | Stock V15 | Current implementation and effect |
| --- | --- | --- |
| Peer UART during PRE | Only the master changes A10/A9 to mode 4 after sending AA. A non-master skips that branch (`0x08002E0E`–`0x08002E5E`). | `gmk70.c` changes these pins on either half whenever selection is wireless. The right half leaves UART mode during PRE, before the 200 ms STOP gate. A slave reply still in progress could be interrupted. |
| Deadline clock | State/activity timestamps and elapsed checks use local timer routines `0x0800E784`/`0x0800E80C`. | `lowpower.c` uses `sync_timer_read32`/`sync_timer_elapsed32`. Current QMK adjusts the non-master's clock offset from the peer. A correction can make idle appear expired or bypass the 200 ms STOP/WAKE gate. The master uses local time in either implementation. |
| Mode-sync recovery | Device selection at `0x080028D6`–`0x080028E8` moves PRE/STOP to WAKE and clears active preparation to ordinary, even when selecting the same device. The peer 55 handler calls this path (`0x080052AC`). | `wireless_devs_change()` refreshes activity but does not perform that state/class transition. Peer mode synchronization therefore lacks a stock recovery path while PRE/STOP. |
| Special radio preparation | Before special STOP, `0x08002ECE` reads power byte +1, the **previous state**. Previous STOP skips the USB-selection command. | `wls.c` tests the **wake cause** against UART instead. Rejected special wakes can send additional USB-selection commands; a previous UART cause can skip a selection that stock would send. |
| Special STOP dispatch | The power task dispatches directly to the ordinary, critical or USB post-hook according to active class (`0x08002F0A`–`0x08002F46`). | `lpwr_stop_cb()` first performs ordinary classification for every class, then invokes the special post-hook. Special returns perform two state assignments instead of one, including two voltage queries. A rejected matrix/switch wake leaves previous WAKE, causing `lpwr_is_allow_stop()` to reject subsequent iterations. |
| Physical STOP eligibility | PRE captures the selected device at `0x08002E64`. STOP enters the MCU only if selection still equals that snapshot and the device/cable combination is not wired USB with the cached cable flag set (`0x08002EEE`–`0x08002F06`). | `lpwr_enter_stop()` checks state and pending wake, but lacks these two stock gates. It can physically sleep during wired host suspension or after a selection change where stock keeps the MCU running. |
| Cable runtime flag | Each master power-task pass copies A7 into `confinfo.flag` before evaluating the state machine (`0x08002D6C`–`0x08002D7C`). The ordinary acceptance predicate also captures it, and a non-master forces it true there. | `gmk70.c` copies the physical cable into `charging_state`, but refreshes `confinfo.flag` only at ordinary timeout acceptance. Charging notifications, indicators and critical-battery decisions use the potentially stale flag. A stale charged flag can delay battery protection. |

The first three differences affect the right half's communication and recovery
directly. The special paths also differ, but an ordinary peer AA request does
not by itself select a special class. The existing change that keeps the left
critical flag out of the peer packet remains correct.

## Reproduced examples

- With one minute of local idle and a simulated +241,000 ms synchronization
  correction, stock rejects the five-minute idle deadline; the current
  non-master accepts it. With only 100 ms of local elapsed time, a -1,000 ms
  correction causes the current unsigned elapsed check to accept STOP/WAKE;
  stock waits. Master-role and zero-offset controls agree.
- Ordinary PRE on a wireless non-master performs no stock GPIO mode writes;
  the current code changes A10 and A9 to mode 4. Master-role and wired controls
  agree.
- Re-selecting BT1 while in PRE/STOP makes stock enter WAKE with ordinary
  preparation. Current firmware retains its state and preparation class.
  NORMAL/WAKE controls agree on this state transition.
- A rejected critical matrix wake leaves both implementations in STOP, but
  stock records previous STOP and sends one voltage query. Current firmware
  records previous WAKE and sends two queries. After another 200 ms, stock
  processes a simulated cable wake and clears the special class; the current
  STOP gate refuses to run, even with cable cause already recorded. It remains
  stuck after another three seconds. USB-special repeats the same failure.
  The ordinary return controls agree.
- Wired selection with the cached cable flag set makes zero stock MCU STOP
  calls and one current call. A changed selection since PRE has the same
  entry-count difference. Matching wireless selection controls enter once.
- For previous STOP and previous cause NONE, stock sends no special USB
  selection; current sends one. For previous PRE and cause UART, stock sends
  one and current sends none.
- On the master, starting with runtime cable flag true and physical A7 low
  clears the stock flag during a normal task pass. Current firmware retains
  it until ordinary acceptance. A matching initial flag is a control.

These are software reproductions, not observed physical event sequences. A
synchronization offset can change after unequal STOP intervals, but no capture
establishes its value during the user's incident. The tests do not prove a
permanent failure or reproduce the complete one-minute, both-halves-dark
symptom on hardware. They do reproduce the stuck software state under the
special-class conditions described above.

## Verification scope and earlier blind spots

The existing 1,238 bounded power comparisons and 12 broader ARM checks still
pass. They cover the previously reconstructed peer payload, GPIO wake setup,
acceptance gates, battery predicate, VIA controls, keymap/encoder/RGB tables,
USB identity and selected board actions. Their success remains limited to their
assertions.

The earlier fixtures returned the same simulated value for local and
synchronized clocks, and compared special hooks after manually applying the
ordinary classifier. They also tested PRE mostly as a master and evaluated
the ordinary cable predicate separately from the master task's flag refresh.
Those boundaries hid the new mismatches. In particular, the earlier power
report's description of the special radio gate as a prior UART cause was
incorrect: byte +1 is the previous power state.

The new harness executes the real current QMK synchronization arithmetic,
complete stock/current STOP dispatch around a simulated physical STOP, PRE on
both roles, and the mode-selection power prehook. It records 140 scenario rows
plus 32 additional STOP/WAKE clock-gate checks across release/debug. Results,
JSON observations, disassembly and the local harness are under
`artifacts/analysis/stock-v15-recheck-2026-10-09/`. GPIO/time/radio/USB/private
buffer boundaries are still simulated or intercepted. No physical device was
read, reset or flashed, and the existing firmware packages were not replaced.

## Other differences and unresolved areas

The STOP-entry race guard, persistent debug journal, EEPROM readers and resident
split updater are intentional repository additions. The guard preserves wake
events that stock's raw entry sequence could discard. These remain differences
even after fixing the seven mismatches above.

The older framework's private key-record reset, complete tap/hold/report and
encoder sequences, animation frames and timing, physical SPI wear leveling,
USB driver/event delivery, hidden manufacturing paths and mixed stock/current
split interoperability remain unverified. The supplied MCU image does not
contain the separate RF module or receiver firmware. See the
[reconstruction report](STOCK-V15-RECONSTRUCTION.md) for broader coverage.

This review does not change the valid stored idle selector. The default is
still five minutes; selector 1 is one minute, and stock's separate critical
battery and disconnected-radio requests remain separate paths.

## Fix validation, 2026-10-09

The maintained code now leaves peer UART pins unchanged during PRE, uses local
power clocks, restores stock mode-sync wake, checks previous state for special
radio preparation, dispatches each STOP class once, gates physical STOP on the
PRE device snapshot and cached USB cable bit, and refreshes the master cable
flag before each power task. Physical USB resume also assigns special WAKE
once before clearing its raw latch. The pending-wake entry guard is retained.

The new regression runner compares the complete state, previous state, class,
MCU STOP count and radio commands, including a rejected special wake followed
by cable recovery. Both release/debug run the 140 scenarios and 32 additional
clock checks with stock-equality assertions. Evidence is saved separately in
`artifacts/analysis/stock-v15-edge-fixes-2026-10-09/`; the pre-fix evidence above
is preserved. Native tests link the actual board special classifiers with the
common state machine, rather than testing those hooks only in isolation.

Full stock equivalence and the physical Bluetooth incident remain unverified.
These fixes address demonstrated software differences; they do not establish
which power class or event occurred on the user's hardware.
