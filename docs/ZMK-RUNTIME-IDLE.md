# GMK70 power between keypresses

The target is to retain the keyboard state and reduce power between events,
without waiting for the ordinary five-minute sleep timeout or interrupting
Bluetooth, dongle, USB, held keys, settings or the resident updater.

## Feasibility result

WB32FQ95 supports returning STOP with SRAM retained. It is already used by
[coordinated full sleep](ZMK-EVENT-POWER.md). That does not establish that the
current transport can safely enter STOP during ordinary connected operation.

The manufacturer's RM2905025 Rev01, sections 5.3.3–5.3.4 and 7.2–7.3, describes
the distinction:

- WFI Sleep stops CPU execution while peripheral clocks remain available.
- STOP stops the fast oscillators/PLL and peripheral clocks. Wake selects MHSI;
  clock configuration must be restored. SRAM retention does not buffer serial
  bits received while the UART clock is stopped.
- MAINCLK supplies AHB/APB and USB. Switching MAINCLK is therefore not a
  transparent way to stop the CPU's PLL while preserving the existing UARTs.

DS1104041 Rev01 table 5.2-9 allows up to 200 microseconds for PLL lock alone.
A nominal 115200-baud 8N1 byte takes about 86.8 microseconds. Restoring the
current clock configuration can therefore lose complete incoming bytes, even
if a GPIO edge wakes the CPU immediately. A fast typical wake is insufficient
to qualify the whole supported range.

The current split event protocol acknowledges matrix snapshots but has no
negotiated wake-before-header handshake. The stock radio UART likewise has no
established readiness handshake in this integration. QMK's stock-compatible
full-sleep setup explicitly excludes radio RX C11 as a wake source; B6 being
armed is not evidence that the radio asserts it before every transmission.
An empty FIFO at STOP entry cannot exclude a new asynchronous transmission.

Consequently, **unconditional brief STOP is not enabled**. This is an unresolved
integration requirement, not a claim that the MCU lacks STOP or that a future
transport design is impossible.

## ZMK implementation

Keep the current UART-safe WFI fallback and gate inactive RGB-engine clocks:

- Enable SPIM2 and DMAC2 clocks under the existing pixel mutex before accessing
  their registers. Use the atomic peripheral bit-band writes documented in
  RM2905025 section 2.3.1 and complete clock enables before peripheral access.
  Restore clock access without resetting the controller.
- After initialization or a completed/aborted transfer, disable the SPIM2 clock
  only after it is stopped. Gate DMAC2 and its AHB bridge only when no DMA
  channel is enabled; preserve another channel's clocks and unrelated RCC bits.
- A failed DMA abort retains clocks and the existing buffer ownership/failure
  handling. LED power sequencing, waveforms and battery indicators are unchanged.

This applies between animation frames as well as while normal RGB is off.
It does **not** shut down the 96 MHz PLL, lower the CPU frequency, or establish
Sofle-equivalent power consumption. No new thread, timer, setting, watchdog or
radio command is involved.

The alternatives considered were reusing full sleep after each event, changing
MAINCLK around WFI, and gating only idle peripheral clocks. The first two lack
qualified UART reception; peripheral clock gating is the implementable first
part of the lighter-idle fallback already discussed with the user.

## Offline checks and their limits

Run `source scripts/project.sh`, then:

```sh
bash tests/zmk/run_event_idle_tests.sh
bash tests/zmk/run_pixels_tests.sh
make test test-zmk check-tools
GMK70_TOOLCHAIN=xpack make build-zmk-wired-studio-updater build-zmk-standalone
```

The event-UART fixture feeds the actual maintained ISR queue/parser with a
bounded byte-delivery model with clock-off counterexamples at
87/100/150/200/250 us. These unguarded windows lose the header. Keeping the
UART clock available, or explicitly postponing data until after readiness,
preserves the same frame.
The model assumes UART clock loss and illustrates that integration constraint;
it does not simulate WB32 silicon, measure wake latency, or prove a future
handshake correct. No installable runtime-STOP candidate is produced.

The pixel fixture checks clock access on every SPI/DMA MMIO operation, prevents
clock removal while DMA is active, preserves a second channel and unrelated RCC
bits, and exercises real maintained encoding, LED power, indicators, error
recovery and subsequent transfers. Canonical ARM builds must still fit their
existing flash/RAM slots and preserve resident/settings compatibility.

Verification passed with pinned xPack GCC 14.2.1: `make test test-zmk check-tools`,
the final pixel/event fixtures, and both canonical ARM builds/image checks.
The updater occupies 106,496/106,496 flash bytes; standalone occupies
101,216/114,688. Both grow by 56 bytes with unchanged RAM reservations.
The updater retains resident ABI1 and padded resident CRC `0x698B3EC1`.
No physical device was accessed, flashed or reset.

## VIA and Vial implementation

The same idle RGB clock policy is implemented for both QMK families in
`firmware/qmk-common/keyboards/zuoya/gmk70/rgb_idle.c`. Their pinned ChibiOS
WB32 driver feeds SPI through interrupts rather than ZMK's direct DMAC2 path.
Use its normal `spiStop`/`spiStart` lifecycle to gate SPIM2; do not take ownership
of other DMA channels, the settings QSPI bus or UART clocks.

Both builds link wrappers around the unchanged upstream WS2812 initialization
and flush functions. Save the native SPI configuration before `spiStop` clears
its pointer. Park after initialization, then retire completed frames from the
existing housekeeping task. Check both READY and the hardware BUSY flag before
stopping; an active frame keeps its clock and borrowed encoder buffer. A
contended housekeeping call skips parking rather than blocking the main loop.

The next frame restores the native configuration, pin mux and reset-low guard
before dispatching the original asynchronous encoder/transmitter. Overlapping
flushes cannot rewrite an in-flight buffer: a pending flag coalesces the latest
colors and housekeeping submits them after completion, without requiring a
future animation tick. Sleep/suspend can park that pending work but cannot
transmit it until normal operation resumes. The board's full-STOP
clock-restoration hook leaves an already parked SPI driver off until that next
frame. LED supply policy, saved
RGB state, battery/charging overlays and both framework-specific half mappings
remain with their existing owners.

`tests/qmk/run_rgb_idle_tests.sh` links the actual pinned encoder from each
framework with the maintained adapter and a bounded HAL model for both 36- and
41-LED halves. It checks repeated frames, asynchronous completion, shifter BUSY,
mutex contention, borrowed-buffer preservation, config-pointer clearing,
restart/reset timing and parked/active full-STOP restoration. The original
native encoder baseline and board battery/RGB tests remain in `make test`.
These are software checks; current consumption and physical waveforms still
require hardware measurement. Brief runtime STOP remains unqualified for all
three builds because it needs the separate wake/data integration above.

The VIA/Vial extension passed `make test check-tools`, including both native
encoder fixtures and the shared build-integration checks. All five QMK variants
(release/debug normal and updater, plus fixed default), all three Vial variants
(normal/updater/unlocked updater), and both canonical ZMK variants built.
The final QMK/Vial ELF driver tables reference the wrappers, their linker heap
pools remain positive, and updater packages retain the pinned resident digest
and CRC. No release or physical installation is performed by these checks.

Hardware current savings remain unmeasured. Before a brief-STOP implementation,
establish a reliable radio wake/data path, negotiate split readiness, wake for
the next kernel deadline using a low-frequency timer, and qualify first-byte,
held-key and first-key delivery. The full-sleep RTC's coarse hourly alarm is
not a general runtime deadline scheduler. The ZMK updater's full application slot
is another constraint; it must not be silently exceeded or solved
by changing the resident/settings layout.
