# Live battery capture after the retained-estimator analysis

The [first-100% analysis](INITIAL-100-ANALYSIS.md) traces the radio query handler,
the maintained ZMK control core and cold/periodic ADC setup. Stable false full
input produces no full command on USB selection. Cold setup consumes ten ignored
conversions; periodic setup defers one completion by 10 ms without that priming
step. Neither observation proves a bad physical conversion. Eleven instruction
tests now pass; first-high-voltage versus transient-full attribution remains open.

The subsequent user-reported sequence below supplies stronger transition evidence:
entering wired mode changes the reading to 100%, returning to Bluetooth retains
it with or without USB power, and full power cycling restores a normal reading.
This directs the investigation to a USB-triggered retained state. The older
three-hour-spaced telemetry comparison alone did not establish this sequence.

The latest wired-return observation confirms 100% in the board cache while the
hardware full input is inactive. It is not an immediate 86-to-100 mode-switch
trace: file timing places the earlier Bluetooth observation about three hours
before this capture, and current LEFT uptime indicates an intervening restart.
See the final section for the measured fields and interpretation limits.

The subsequent battery-only Bluetooth observation below reads 86%, with matching
uncached BLE data and no progressing large-drop counter during a minute-long
window. It preserves the user's Bluetooth mode and adds no reset or unplug test.

The user connected LEFT USB in wired mode without requesting a full power
cycle. Both halves verify application size 105,996, CRC32 `3957255507`, and
resident CRC32 `1770733249`. No firmware, pairing or explicit reset command was
sent. Temporary output selection used the already paired Bluetooth 2 profile;
USB output was restored and independently verified afterward.

## Initial state captured before profile changes

The first reply reported 100%, cable present and hardware full input inactive,
with 16,287 queries and 16,287 replies. A subsequent seven-sample series over
60.11 seconds remained at 100%; queries/replies both advanced from 16,322 to
16,342. Reply age never exceeded 2,180 ms. LEFT uptime advanced continuously
from 49,159,696 to 49,219,772 ms, and the radio error counter stayed zero.

Thus UART querying and the ZMK cache were working during this baseline. A flat
100% cannot demonstrate lack of charging updates: the estimate is already at
its ceiling. The hardware full signal being inactive also means the reported
100% must not be treated as confirmation that the charger has terminated.

## Direct radio and Bluetooth observations

The known BLE diagnostic service was unreachable while USB output was selected.
After the bounded selection of existing Bluetooth 2, both radio RAM snapshots
reported:

| Field | Value |
| --- | --- |
| Previous estimate valid / previous percentage | 1 / 100% |
| Current percentage | 100% |
| Charger mode | 3, charging |
| External percentage source | 0, internal source |
| Bluetooth Battery Service enabled | 1 |
| Large-drop counter | 0 |
| Sampler timer ID / period field | `0x0f04` / 1,000 ticks |

The timer's valid ID and period agree with an armed nominal ten-second timer.
That establishes timer configuration during the Bluetooth probe, not proof
of new physical voltage conversions. Selecting Bluetooth may restart sampling;
it cannot establish whether the sampler was stopped in the original USB state.

An uncached native Bluetooth Battery Service read returned 100%, matching both
the radio field and the earlier USB reply. The three paths therefore agree in
this capture. It supplies no evidence of a Windows-versus-ZMK percentage mismatch
at this moment and does not establish behavior at a lower state of charge.

## Interrupted unplug comparison

The user performed the requested unplug/reconnect interval. The collector had
recorded 11 charging-mode samples when it stopped; it obtained **no discharge-
mode sample**. Its cleanup attempted to select USB while USB was absent, failed,
and obscured the initial exception. The collector must not be presented as a
successful charge-to-discharge trace or evidence of an ADC failure.

Subsequent health inspection showed that both main controllers had restarted
(LEFT uptime 174,498 ms, RIGHT 174,655 ms). Sleep or a power transition may have
interrupted the observation; the exact cause was not recorded. USB-powered idle
time counts toward the inactivity deadline, so the setup also allowed immediate
sleep after cable removal if the keyboard had already been idle for five minutes.
The experiment did not preserve a continuous main-controller session.

After the user reconnected, an explicit USB selection and final read-only status
verified selected=active=USB, waiting=false, queue=0 and errors=0. The battery
again reported 100%, cable present and hardware full inactive, with matching
query/reply counts. No software reset or firmware installation was requested
during the experiment.

## Supported conclusion

The live capture validates the reporting chain at 100% and confirms correct
charging-state and internal-source flags during the Bluetooth probe. It does
not reproduce a charging freeze below the ceiling or establish fresh ADC
conversion timing before the profile change.

The [captured-instruction estimator findings](../2026-11-04-zmk-battery-estimator/README.md)
remain the demonstrated mechanism for retained values: directional clamping,
the 360-sample large-drop hold, incompatible history across charging-state
changes and the early 100% voltage threshold. A fresh UART reply can contain
that retained value. A live sampler stall remains possible but unproven here.

A useful future trace needs a partial-charge value and a continuous battery-
powered session across unplugging, with fresh input before the test so it does
not immediately reach the sleep deadline. No further physical test or firmware
write was initiated after this interrupted comparison.

Private evidence is under
`.work/battery-estimator-investigation/2026-11-05/`: initial battery and identity
records, `usb-series.jsonl`, direct radio/BAS snapshots, probe scripts/logs,
the partial CSV, `health-after-unplug.json` and final USB status. The
[sanitized result](capture-result.json) contains no device addresses or key data.

## Subsequent Bluetooth-only partial-charge observation

The user selected Bluetooth and asked for another analysis. No output selection,
charging-state command, pairing, reset or firmware/RAM write was performed in
this follow-up. Existing read-only GATT helpers captured battery state directly
over the already connected Bluetooth profile.

The uncached Battery Service and radio RAM both reported 86%. Thirteen radio
samples spanning 63.165 seconds kept current/previous percentage at 86%, charger
mode at 0 (discharging), external-source flag at 0, BAS enabled at 1, and the
large-drop counter at 0. The timer ID remained `0x0f08`, with period field 1,000
ticks, consistent with an armed nominal ten-second sampler.

This confirms agreement between the exposed radio state and the uncached BLE
value during a partial-charge, discharging observation. The 360-sample large-
drop hold was not visibly accumulating. A steady value over one minute does not
establish a freeze or prove new ADC conversions: the interface still supplies
no measured voltage or ADC sample sequence number. It also cannot establish
the actual energy consumed between the earlier charging 100% and this 86%.

The [Bluetooth result](bluetooth-result.json) is sanitized. Private evidence is
in `.work/battery-estimator-investigation/2026-11-05-bluetooth/`: radio/BAS
snapshots, `radio-series.csv`, the strictly declared read-only PowerShell helper
and its syntax/runtime verification. Source-derived curve examples were also
executed with a cleared history state: 3,792 mV maps to 60% discharging / 12%
charging; 4,000 mV maps to 90% / 67%. These are synthetic inputs to the actual
captured mapper, not measured voltages of the current physical battery.

## Later wired return: 100% with hardware full inactive

The user reported 100% in wired mode after the earlier 86% Bluetooth reading.
A local USB battery getter independently confirmed 100%, cable present and
hardware full inactive, with 121 queries and 121 replies. The getter reads the
same board cache used by the held LED view; the parser copies the percentage
from valid radio `0x5c` replies without converting USB presence into 100%.

A seven-sample follow-up over 60.047 seconds held at 100%. Query/reply counts
both increased from 144 to 164; reply age stayed at or below 2,188 ms. Selected
and active output were both USB (0), with no pending control and radio error
count 0 throughout. LEFT uptime advanced continuously from 429,446 to 489,462 ms,
with zero watchdog feeds. No output selection, direct radio RAM access, pairing,
firmware/RAM write or reset was requested in this follow-up.

The private file modification times put this series' first sample approximately
11,044.4 seconds after the earlier Bluetooth series ended. Combining the later
file time with current uptime places the current main-controller boot about
10,615 seconds after that earlier capture. These are approximate file-derived
intervals, not recorded charging or mode-change times. Thus this is not a
continuous-boot, immediate 86-to-100 transition. Actual charging over the interval
is possible; restart cause and radio-state retention across it were not recorded.

The captured mapper supplies two code-supported ways to reach 100%: a synthetic
voltage of at least 4,100 mV, including while charging mode is 3, or a retained
hardware-full mode 4 with a valid previous estimate. Starting with previous 86%,
the actual instructions return 86% at 4,099 mV in charging mode, 100% at 4,100 mV,
then retain 100% when a subsequent synthetic charging input is only 4,000 mV.
These executions demonstrate a threshold plus charging-direction clamp; they
do not measure the current cell or prove which path produced today's 100%.
The presently inactive full input does not rule out an earlier full indication.

The evidence confirms that wired 100% is present upstream of LED rendering and
that percentage replies continue arriving. It neither proves actual full charge
nor proves the ADC sampler is currently running. An observation at the ceiling
also cannot establish a charging freeze. Attribution specifically to USB mode,
the curve threshold, a transient full signal or stale sampling remains open.

Private evidence is in
`.work/battery-estimator-investigation/2026-11-05-wired-return/`: first battery
snapshot, the strictly typed/formatted/linted `capture_wired.py`, its JSONL
series and `mapper-from-86.json`. The five existing captured-instruction tests
pass again. See the [sanitized result](wired-return-result.json).

## User-reported USB-entry latch and conditional persistence tests

The user then supplied a specific sequence: Bluetooth -> wired produces 100%;
wired -> Bluetooth retains 100% regardless of USB cable presence; full power
off/on restores the normal level. Treat this as the primary reproduction to
investigate. It supersedes treating elapsed charging between the earlier isolated
captures as a sufficient explanation for the reported behavior. This sequence is
user evidence; the agent has not yet recorded its first incorrect sample.

Three additional private tests execute the actual captured mapper instructions.
With previous 86%, one synthetic charging sample at 4,100 mV returns 100%;
1,000 subsequent charging calls at 4,000 mV retain 100%. If the charger mode
instead changes to discharging at 4,000 mV, the first 360 calls retain 100%,
then call 361 returns 90%. Clearing estimator history returns 90% immediately
in discharge mode. Separately, a single synthetic full-mode input with valid
history produces 100% even at 3,792 mV, then charging retains that value.

These tests explain how one incorrect high result can persist while plugged in
and after unplugging without requiring the display to be stuck. They do not
prove the cause of that first result: voltage conversion and host-mode selection
are not emulated by these mapper tests. All eight mapper tests pass, including
the three new conditional cases; the new test file is formatted, linted and
strictly type checked. Synthetic voltages are not measurements of this device.

A read-only USB cache request was unavailable at this checkpoint. Without
selecting an output or requesting a reset, the known Bluetooth connection did
respond: current/previous percentage 98%, discharging mode 0, internal source,
BAS enabled, drop counter 0 and timer ID `0x0f08` / period 1,000 ticks. This
isolated snapshot does not reproduce or refute the user's transition sequence;
the timing of any user power cycle was not captured.

Next investigation should establish which USB-transition operation seeds 100%.
Compare QMK and ZMK control ordering, charger/full notifications (including a
possible transient full indication), and ADC validity around radio sleep/wake.
Review the ZMK-specific 5.5-second quiet interval as a possible contributor,
while preserving the earlier evidence that removing sampler recovery can leave
stale readings. Avoid an unconditional rollback based only on this correlation.
Keep wired battery reporting and exclusive host routing. The regression to
qualify is a partial-charge Bluetooth -> USB -> Bluetooth round trip, both with
USB power retained and after cable removal, with no corrective power cycle.

No firmware/RAM write, output/profile selection, pairing or reset was performed.
Private evidence is in
`.work/battery-estimator-investigation/2026-11-05-mode-latch/`, and private tests
are in `tests/test_usb_latch_conditions.py` under the same investigation root.
See [sanitized findings](mode-latch-result.json).
