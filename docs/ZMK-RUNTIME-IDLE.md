# ZMK power between keypresses

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

## Implemented first step

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

Hardware current savings remain unmeasured. Before a brief-STOP implementation,
establish a reliable radio wake/data path, negotiate split readiness, wake for
the next kernel deadline using a low-frequency timer, and qualify first-byte,
held-key and first-key delivery. The full-sleep RTC's coarse hourly alarm is
not a general runtime deadline scheduler. The updater's full application slot
is another constraint; it must not be silently exceeded or solved
by changing the resident/settings layout.
