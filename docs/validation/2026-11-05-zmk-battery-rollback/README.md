# Rollback of the last battery trial

The user requested removal of the last attempted battery correction, a renewed
QMK/ZMK comparison, and an explanation of wired battery polling. The rollback
restores the exact application that preceded the November 5 full-input/trace
trial. Earlier charging notifications, wired reporting and USB sampler recovery
are preserved. The percentage-estimation problem is not claimed fixed.

## Scope and verification

Restored eleven source/test files from the task-start snapshot, restored the two
test wrappers changed only for trace variants, and removed the trace-only Python
test file. This removes the 1,000 ms full-input qualifier, worker trace metadata,
operation `0x72/0x23`, CLI `--trace` and the trace-specific freshness-order change.
The ordinary battery getter remains operation `0x21`. Research and captured
evidence remain available as historical records.

The canonical application is byte-identical to the saved pre-trial package:

- Application: 105,996 bytes, CRC32 `3957255507`, SHA256
  `88a1c6490e8543201a29b73fa13bd079d5d1c0bfbf8ef26dad8b716c492fd1b2`.
- Resident: unchanged CRC32 `1770733249`, byte comparison passed.
- RAM: 28,162 / 28,544 bytes, leaving 382 bytes unallocated. Stack reservations
  remain 14,336 bytes; this is not a runtime stack-watermark measurement.

All eight affected ZMK distributions build and pass image/package checks.
`make test` and `make check-tools` pass, including 92 repository tests,
189 updater-tool tests and 41 ZMK Python tests. Edited C and Python formatting,
lint, strict typing and shell syntax checks pass. QMK source was not changed,
so no new QMK firmware build was required; its native tests were included.

The dependency review found no FR8000/GMK70 battery driver in the pinned ZMK or
Zephyr sensor drivers. The existing board adapter already uses Zephyr's sensor
interface and ZMK's native reporting; the rollback adds no replacement gauge or
duplicate library implementation. No dependency or stock VIA definition changed.

Private source backup, build logs and the frozen LEFT case are under
`.work/zmk-battery-transition-rollback/`. The exact case expects the trial image
on LEFT and the pre-trial image on RIGHT, checks both resident identities and
forbids replay of an INSTALL request. Both-half 70-second baseline and after
checks passed with exact application/resident identities, zero transport/relay
retries and no watchdog feeds. One LEFT INSTALL was acknowledged and USB
returned without a requested reconnect or power cycle. Both applications now
have the pre-trial identity above; RIGHT received no write. Saved Studio
snapshots compare exactly, retaining all four layers with no unsaved edits.
The case contains its INSTALL marker and must never be replayed. No radio
firmware/RAM, pairing or resident write was made. Source was not committed or
pushed as part of this rollback.

## Remaining battery differences

The historical reference is Vial/QMK `vial-0.1.7`; current maintained QMK is also
inspected where it has since gained validation. Both main applications use the
same radio percentage, A7 cable input, A15 raw full input, UART3 PC10/PC11,
115200 8N1 framing, and charging/unplugged/full commands `A6 64/65/66`.
Neither main application uses a different voltage-to-percentage curve: the
separate stock radio performs that calculation.

| Path | QMK/Vial | Rolled-back ZMK |
| --- | --- | --- |
| Regular percentage inquiries | Every 3 seconds only on wireless transport in normal power state. | Every 3 seconds whenever the UART owner is available, including wired mode and awake idle. |
| Event-driven inquiries | Power-state assignments also request a one-off `A6 53`, without the regular wireless-only gate. | Percentage inquiry uses its existing cadence; native sensor fetch reads the cache. |
| USB while wireless typing | Stops/disconnects USB and disables the external D+ pull-up in the normal build. | Keeps USB available for Studio/updater; keyboard reports still go only to the selected transport. |
| Recovery after radio USB selection | No explicit 5.5-second quiet/restart interval. | Retains the earlier 5.5-second UART quiet interval that restored wired sampling. |
| Charger notification timing | Periodic, after more than 1,000 ms; queued in the FIFO. | Prioritizes a changed state when UART ownership permits, then refreshes after at least 1,000 ms. |
| Charger retries | Up to 50 retries at the ordinary control timeout. | One retry, then a later heartbeat; this avoids keeping queries/sleep blocked by a missing ACK. |
| Startup | Deferred two-second sleep-enable/version/reselection block; physical scanning can also select earlier. | Initial USB request, 50 ms physical selector stabilization, immediately pending sleep-enable controls and different worker scheduling. |
| Cached value validity | Historical Vial starts at 100 and retains replies without the later freshness fields; current QMK validates freshness. | Unknown until a valid reply, then a 10-second freshness limit. |

The one-second full qualifier and transition trace no longer remain as
differences. The earlier software fixes and the radio itself are preserved.
Zero radio errors in the captured jump do not establish retry or framing faults.
The Bluetooth-plus-USB reproduction also means wired percentage inquiries alone
are not proven to cause the high estimate.

## Why wired polling exists

There are two layers. Upstream ZMK's native battery reporter samples its chosen
sensor while active regardless of whether USB or Bluetooth owns typing. Its
ordinary default is 60 seconds. That is useful for a battery display while the
keyboard charges, and is the design used by the inspected Sofle configuration.

GMK70 additionally has a board-specific UART owner. Its three-second inquiry in
both modes was an integration choice made to keep the held battery indicator
and read-only USB telemetry current during wired charging. The native sensor
then reads that cache. It is not required by wired typing, Studio or firmware
installation, and the radio's Bluetooth Battery Service is independently owned
by the radio. A fresh UART reply can still contain an older ADC result.

The earlier 5.5-second interval was added because USB selection stops the radio
sampler, while continuous heartbeat/query traffic could prevent the sleep/wake
transition that restarts it. It supports this extra wired-reporting behavior;
it is not a native ZMK requirement or part of the last trial being rolled back.

Matching QMK's regular query scope would mean gating percentage inquiries, not
stopping the UART worker: charger-state notifications, incoming ACKs, host
selection and role/power handling still need service while USB supplies power.
Wired percentage freshness and the indicator's behavior would then need an
explicit policy. The current request asks why this differs and to remove the
last trial; it does not explicitly request that additional behavior change.

Source locations: `firmware/zmk/battery/battery.c`, `battery_sensor.c`,
`firmware/zmk/radio/core.c`, `firmware/zmk/boards/zuoya/gmk70_wired/studio.conf`,
`dependencies/zmk/app/src/battery.c`,
`firmware/qmk-common/drivers/wireless/wireless.c`,
`transport.c`, and `firmware/qmk-common/keyboards/zuoya/gmk70/gmk70.c`.
