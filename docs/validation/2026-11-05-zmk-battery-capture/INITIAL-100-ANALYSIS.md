# First 100% result during wired selection

The user's reproduction is Bluetooth -> wired -> 100%, returning to Bluetooth
with or without USB power retains 100%, and full off/on restores a normal value.
This investigation separates the first high result from the retained filter
that can prolong it. No installed firmware, radio RAM, output selection, pairing
or reset was changed. The initial trigger remains unconfirmed.

## Paths excluded or bounded

The ZMK battery parser copies valid `0x5c` reply percentages. Its cache, native
sensor and LED renderer do not convert cable presence or USB selection into
100%. In the captured radio, UART request `A6 53` at relocated
`0x1100184e..0x1100187c` returns current percentage `0x11003d07`, without a
USB-specific override. BLE service initialization's separate 100% default at
`0x10007216` writes `0x11003d42`, not this percentage. That default alone cannot
explain the 100% already observed in the main controller's UART-fed cache.

An offline C harness runs the maintained ZMK `radio/core.c` through an 18-second
Bluetooth -> USB -> Bluetooth sequence with immediate synthetic ACKs. With cable
present and full input continuously false, it emits 13 charging commands and no
`A6 66` full command. With cable absent it emits 13 discharging commands and no
full command. Thus USB selection does not itself create a full notification
under stable inputs. The same harness with a synthetic 10 ms full-input pulse
emits one full command. A later telemetry reading of full=false therefore cannot
exclude a prior transient. A pulse must still overlap a mapper invocation, or
leave full mode retained until one, to affect the percentage.

The C probe compiles with strict conversion warnings and ASan/UBSan, passes its
assertions, and passes GCC static analysis. It is not a physical pin trace or
an emulation of the radio's ACK timing.

## Actual mapper conditions for the first increase

For a valid previous value of 86%, the captured mapper can produce 100% after a
converted input reaches 4,100 mV while charging, or when charger mode is 4
(full). Discharging modes 0/1 reject increases: even a synthetic 4,500 mV input
retains 86%. A cleared previous-valid state permits a fresh 100% estimate in
discharging mode, so history validity also belongs in a transition capture.
The current live protocol reports filtered percentage, not these voltage inputs.

USB selection itself posts deep-power event 1 and stops the periodic sampler.
It does not directly call the percentage mapper. The ZMK-specific recovery
interval then permits a UART idle/sleep/wake transition and restarts the sampler.
QMK has no equivalent explicit 5.5-second hold in its mode-control path; its
regular percentage queries and level key are wireless-only. This makes the
additional wired sampling lifecycle a concrete difference to investigate, not
proof that the interval causes a bad conversion.

## Cold versus periodic ADC setup

Following original branch targets gives:

| Captured routine | Observed control flow |
| --- | --- |
| Cold ADC initialization `0x10007024` | Wait for and consume ten conversion results; do not map them to percentage. |
| Periodic setup `0x10006fac`, reached from timer callback `0x10011f08` | Configure/start ADC, register completion `0x10011ec5`, schedule it after 10 ms; no discarded conversion calls in this routine. |
| Completion `0x10011ec4` | If data-ready, read one result, scale it, call mapper `0x100070d4`, store percentage and update reporting; then disable ADC. |

Two bounded tests execute the original cold/periodic setup instructions with
GPIO, ADC configuration, data readiness and timer APIs stubbed. They confirm
ten ignored reads versus zero ignored reads plus a 10 ms deferred completion.
They do not establish analog settling or emulate an actual failed conversion.
Ordinary Bluetooth sampling also uses the periodic path, so this difference
alone does not establish a USB-specific defect. A conversion affected by the
additional sleep/wake transition remains a hypothesis.

The pinned FreqChip driver source, fetched only under the private investigation
directory, identifies ADC data-ready and calibration/reference operations:
[driver source](https://github.com/shunxingzhe/fr8000/blob/9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6/components/driver/driver_adc.c)
and [header](https://github.com/shunxingzhe/fr8000/blob/9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6/components/driver/include/driver_adc.h).
The installed captured instructions, rather than this SDK mirror, remain the
evidence for the GMK70's exact sampling behavior.

## Conclusion and next diagnostic

The initial 100% is not explained by LED rounding or a native ZMK USB default.
Under stable false full input, the maintained control core also does not command
full on USB entry. The two main unresolved inputs are a transient full-mode
notification and a high converted sample during the extra USB recovery lifecycle.
A sampler stall can retain an old result but, by itself, cannot explain a new
86-to-100 increase.

A decisive diagnostic must correlate host selection, cable/full pin transitions,
actual transmitted charging commands, radio charger/history state, and the first
converted voltage reaching the mapper. The last item is not exposed by the
current read-only percentage getter. Queries/replies or an armed timer cannot
substitute for that measurement. Keep wired battery reporting; do not label
blindly removing the recovery interval or changing the curve as a verified fix.

### Remediation boundary

The existing UART commands do not provide a verified clean estimator-history
reset. In particular, relocated `A7` dispatch at `0x1100188a..0x110018b4`
either changes the Battery Service enable flag, or supplies an external
percentage and stops internal sampling. It does not clear previous-valid or
the large-drop counter. This is not a suitable substitute for a new measurement.

The first main-firmware correction should follow transition evidence: qualify
full-input stability before sending full if a false notification is captured;
correct the USB sampler recovery lifecycle if that path produces a bad sample.
Retain wired percentage reporting, native ZMK battery integration and exclusive
host report routing. If a radio-side defect cannot be avoided through verified
main-MCU sequencing, the alternative is a radio-side estimator/sampling change,
which is a separate deployment from the main-MCU updater. No radio change is
prepared or authorized by this analysis. Validate the user's complete mode
round trip without a corrective power cycle before calling a fix successful.

An additional read-only Bluetooth snapshot currently reports 98%, discharging
mode 0, internal source and drop counter 0; it does not capture the reported
transition. Private evidence is under
`.work/battery-estimator-investigation/2026-11-05-initial-100/`, including
`control-trace.json`, `adc-setup-trace.json`, instruction ranges and test logs.
The shared private probe and dedicated tests live in the parent investigation
directory. Eleven captured-instruction tests pass; added Python code passes
formatting, lint and strict typing. No physical mode transition was induced.
