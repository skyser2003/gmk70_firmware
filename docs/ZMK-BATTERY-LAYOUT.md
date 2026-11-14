# Battery indicators and saved Vial layout migration

## Refresh policy

Automatic three-second percentage inquiries run only when the selected output
is Bluetooth or dongle. A host-selection shortcut can override the physical
slider, so polling and its next deadline follow the logical output. The physical
USB-position requirement for updater and storage writes is unchanged.
Pressing the held battery-level action queues one coalesced refresh through the
existing UART worker, with one bounded retry. This preserves wired level display
without background wired queries. Charging-status presses and releases do not
request percentages. Native ZMK sensor reads remain cache-only; unknown or
expired data stays purple until a valid reply arrives. A hold longer than the
ten-second freshness limit requires another press to refresh.

Selecting USB cancels queued periodic queries. A frame already being transmitted
finishes without losing ACK ownership, but does not trigger a periodic retry.
The charger notifications and USB sampler-recovery interval remain unchanged.
The radio estimator is unchanged.

A due inquiry now takes its turn before ordinary queued HID reports; a busy
typing stream previously could postpone it indefinitely. Module acknowledgments,
host/charging-state controls, reconnect deadlines, zero-report release barriers
and an already-started A1/A2 keyboard pair retain priority. An unanswered query
still gets only one ten-millisecond retry, so telemetry cannot indefinitely block
typing or sleep. A periodic repeat of unchanged charging state follows the query.

Regression tests exercise the real UART owner and radio core with polling and
interrupt RX, both slider positions, USB/BT1/BT5/dongle selections, held Alt and
release reports, NKRO pairing, charging changes, failed ACKs and sleep admission.
They reproduce the scheduling defects, not the user's live Bluetooth failure.
These corrections cannot force a new ADC conversion or change the radio's
retained percentage filter described below; confirmation of the reported
60%-to-0% behavior still requires Bluetooth hardware testing.

The Studio central implements two held actions under **GMK70 Battery /
Charging**: parameter 0 displays battery level; parameter 1 displays charging
status. They can be assigned in Studio. The compiled factory keymap is unchanged.
The right peripheral displays the central's existing RGB frames and needs no
battery firmware update.

Holding the level action temporarily shows the stock ten number-row LEDs. Green
indicates at least 30%; lower levels use red. Zero still lights one segment,
matching the stock display. Unknown or older-than-ten-second data shows all ten
segments purple. Holding charging status overlays the left Fn LED red when cable
power is detected, or green when the stock full-status input is also asserted.
Releasing restores the current normal animation or off state. Both actions work
with ordinary RGB off and can be held together.

## Battery implementation

Only LEFT uses UART3, PC10/PC11 AF7 at 115200 baud. With event-driven idle,
UART RX interrupts enqueue bytes and notify the existing worker; the bounded
parser and transmitter remain single-owner worker operations. The polling path
remains when that option is disabled. No extra thread is reserved.
Battery inquiries use `A6 53 F9` and acknowledgments use `61 0D 0A`.
Automatic queries run every three seconds in wireless mode; the level action
also requests a refresh on press. Either query cycle permits one bounded retry.
Checksummed percentages must be in 0..100. The parser consumes other status/raw
frames so their embedded bytes cannot become battery samples. A timed-out raw
frame retains a finite discard budget; truncation can delay recovery of valid
samples, which remain explicitly unavailable during that interval.

In the canonical radio-enabled image, the same UART owner also forwards the
stock charger state: `A6 64 0A` while charging, `A6 65 0B` after unplugging, and
`A6 66 0C` when the cable/full inputs indicate completion. These are the existing
QMK/vendor commands, sent on a state change and refreshed every second. The
module's own battery estimate feeds both the percentage reply and Bluetooth
battery reporting; no percentage is synthesized from elapsed charging time.

The earlier port only queried the module and sampled charger GPIO locally. It
omitted these charging notifications, consistent with the reported level staying
at 3% through charging until a restart. The correction uses the existing radio
frame queue and ACK owner. A missing charger ACK gets one bounded retry and then
returns to the periodic refresh without disconnecting typing or keeping sleep
busy. State changes during a partial frame preserve that frame's original bytes.

### Fresh replies can contain an old measurement

Initial host selection now follows the QMK selector: wait for the existing
50 ms physical-switch debounce and select that destination directly. An explicit
host shortcut can also make the first selection. Previously, ZMK queued USB at
initialization before the switch settled, sending `A6 11 B7` even on a Bluetooth
startup. That USB command can stop an already-running radio ADC sampler. Sleep
configuration and charger maintenance still run during switch debounce; actual
USB selection retains the recovery interval described below.

The native radio-adapter regression fails before this correction and passes
afterward. It exercises initial USB, Bluetooth and dongle selection, restored
Bluetooth slots, switch bounce/invalid positions and the RIGHT role. Existing
UART tests still verify charging updates, query/ACK ownership and actual USB
sampler-recovery leases; they now request USB explicitly.

A bounded replay of the captured stock instructions confirms that direct
Bluetooth selection preserves an active ten-second sampler, whereas USB then
Bluetooth leaves it stopped until a separate wake event in the tested state.
The replay executes selector `0x10011A68`, power handler `0x100171D8`, sampler
setup `0x10007094` and inactivity control `0x100121EC`. Timer, UART and BLE APIs
are stubbed; asynchronous physical wake is supplied explicitly, not simulated
automatically. This establishes a possible stale-sampler path, not proof that
it caused a particular hardware incident or that the radio's percentage filter
has been corrected. No radio firmware is changed.

The stock radio image
(`100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`)
has a sampler-recovery requirement. USB selection (`A6 11`) posts its deep-power
event and stops the ADC sampler, even when USB was already selected. Ordinary
queries and charging commands only reset a four-second inactivity counter.
Physical UART wake restarts sampling, with the first conversion ten seconds
later. Continuous one-second/three-second traffic can therefore keep answering
from the cache while preventing the sleep/wake transition needed to restart it.

The follow-up correction gives an acknowledged USB selection one 5.5-second
quiet interval before resuming ordinary radio maintenance. Battery queries wait
behind that control lease; complete-frame acknowledgments remain permitted.
An explicit wireless selection bypasses the interval. Native USB typing uses a
separate transport and continues normally. The existing heartbeat/query cadence
then keeps the radio awake long enough for its ten-second sampler to run.
No radio flash, invented percentage, pairing operation or hardware watchdog is
involved.

The stock estimator also uses different charging and discharging curves and
directional hysteresis. Executing its actual mapper with an initial 3,792 mV
sample gives 60% in discharge mode and 12% in charging mode. A reboot jump is
therefore not a measurement of consumed charge. A7/A15 and percentage telemetry
do not measure current or establish battery endurance.

The stock estimator's captured-instruction behavior shows that a discharge drop larger than five
points is held for 360 sampler calls, with acceptance on call 361. At the nominal
ten-second cadence this is roughly an hour of active sampling. Discharge rejects
increases and charging rejects decreases, so mode changes can retain an old
value until reinitialization or a qualifying update. These radio behaviors are
separate from whether its ADC is currently running. Fresh UART replies alone
cannot establish fresh voltage measurements.

Separately, exhausted radio controls previously latched a fault that stranded
pending sleep commands, blocked battery queries and prevented MCU sleep. Failed
work now retires after bounded attempts, with bounded zero-report releases and
native HID clearing. A passive fault owns no sleep lease, while wireless typing
stays inhibited until explicit selection. Physical UART/parser/storage gates
still apply.

GPIO A7 is cable detection; A15 is the stock charger-full input. This reports the
board's status signal, not measured charge current. The cache also feeds a native
Zephyr state-of-charge sensor, with no fabricated voltage reading. Percentage is
the central radio's stock reading, not independent measurements of both packs.
Radio firmware, pairing, wireless transport and speculative radio-reset pins are
unchanged. This adds held Studio actions, not a Studio battery dashboard.

`tools/firmware-update/read_battery.py` reads a cache-only LEFT snapshot through
the updater's separate namespace `0x72`. It never resets, retries, writes flash
or sends radio commands. Run it separately from builds/tests, using the existing
firmware-update uv project after loading `scripts/project.sh`.

## Saved layout import

`tools/keymap-migration/vial_to_zmk.py` is an offline converter. It resolves the
maintained 72-position matrix geometry, preserves all four source layers and
translates supported HID, modifier, layer, lighting, OS-selection and battery
actions into symbolic ZMK behaviors. Unsupported actions are reported explicitly;
`--unsupported=unassigned` permits an explicit unassigned result. Active
unsupported macros or other auxiliary features still block conversion. Vial
tuning settings are not imported.

The separate `tools/firmware-update/studio_rpc.py` device client resolves runtime
behavior IDs and validates all 72 physical keys before a write. It requires a
clean saved map, creates a fresh private backup, preserves current layer names
and order, writes bindings, saves, reloads and compares the complete result.
Failures attempt to restore and verify the fresh original map. There is no
automatic reconnect or firmware update in this client. Personal exports, plans,
backups and full readbacks stay outside version control.
