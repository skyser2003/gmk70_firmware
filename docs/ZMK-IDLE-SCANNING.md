# GMK70 ZMK idle matrix scanning

The native GMK70 scanner now stops local row/column sweeps when all five local
rows are released and the existing 5 ms debounce has settled. It drives the
populated columns low through open drains and arms falling-edge interrupts on
the row inputs. A row interrupt schedules scanning and debounce; the native
key event updates activity only after a real matrix transition. Interrupt
handlers never interpret bindings, send reports, run split transactions or
write settings.

This applies to both roles in the universal image. Both halves need the new
application for event-driven split input. New peers negotiate that extension;
older peers retain polling. Resident ABI, updater admission, saved keymap/settings
and no-watchdog policy are unchanged. See [event-driven power](ZMK-EVENT-POWER.md)
for worker deadlines and the reused initialization thread.

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

With `GMK70_EVENT_IDLE` enabled, the scanner waits for GPIO/UART notifications
or its next input, transport or lighting deadline. Quiet updated peers exchange
health snapshots every 500 ms, while held keys and active debounce retain the
fast path. A right-side tap is queued until acknowledged. CRC failures and
master restarts preserve pending snapshots; completed legacy capability replies
admit the old polling path.

The service worker also waits on real radio, battery, settings, USB-updater and
power deadlines. Routine traffic schedules work without raising native activity.
Sleep preparation silences maintenance before the UART wake barrier. The kernel
was already tickless; these waits allow it to remain in normal CPU idle longer.

Normal interrupt wait preserves application state. Coordinated inactivity sleep
also supports returning STOP with retained SRAM/input state and calibrated RTC
time catch-up. A foreign RTC configuration or incompatible hardware option uses
the existing STOP/reset fallback. Clock/timebase restoration has native regression
coverage; physical retention, first-key delivery and current consumption still
need hardware qualification. This does not establish Sofle-like battery lifetime.

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
