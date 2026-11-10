# GMK70 event-driven idle power

The user requested all of the idle power improvements discussed on 2026-11-09,
with ordinary typing, settings, mode changes and the resident updater preserved.
Routine split communication must neither refresh inactivity nor keep a sleeping
keyboard awake. Hardware current and Bluetooth wake qualification remain separate
from offline verification.

## Approach

Keep the existing pinned ZMK/Zephyr sources and stock radio firmware. Replace
millisecond background polling in the maintained board integration with IRQ
notifications and deadlines. Negotiate a versioned runtime event transport over
the existing split UART; retain legacy polling for older peers. Replacing the
radio or changing the electrical bridge is unnecessary for these changes.

The kernel is already tickless. Normal idle preserves CPU/application state
using its interrupt wait. Coordinated inactivity sleep now has a returning STOP
path that retains SRAM and input/framework state. The previous STOP/reset path
remains the fallback when hardware options or RTC ownership prevent returning
sleep. Source-level tests and ARM builds qualify the integration; physical
retention, first-key delivery and current consumption still need hardware tests.

## Boundaries

- GPIO key/encoder interrupts wake the scanner. Scan with normal debounce while
  physical inputs are active; otherwise wait for input or the next real deadline.
- Updated peers negotiate an event capability before sending any unsolicited
  bytes. Bounded, acknowledged matrix snapshots preserve press/release ordering,
  reject corrupt replies, retry notifications and recover after reconnect.
  Firmware update frames and RPC authorization remain unchanged.
- Maintenance heartbeats only refresh transport health. Only native key/encoder
  edges and pointing activity update user inactivity. Quiet peer leases may be
  longer, but held keys retain the existing short disconnect-release deadline.
- A sleep preparation stops routine work before wake IRQs are armed. Both halves
  must quiesce and acknowledge; key races abort sleep. No heartbeat or queued
  notification is permitted after committing sleep.
- UART interrupts wake the existing service worker; parsers retain single-thread
  ownership. Battery queries, retries, settings saves and mode debouncing use
  deadlines. USB/settings/updater mutation gates and saved formats are preserved.
- No watchdog, new thread, dependency edits or physical device operation.

## Resource and build integration

The native initialization thread becomes the scanner after pinned ZMK main has
completed settings initialization. Its native essential flag is cleared, then
its priority is set to the scanner's
existing priority 5. Main reserves 1536 bytes, preserving the scanner reservation
while eliminating its separate stack/thread. The serial responder and storage
worker remain independent; active stacks are never overlaid. ELF verification
requires the real main/priority/scanner call order and rejects trial entry paths.

The updater's application still ends before the original settings banks. To fit
the new runtime transport, use the pinned compiler through Zephyr's supported
GNU Arm Embedded toolchain integration, local ISR declarations and selective
LTO. Files defining wrapped native functions stay outside LTO; even `noipa`
does not prevent LTO from bypassing GNU linker interposition. Maintained board
owners can share optimization, with explicit out-of-line image-check boundaries
for initialization, clocks, storage admission and report routing. Resident bytes,
application/staging addresses and settings schema/addresses are unchanged.

## Verification

Exercise actual maintained adapters with bounded scheduler/UART/MMIO fixtures:
quiet input, debounce, short taps, simultaneous halves, held keys, disconnects,
CRC/retry/duplicate frames, negotiation fallback, IRQ/wait races, timer wrap,
maintenance traffic across inactivity and sleep commit, radio deadlines and USB
mode transitions. Run repository/tooling checks and both canonical ZMK variants;
check flash/RAM budgets, settings layout and resident ABI. Preference save
debounce/retry logic remains authoritative, with 250 ms worker windows only
while dirty writes are permitted. Explicit indicator deadlines preserve
pairing/OS-feedback phases even with ordinary RGB off. Shared QMK source
changes, if needed, require VIA and Vial builds as well.

Before deployment, hardware must verify wake from either half, first-key delivery,
Bluetooth/dongle reconnect, USB/Studio/updater operation and measured current in
active, quiet-connected and full sleep states. Offline tests do not establish
months of battery life.

## Transport limits

Both updated halves are needed to remove quiet legacy matrix polling. Capability
negotiation precedes any legacy read, so a restarted master cannot disable a
retained queue before retrieving it. Rekey changes the session tag and disarms
notices until a valid event poll; it preserves unacknowledged snapshots. A reset
between publishing an event and acknowledging it can replay that outstanding
event. The eight-entry queue coalesces its newest tail on overflow; it guarantees
a current final state rather than unbounded history.

The admitted interrupt table covers IRQs 0 through 34 when returning STOP is
enabled (RTC alarm34), or through 33 for event idle alone. Startup still
disables/clears all 64 physical NVIC lines before driver initialization. Resume
uses a clock-only routine and preserves pending interrupts. A higher IRQ driver,
such as USB wake35, requires raising the admitted bound before use.

## Verification result

Both canonical ARM builds pass the final image checks with the pinned xPack
GCC 14.2.1 toolchain. The resident ABI1, padded resident bytes/CRC, application
and settings boundaries, VIA definition and saved settings format are unchanged.
Dependencies remain pinned and unmodified. No keyboard was flashed or reset
during offline validation. Hardware qualification and release publication remain
pending.

| Variant | Flash bytes / slot | Reserved RAM / capacity | Unallocated RAM |
| --- | ---: | ---: | ---: |
| Studio updater | 106,480 / 106,496 | 28,020 / 28,544 | 524 |
| Studio standalone | 101,136 / 114,688 | 27,172 / 28,544 | 1,372 |

These figures include the unified [Studio controls menu](ZMK-BASIC-PARITY.md#integration-and-operation).
Its metadata initializes 624 bytes of static RAM from compact name tables,
trading RAM for flash while retaining the descriptive dropdowns. No heap or new
thread is involved. The profile disables unused dynamic settings handlers,
already-inactive time slicing (the previous slice size was zero), and device
power-domain support (no enabled device has a `power-domains` dependency).
Ordinary device suspend/resume and static settings handlers remain enabled;
image checks reject new dependencies on the disabled facilities.

The updater has 16 flash bytes spare. Bounds checks remain strict; a future
compiler or feature change may require more size work. Unallocated RAM is a
linker quantity, not a measured runtime peak. Stack reservations are unchanged
except that main now owns the scanner's reservation and its separate thread is
removed. Serial, storage, system/USB work queues and interrupt stacks remain
independent; no active stacks are overlaid.

The compiler reports a 176-byte retained-sleep frame, 256-byte service-worker
frame, 488-byte settings-save frame and 296-byte flash replacement frame.
Metadata was generated for the LTO program and 28 non-LTO translation units;
the instrumented link's load BIN is byte-identical to the candidate. Individual
frames are not complete call-chain bounds or physical high-water measurements.

Native fixtures cover actual pinned SysTick and timeout code, COUNTFLAG read
side effects, raw RTC halfword rollover/resynchronization, 20/40 kHz calibration,
sub-tick carry, 600-hour sleeps, quiet hourly checkpoints, coincident key/alarm
wake, pending IRQ deferral and foreign backup/reset options. Native PM and split
fixtures check report admission, retained tap queues, lease rebasing, native
activity timestamp renewal and the reset fallback. None is a silicon or battery
current measurement.

The final linker checks require a strong SysTick timeout setter with actual
stores and live call paths from both native announcement and retained freeze.
Both its real driver and weak fallback definitions are outside LTO; otherwise
IPA can eliminate calls against the weak no-op. They also require native main
before scanning, guarded settings/flash admission, report routing and the actual
USB Feature registration. Audits find all 35 updater wrap targets (36 native
strong/weak definitions), and all 34 standalone targets, outside LTO.

For endpoint, keymap mutation and RGB exports, small non-LTO public ABI thunks
forward to uniquely renamed implementations in the untouched native sources.
Source-specific preprocessor definitions apply only to those original TUs, so
native same-TU calls keep their prior semantics. No keymap, transport or rendering
logic is duplicated. A host LTO regression links the actual maintained thunks
and checks all 13 exports, structure arguments, public versus `__real` calls,
function pointers and native internal calls. ARM disassembly verifies the actual
wrapper → public thunk → native implementation paths.

Repository/ZMK tests, tooling checks, both canonical ZMK builds and seven affected
VIA/Vial variants pass. Candidate packaging includes only direct BIN/checksums
or the five-file updater ZIP (application, resident, installer, manifest and
checksums). The exact resident hash/CRC and all distribution checksums are
verified. Physical wake/first-key, USB/Studio/updater, Bluetooth/dongle reconnect
and current/stack-watermark tests remain pending.

## Returning STOP implementation

The user requested continuing beyond the initial STOP/reset boundary. The exact
WB32FQ95 manual confirms SRAM/register retention with PWR_CR0[18:19] clear and
PDDS[6:5]=0, MHSI on wake, and reset RCC configuration. SYS_RSTCR[1] is a read-only
STOP-reset option; a returning path must refuse that option. The existing
0x3B004/0x3B3 sequence retains both SRAM banks; the manual's SP4 table value
0x7B004 powers off the 32 KiB bank and must not replace it.

Implement returning full suspend in the existing service owner, after split
quiescence. Native activity continues to set policy, but its workqueue may not
enter the non-returning power-off API for an admitted returning session. Keep
native USB shallow sleep and reset fallback for unavailable RTC/retention.

The chosen clock design uses one-time, non-destructive LSI RTC ownership,
calibrated for 64 ms against active crystal-based cycle time. Backup registers
DR20 and DR21 are full 32-bit ownership/version words (`0x474D4B37` and
`0x52544301`). Claiming requires an unconfigured clock source and both words
blank, or the exact existing ownership/version. Foreign values, a foreign RTC
source/alarm or the read-only STOP-reset option retain the reset fallback; the
backup domain is never reset. Prescaler registers are write-only and are never
used as evidence of ownership. Stop/resume
native periodic activity/battery/RGB timers around catch-up (and WPM/display
timers when configured); other one-shot
kernel timeouts expire normally at wake. Native SysTick retains its active-time
bookkeeping; sleep whole ticks are announced separately and a protected offset
keeps exported cycle counters continuous. Exercise the actual pinned timer and
kernel timeout implementation to reject duplicate/missing ticks. Synchronize
SysTick immediately before disabling it, including COUNTFLAG read side effects.
Carry fractional slept ticks across sleeps; the independent active/sleep phases
can differ from a combined phase by less than one kernel tick. Hourly alarms
use a rounded frequency only for their coarse deadline; elapsed time uses the
unrounded calibration ratio. Calibration does not compensate later temperature
or voltage drift of the LSI oscillator.
Hold the scheduler while catching up timers and restore clocks before enabling
interrupts. No key/modifier state is cleared on wake.

RTC-only hourly checkpoints bound raw-counter wrap; they neither resume the
keyboard nor send peer/radio traffic. Physical wake propagates to the peer after SysTick is running; incoming peer
wake edges are not echoed. Rebase the paused split lease after elapsed-time
catch-up so the wake tap is retained. Restore report admission before reopening
scanning. Refresh the native activity timestamp through its existing pointing
work item, without creating a fake key. Returning STOP therefore requires the
canonical synchronous pointing integration. Optional upstream wired polling
backends require a separate timer/wake audit before use with this integration. Native dependencies and
resident/settings addresses remain fixed. The returning path remains subject to
hardware qualification; the source implementation and offline tests do not prove
silicon retention, current consumption or oscillator stability across temperature.

Sources (public vendor documents mirrored by emolitor/WestberryTech-WB32):

- [FQ95 reference manual](https://github.com/emolitor/WestberryTech-WB32/blob/main/vendor-docs/reference-manual/EN_RM2905025_WB32FQ95xx_V01.pdf),
  §§3.3.6, 5.3.4, 5.4.1, 7.2.8–11, 17.3; SHA256
  `10fe86f3e26cf83b4a2f3486fd208896028f7472d09dbd26a2c7bb441a818c3c`.
- [FQ95 datasheet](https://github.com/emolitor/WestberryTech-WB32/blob/main/vendor-docs/datasheets/EN_DS1104041_WB32FQ95xC_V01.pdf),
  table5.2-8 gives LSI20–40 kHz; SHA256
  `36512aa726f3022e7b0377c7b012fb8208c6ebd2f5dfbc46a1ee2852730a189a`.
- Pinned vendor ChibiOS `demos/WB32/RT-WB32F3G71-RTC/main.c` demonstrates
  return through the identical PRE/POST sequences, clock restore and RTC alarm;
  its sibling-family assumptions are cross-checked against the exact FQ95 manual.
