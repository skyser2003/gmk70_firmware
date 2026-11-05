# Charging/discharging discrepancy and retained estimates

The [November 5 live follow-up](../2026-11-05-zmk-battery-capture/README.md)
records matching USB/radio/BLE readings at 100% and an interrupted unplug trace.
The unavailable-telemetry statements below describe the earlier checkpoint.

The user reports that the battery level generally appears higher while charging
than unplugged, and seems to refresh only after fully powering off. Exact paired
percentages and live voltages have not been supplied. This follow-up confirms
retained-estimate behavior in the captured stock radio; whether the current
incident also includes a stopped ADC sampler still needs a live capture.
No maintained firmware, installed application or radio image was changed.

## Reporting path

The FR8000 radio measures voltage and calculates its percentage. ZMK queries
that percentage every three seconds, caches it, and exposes it through the
native state-of-charge sensor and held LED view. The external radio also owns
the Bluetooth Battery Service; ZMK's sensor does not replace that BLE gauge.

`battery.c` marks every valid UART percentage reply fresh. Its `age_ms` is time
since the reply, not time since an ADC conversion. The response supplies neither
voltage nor a conversion timestamp. Increasing query/reply counters cannot
prove a new measurement. Fn+B quantizes the result: one LED covers 0–19%, the
second appears at 20%, and subsequent LEDs appear at ten-point boundaries.
Small charging gains can therefore be invisible in that view. Windows may also
cache BLE notifications. The displayed percentage is the central radio's
estimate, not separate measurements of both physical packs.

## Captured-instruction tests

The probe executes mapper `0x100070d4` from the captured stock application,
SHA256 `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`.
Unicorn 2.1.4 supplies a synthetic CPU, voltages and RAM state. The only stubbed
dependency copies the actual captured voltage tables; the mapper's arithmetic,
branches and retained-state updates execute original instructions. This is
offline evidence rather than a device measurement or a rewritten mapper.

Five stateful tests, formatter/lint and strict typing pass. The existing power
core suite passes too. See [offline output](offline-proof.json); private code,
tests and the frozen emulator environment are in
`.work/battery-estimator-investigation/`.

| Synthetic input/history | Result |
| --- | --- |
| No previous estimate, 3,792 mV | 60% discharging; 12% charging. |
| Retain charging's 12%, switch to discharge at the same voltage | Still 12% after 1,000 mapper calls; reinitializing the estimator yields 60%. |
| Previous 60%, discharge voltage mapping to 50% | Holds 60% for 360 calls; accepts 50% on call 361. |
| Previous 55%, discharge voltage mapping to 50% | Accepts the five-point decrease immediately. |
| Previous 60%, charging voltage mapping to 12% | Still 60% after 1,000 calls. |
| Previous 0%, discharge voltage mapping to 60% | Still 0% after 1,000 calls; reinitialization yields 60%. |
| Charging, 4,099 to 4,100 mV | Jumps from 82% to 100% at the hard voltage threshold. |

Discharge rejects increases. A decrease larger than five points increments a
retained counter through 360 before acceptance on the next call. Charging
rejects decreases. The full-charge state forces 100% once previous state is
valid; the initial call still takes the voltage mapping, as covered by tests.

Sampler setup at `0x10007094` configures a nominal 10,000 ms period. The large-
drop filter can therefore hold a result for roughly **one hour of active
sampling**, longer in wall time if sampling pauses during sleep. UART queries
do not advance the counter; calls from the voltage sampler do.

Captured UART commands `A6 64/65/66` only update the charging-state byte; they
do not reset previous percentage or drop-counter fields. A charge-state change
can retain incompatible history from the other curve. Restarting the main MCU
also need not clear the separate radio's retained estimate. Estimator-state
reinitialization permits an immediate recalculation in the probe, consistent
with the user's full power-off observation. The complete physical reset
sequence was not measured here.

## Interpreting charging versus unplugged

This path estimates charge from voltage; it does not integrate current or
measure remaining mAh. Load and charging affect terminal voltage, while rested-
voltage estimation requires settling. See [TI's voltage-gauge discussion](https://www.ti.com/document-viewer/lit/html/SSZT786/GUID-5DD8C3D9-BCBE-467B-90E5-FAA672AAC15D)
and [gas-gauge application reference](https://www.ti.com/lit/an/slua458/slua458.pdf).

The same-voltage example gives a lower charging estimate, so the curves alone
must not be presented as proof of the user's observed direction. Actual voltage,
charging flags and retained history are needed to explain its size and direction.
A change on unplugging can include terminal-voltage change and these estimator
discontinuities. Neither percentages nor this analysis measure battery endurance.

A retained zero matters to the main controller: its existing critical policy
requests sleep after 60 seconds of fresh reported 0% on battery. A stale radio
estimate could resemble an empty battery or early shutdown. This is a source-
supported possibility, not proof that it happened or that the battery is faulty.

## Previous corrections and remaining evidence

Earlier fixes send charging-state notifications, preserve query liveness and
provide a quiet UART interval after USB selection to restart the sampler. Their
charging captures advanced without cycling power. They do not replace the
radio's voltage tables or retained filter. A stopped ADC is a separate possible
cause of a frozen value and still needs a current sampler-state capture.

Polling faster or changing ZMK's battery interval cannot remove the radio's
filter. The implemented query provides only filtered percentage. A proper
estimator correction needs a verified voltage source or a radio-side change;
overriding percentage without a measurement would manufacture the result.

Current USB inspection found no matching updater interface. A read-only BLE
attempt through the previously verified profile failed service discovery, so
neither supplied a fresh percentage or sampler-state measurement. No profile
selection, pairing change, firmware write or reset was requested.

The user has been asked to connect LEFT USB in wired mode while keeping the
keyboard powered, to capture the current estimate before a full power cycle
clears it. Until then, the stock estimator's retained-value behavior is proven;
attributing the entire present incident to it alone would exceed the evidence.
