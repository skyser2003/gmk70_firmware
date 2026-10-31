# GMK70 ZMK idle matrix scanning

The native GMK70 scanner now stops local row/column sweeps when all five local
rows are released and the existing 5 ms debounce has settled. It drives the
populated columns low through open drains and arms falling-edge interrupts on
the row inputs. A row interrupt latches activity; the existing scanner thread
resumes GPIO sweeps and debounce on its next maintenance cycle. Interrupt
handlers never interpret bindings,
send reports, run split transactions or write settings.

This applies to both roles in the universal image. Both halves need the new
application to gain the local scanning change. The split protocol, resident
ABI, updater admission, saved keymap/settings and no-watchdog policy are
unchanged. There is no added thread or stack reservation.

## Transition and failure handling

An idle matrix has no raw candidate or debounced held key. Held modifiers and
pending releases therefore keep the fast scan path active. On entry, interrupts
are armed before columns are driven, then all rows are sampled again. A key
already down during the transition requests another scan without needing a
second edge. Pending edges after that sample remain latched by the interrupt.
On exit, row interrupts are masked and columns return to the normal scan state.

The WB32 GPIO driver implements rising, falling and both-edge EXTI callbacks.
Its AFIO line mapping, RCC clock bits and seven IRQ groups follow the pinned
Westberry CMSIS/ChibiOS definitions. It rejects level-trigger requests and
conflicting ownership of the same numbered EXTI line by different ports. The
handler acknowledges only the observed enabled GPIO lines in its own group;
other GPIO groups and non-GPIO EXTI sources are preserved.

If a row interrupt cannot be configured, partial row arms are undone and the
scanner retains ordinary polling. It does not take another port's EXTI line.

## What continues while the local matrix is idle

The existing scanner thread still runs its maintenance cycle: LEFT polls RIGHT
over the request/response UART link, publishes peer state, handles disconnect
releases and services lighting. Its 1 ms pause remains. The separate updater,
radio, settings and USB workers retain their existing scheduling. A right-side
press therefore does not depend on a new left-side key press to reach the host.

This is interrupt-triggered **local matrix scanning**, not system deep sleep.
It removes idle column-drive/read sweeps but does not make the whole application
wait indefinitely for a key interrupt. `CONFIG_ZMK_SLEEP` and system PM remain
disabled; no radio sleep policy, clocks, LED auto-off timing or USB suspend
behavior is changed. Battery-current savings have not been measured. Matching
Sofle's whole-system low-power behavior needs a separate WB32 PM design that
also handles split wake, radio, USB, timers and peripheral restoration.

## Verification

`make test-zmk` runs a native fixture with the actual maintained GPIO driver,
WB32 matrix/UART adapter and debounce implementation. MMIO registers, IRQ
dispatch and GPIO callback lists are simulated; no keyboard is accessed. It
covers both physical roles, 1,000 idle calls without another GPIO sweep, held
keys/releases, row A1 and C13 interrupts, a press during arming, a queued edge
after the input returned high, EXTI ownership conflicts and polling fallback.
The central still retrieves peer keys while its own matrix is idle. Existing
kscan tests retain remote-only key events, disconnect releases and callbacks.

ARM builds/image checks must pass for the supported ZMK variants. They check
the resident prerequisite, application bounds and retained RAM boundaries.
Physical wake/typing/RGB/USB/Studio/updater checks and battery-current
measurements remain pending; native tests are not hardware confirmation.

See the [offline validation results](validation/2026-10-31-zmk-idle-scanning/README.md).
