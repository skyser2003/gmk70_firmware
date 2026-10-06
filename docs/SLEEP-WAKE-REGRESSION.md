# Sleep-entry wake regression

For failures that require power-cycling, use the new
[persistent debug-updater recorder](PERSISTENT-SLEEP-LOG.md). The RAM capture
procedure below still applies to applications that only have the earlier recorder.

On October 6, 2026, the user reported that the reconstructed V15 firmware turns
both halves' RGB off after approximately one minute of Bluetooth idle. After
waking the left half, the right half remains unable to type or light, and its
own keys do not wake it until power is cycled. The installed package was
described as the latest stock-behavior build; exact image hashes and matching
installation on both halves have not been captured.

## Reproduced source defect

The WB32 PAL wake callback records a wake cause, then disables every EXTI wake
interrupt. Previously, `lpwr_enter_stop()` armed those interrupts, released the
kernel lock, disabled CPU interrupts, and unconditionally entered MCU STOP.
A key or split-line interrupt in the gap could therefore run the callback
before STOP, leaving STOP with no enabled key/split wake interrupts. This can
leave either half unresponsive until another interrupt or a power cycle.

There was also a lost-edge window: `wb32_stop_mode()` cleared EXTI and NVIC
pending requests after wake sources were armed. An edge arriving before that
clear could be discarded before WFI.

The correction clears stale requests before arming the wake sources. With CPU
interrupts disabled, it checks whether a wake callback already disabled EXTI,
an accepted cause was recorded, or a radio worker delivered host resume. If so,
it skips WFI and restores the clock and matrix pins. Requests arriving after
arming remain pending so they can release WFI. State and wake-cause fields read
across interrupt/thread boundaries are volatile. A host resume recorded during
entry is preserved by the STOP return classifier.

Ordinary NONE/UART returns still retain STOP. Critical-battery preparation's
synthetic UART cause, without a wake interrupt, still allows STOP. Debug builds
record event 17, `MCU STOP entry cancelled`, when WFI is skipped. Events 4/5
describe the entry attempt and path return, which alone do not prove WFI ran.

This is a source-level correction with simulated interrupt regression coverage.
It does not establish that this race occurred on the user's keyboard or that
stock firmware is free of the same race. The recovered low-power machine-code
arrays are unchanged; electrical timing and WB32 STOP behavior need hardware
validation.

## Why one minute still needs a capture

The removed 60-second connected-radio idle branch is absent in the current
source. Other paths can still request sleep:

| Path | Current behavior | Evidence to read |
| --- | --- | --- |
| Stored idle selector | Selector 1 is 60,000 ms. Default selector 7 is 300,000 ms. | EEPROM `decoded.radio.sleep_selector` and `idle_sleep_timeout_ms`; `sleep accepted: manual=False` |
| Critical battery | Cached radio battery at 0%, without charging, can request sleep after 60 seconds. Its latch is sent to the peer and suppresses ordinary key/host wakes. | `sleep requested: low battery, context=0`; `low_battery_sleep=True` |
| Disconnected radio | A 30-second reconnect deadline requests sleep. | `sleep requested: radio disconnected timeout` |
| Radio host suspend | The radio can request sleep through its host callback. | `radio host resume=False`, followed by state/manual events |

MCU uptime excludes time in STOP. A firmware timestamp is not a wall-clock
measurement of the user's idle interval. The one-minute setting and the
critical-battery path remain hypotheses until the retained settings and sleep
reason are read. This correction does not overwrite the stored timeout or
reset EEPROM.

## Read-only capture and update package

After the symptom occurs, preserve power, close VIA, connect the left half by
USB, and select wired mode. In a configured repository environment:

```sh
bash scripts/read-eeprom.sh --target left --output symptom-left.json
uv run --offline --frozen --project tools/battery-diagnostics \
  python tools/battery-diagnostics/read_split.py > symptom-split.txt
```

With Python 3.12+ and `hidapi==0.15.0` on the PC owning USB, the portable package
contains equivalent readers:

```sh
python read_eeprom.py --target left --output symptom-left.json
python read_split.py > symptom-split.txt
```

Use the left target first: an unresponsive peer cannot answer a relayed right
EEPROM query. Read the two `decoded.radio` fields in
`halves.left.decoded.radio` and the last sleep request/acceptance events. Changing
mode or connecting USB may wake the left half and add events; the recorder keeps
only the last 128 events. A power cycle clears its RAM history.

The diagnostic reader accepts current `HS` and earlier `LEO` USB identities,
while retaining the VID/PID and raw-HID usage checks and rejecting multiple
matching devices. It sends only read commands.

The rebuilt `artifacts/qmk/debug-updater/debug-updater-update.zip` is for an
existing compatible split-updater installation. It includes the application,
manifest, unchanged resident image, updater and readers. Use the included
`README.txt` and `python update.py . --inspect` to check both halves before an
explicit installation. A dead peer cannot be updated through the left until
its communication is restored. Both applications must receive the fix.

No device was flashed, reset or queried while making this change. A Bluetooth
idle test followed by separate left-key and right-key wake tests is still
required before declaring the hardware symptom resolved.
