# Recovered V15 sleep, wake and split behavior

This follow-up implements confirmed differences against the supplied stock V15
application. The reference is the 80,220-byte application with SHA-256
`3048ee6390a42f6c81a28df3dc9f032a926fe843d904ea9154a0554dae2af0b0`.
The preceding repository baseline is `d4e1c2b`. The complete firmware is still
not verified equivalent; see [the reconstruction report](STOCK-V15-RECONSTRUCTION.md).

The [2026-10-09 recheck](STOCK-V15-RECHECK.md) found seven additional differences
outside the earlier isolated test boundaries. All seven are now fixed and
covered by additional complete-task comparisons. This is not a claim of
complete firmware equivalence.

## Implemented contracts

| Contract | Recovered stock evidence and implementation |
| --- | --- |
| Peer sleep | PRE sends `AA 00 00 00 00` in every preparation class. The receiver ignores the body, requests ordinary manual sleep and preserves its own critical flag. Stock PRE/receiver: `0x08002CE0`, `0x080051CC`. The former port propagated the left critical flag and could make the right reject all matrix/split wakes. |
| Radio host events | Resume wakes only ordinary PRE/STOP. Suspend marks a pending manual request in every phase; subsequent state assignment clears it. The marker is volatile across the radio worker and main task. Stock callback: `0x08002B2C`. |
| Preparation classes | Track ordinary 0, USB special 1 and critical battery 2 separately from the raw request flags. NORMAL evaluates ordinary, critical, then USB special, including each state assignment. Stock power task: `0x08002D60`; assignment: `0x08002890`. |
| Held keys and timing | PRE bypasses held keys for either special class. STOP requires previous PRE/STOP; ordinary STOP waits for release and 200 ms. Critical STOP bypasses held keys but keeps 200 ms. The first USB-special STOP resets matrix and held count immediately; repeated USB STOP waits for release and 200 ms. WAKE completion waits 200 ms without a held-key gate. Gates: `0x08002838`, `0x08002864`, `0x08002C28`, `0x0800262C`. |
| Power clock | Use the local `timer_read32`/`timer_elapsed32` for all power state/activity deadlines and accepted-idle diagnostics. Peer synchronization corrections cannot expire local idle or the 200 ms gates. Stock: `0x0800E784`, `0x0800E80C`. |
| Mode synchronization | Selecting a device, including the same device via peer 55, moves PRE/STOP to WAKE and clears the active preparation class while retaining independent raw request latches. Stock: `0x080028D6`–`0x080028E8`. |
| PRE timing | Prepare inline through the board callback; avoid the modern ChibiOS suspend routine's additional 17 ms. Stock changes split A10/A9 once after the peer request only on a wireless master. The peer retains UART mode until actual STOP preparation. PRE: `0x08002DEE`. |
| Wake inputs | Configure split RX A10 last as floating input on both edges, ordinary encoder B6 on both edges, A7 cable on rising edge, and master C14/C15 selector inputs on both edges. Ordinary matrix columns follow stock's master predicate. USB special retains C13 and the column selected by left-table C3 (right C7); critical arms no matrix IRQs. Unselected columns become pull-up inputs and rows become pull-down inputs in either special class. B6 is ordinary-only. Radio RX C11 is not a wake source. Preserve encoder/selector/cable boot GPIO modes. A10 and B6 classify as MATRIX; C13 classifies as ONEKEY. GPIO setup: `0x080034A0`, `0x08003510`; classifier: `0x08003848`. |
| Interrupt groups | Arm only the required EXTI vector groups. A callback disables/clears its own group, preserving independent groups. V15 handlers: `0x08003880` onward. Preserve the repository's STOP-entry guard and clear stale requests before arming; these avoid discarding a new edge or entering WFI after servicing it. |
| Lighting recovery | Restore LED power based on the saved matrix enable state, including brightness zero, rather than merely testing brightness. Stock wake: `0x08002F96`. |
| Early split restoration | Restore A9/A10 to alternate mode `0x382` immediately after clock restoration, before CPU interrupt enable, including rejected wakes. Do not defer restoration until normal WAKE completion. Stock return: `0x080036FC`. Stock normal wake relies on the restored split line and subsequent synchronization rather than an additional CC command. Since 2026-10-16, this port additionally pulses the non-master's split TX on a local ordinary wireless wake; see [the right-key wake candidate](HARDWARE-VALIDATION.md#october-16-right-key-wake-candidate). |
| Delayed framework wake | Since 2026-10-16, the 200 ms board recovery dispatches quantum lighting/module/board initialization without ChibiOS's input-state clear. This adapts the modern framework integration to scanning already active during WAKEUP. Native USB resume and physical host switches keep the full reset. See [held-input validation](HARDWARE-VALIDATION.md#october-16-held-input-during-delayed-wake). |
| Radio before STOP | Lower both LED power enables. USB with cable sends no radio selection command. Special classes select radio USB unless the previous power state is STOP. Ordinary disconnected sleep selects USB and waits 300 ms. Drain the command queue before STOP. Enter physical MCU STOP only when the selected device matches its PRE snapshot and selection is not USB with the cached cable flag set. Stock task: `0x08002EA8`–`0x08002F36`. |
| Special recovery | Only USB/cable clears an active special class; key/split/switch wakes retain STOP. Dispatch directly by class and assign the state once. Rejected special wakes retain previous STOP so another STOP return can recover. Accepted special wakes assign WAKE before clearing the active class and raw latch. A physical USB resume delivered through QMK's board wake callback supplies the USB cause. Stock special post-hooks: `0x08002C80`, `0x08002CB0`. |
| USB host suspend | USB device selection plus READY/SUSPENDED plus cable requests ordinary manual sleep after seven seconds. This is a separate host-suspend latch, not USB special class 1. The wired record hook wakes PRE/STOP and clears the active class. The old imported `process_action_kb` callback is not called by modern QMK; the port now invokes wired wake from the supported record hook. A qualifying suspended-host event consumes the host latch once, resets its deadline, requests remote wake when permitted, then waits one second. Basic keys remain suppressed while the host is still suspended. Stock: `0x080032F0`, `0x0800335C`. |
| Hidden USB controls | Recovered stock `7E11` cancels USB special sleep and wakes NORMAL/PRE/STOP; `7E12` requests it; `7E13` toggles it through the same setter (`0x08003CE8`). Press-only handling, with no invented master gate. Dispatcher/setter: `0x08003F4C`, `0x08003CBC`. These slots are absent from the stock VIA menu but can exist in saved mappings. |
| Cable snapshot | On ordinary acceptance, master captures A7 into the radio word's runtime bit; non-master forces that bit on. Both roles reject ordinary sleep in active USB after consuming the request. Refresh the master bit from A7 before every power-task pass, including PRE/STOP and battery/charging consumers. Predicate: `0x08002B90`; master refresh: `0x08002D6C`. |
| Battery decision | Use the same two-second percentage snapshot as peer/battery display. With the cached cable flag clear, 0% for more than 60 seconds requests local critical class; readings 1–15% do not reset its deadline, while charging or >15% do. Preserve this protection independently of the five-minute idle selector. Stock: `0x08004A2C`. |

## Verification

Native ASan/UBSan tests exercise the maintained state machine, WB32 driver,
board wake/STOP hooks, radio queue and USB transport with simulated time,
registers, host states and acknowledgments. They cover rejected/accepted wakes,
repeated arming, preservation of other interrupt groups, both halves, all three
classes, held keys, simultaneous requests, USB seven-second/cable gates and
one-shot remote host wake. Existing EEPROM, keymap, lighting, VIA, updater and
diagnostic tests remain part of `make test`; tooling checks use `make check-tools`.

Local Capstone/Unicorn comparisons execute the actual stock Thumb instructions
and the built release/debug ARM routines at bounded I/O boundaries. They are
not an official device emulator, and do not run physical STOP, USB or RF.
Follow-up evidence is under `artifacts/analysis/stock-v15-power-followup`;
the preceding difference report remains under `artifacts/analysis/right-half-sleep`.

Validation on 2026-10-09 passed `make test`, `make check-tools`, all four
normal/updater release/debug builds and their HEX/BIN checks. The final normal
release/debug ARM routines passed 1,238 bounded power comparisons against stock
and 12 broader reconstruction checks covering VIA, lighting controls, state
assignment, default tables and USB identity. The additional recheck scenarios
validate all seven fixes against stock, including full STOP sequencing and
local versus synchronized clock behavior. These checks do not prove complete
firmware equivalence or physical wake behavior.

Debug-updater retains the persistent recorder and read-only right-half
settings/log relays. Both halves need matching applications. The resident
updater is unchanged from the preceding package.

## Remaining limits and hardware checks

Stock's first USB-special STOP also clears older internal key-record buffers.
Modern QMK provides no public reset for those private buffers; this port resets
the matrix/debounce and recovered held counter without inventing report writes.
Exact old tap/hold buffer semantics remain part of the framework equivalence gap.
USB callbacks use modern QMK's event delivery, rather than copying stock's USB
driver. Low-power blob instructions remain unchanged. Electrical timing, USB
resume and STOP clock restoration require physical validation.

For the reported Bluetooth failure, reproduce idle intervals around one minute
and five minutes, then verify keys on either half independently wake both halves.
Also exercise encoder wake, charged/uncharged operation, a suspended wired host,
and mode changes during entry/recovery. Read persistent logs after any recovery
power cycle using [the recorder instructions](PERSISTENT-SLEEP-LOG.md).
No physical device was read, reset or flashed by this development workflow.

The default idle selector still means five minutes; a saved selector 1 still
means one minute. The critical-battery deadline remains one minute. No captured
failure establishes which path triggered the user's incident. Preventing a peer
from inheriting the critical flag addresses a confirmed divergence, but does
not establish the cause or prove the symptom resolved on hardware.
