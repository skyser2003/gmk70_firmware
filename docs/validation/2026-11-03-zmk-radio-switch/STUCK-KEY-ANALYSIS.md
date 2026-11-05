# Repeating key followed by Bluetooth silence

The clear-routing correction described here is now included in the
[November 4 installed sleep-handoff image](../2026-11-04-zmk-sleep/README.md).
The image identities and unflashed status below describe this earlier checkpoint.

The user clarified the earlier profile-switch incident: input became slow,
one character repeated as though held, then typing stopped. The physical mode
slider restored wired typing without restarting. This is additional information
about the earlier incident, not a reported recurrence on the corrected image.

The strongest explanation is **lost release delivery combined with a stalled
radio transport or connection state**. No main-MCU per-switch heap leak was
found. The original trigger is not proven: there was no failing UART/RF trace,
and the 96 post-recovery errors are an aggregate counter.

## Provenance

The incident image was 105,972 bytes, CRC32 `1734310917`, SHA256
`e940c81a45798f74480c742efc7ea4cbd7936ea141be22cb0ed7d3d53a8c646a`.
Its core was reconstructed from the frozen battery-investigation patch against
base `e5e8a8dd365b132d58fe5714fac5a5b9f0b25f84`; `core.c` SHA256 matches
`c8ce5403ebcf468af2eedd1d251505fc20e9b9964eff309e7ac3f34bbf818b17`.
The earlier basic-parity overlay is a different baseline: its blanket fault gate
must not be attributed to this incident.

The image already installed before this follow-up remains 105,900 bytes, CRC32
`2042255645`, SHA256
`897d6ea48bd14a9b119b009c8a78e0aee3985529358af7f4209aa036042331fa`.
This follow-up performed no device operations, profile changes or synthetic
typing. A new candidate was built, but has not been flashed.

## Independent host-state tests

The model tracks each host's received pressed keys separately from the native
HID bitmap and firmware queue. It uses the actual old/current core, endpoint
adapter, shared encoder, pinned native HID/listener, and mapped-key clear logic,
with both HKRO and NKRO configurations.

| Scenario | Result |
| --- | --- |
| Normal BT1 to BT2 switch with a key held | Zero reports clear BT1 before SELECT; BT2 starts released. |
| Ordinary key-up fails; emergency zeros succeed | Both versions clear the host. One failed ordinary send alone does not reproduce the full symptom. |
| Key-down delivered; ordinary key-up and reserved zero A1 fail | Both versions can leave the host pressed while native HID is empty and the core is faulted with no pending reports. Explicit reselection restores it. This is injected failure, not historical proof. |
| Report FIFO overflows on the final key-up | Reserved zeros and the latest snapshot leave the host released. |
| Native clear precedes ordinary physical key-up | Native HID still dispatches ordinary key-up. Mapped function/media outputs deliberately cancel stale ownership; reserved zeros then own release. |
| CONNECTED arrives without SELECT ACK | Incident core blocks HID and faults after about 510 ms. The installed correction drains the wait through its 20 ms quarantine and sends reports. With pre-switch zeros delivered, this reproduces a stall, not a stuck character by itself. |
| CONNECTED arrives after the retry limit | Incident core remains faulted; installed correction recovers a completely sent SELECT. Partial UART frames intentionally lack that context. |

The host normally implements keyboard repeat: a missing release can produce
repeated text without repeated key-down packets. See the keyboard requirements
in the [USB HID specification](https://www.usb.org/sites/default/files/hid1_11.pdf).
The model checks held state, not OS repeat timing or RF delivery.

## Concrete delay found and corrected in source

`gmk_radio_clear_hid()` called native `zmk_endpoint_clear_reports()`. Its
same-translation-unit sends bypass GNU `--wrap` of the endpoint sender. Actual
installed ARM disassembly confirms native USB report dispatch after the wrapped
HID clears, even while the board routes typing to its external radio.

When native USB remains selected/configured, `usb_hid.c` can wait up to 30 ms
for its semaphore per report. Keyboard and consumer clears can therefore add
approximately 60 ms of waits under missing USB-IN completions. Disconnected and
suspended USB take other paths. This is a conditional delay, not evidence that
those waits occurred during the incident.

The installed native physical scan queue has four entries, and pinned
`physical_layouts.c` ignores failed nonblocking enqueue. Radio work shares its
system workqueue. A delayed queue can thus lose a physical release without
heap growth or memory overwrite. No historical queue overflow was measured.

The candidate changes radio-owned clears to use the existing HID-clear wrappers
and reserved A1/A2/A3/A8 release barriers directly. It avoids unrelated USB sends
for BT-to-BT and radio-to-USB clears. USB-to-radio still clears the old USB
endpoint because the radio selection changes only after clearing. Native HID,
function/media ownership and encoder-cache clears are preserved. Direct native
endpoint clears elsewhere are outside this change.

The maintained endpoint regression failed before this change and passes after
it in HKRO and NKRO. It verifies zero native sends for a radio-owned clear,
preserved keyboard/consumer release snapshots, and retained native USB clearing.
The candidate is 105,928 bytes, CRC32 `1671338335`, SHA256
`1d6102e98c74b6c123426a1c2e10f91f348f883de62958386b6f90f543a5ef96`.
All eight affected wired distributions compile. Canonical RAM remains
28,162 / 28,544 bytes; no stacks, watchdog policy or resident were changed.
Full `make test` and `scripts/check-tools.sh` pass, including strict Python
typing/lint checks. Edited C formatting and targeted GCC analysis pass. The
pointing-enabled native HID test also verifies mouse clearing and all four
reserved release formats. Independent review and eight sanitizer combinations
(incident/current core, installed/candidate endpoint, HKRO/NKRO) preserve
outgoing-host release through radio-to-USB-to-radio transitions. Existing
two-send-failure counterexamples remain; no RF-delivery guarantee is inferred.

## Remaining transport weaknesses

These are tested failure paths or inspected limits, not proof of the original
trigger. The selection correction and new clear routing do not remove them.

- **Lost CONNECTED:** after SELECT ACK, dropping its `5b 32 8d` through the
  real RX-error path leaves the core UNKNOWN. A 100-second simulated run kept
  battery queries and charger maintenance alive while rejecting HID. Another
  CONNECTED restored admission; there is no automatic status requery. This
  alone cannot explain a newly delivered key-down on that destination.
- **Partial TX deadline:** after a partly queued release and a 10 ms polling
  gap, the UART owner faults before trying now-free TX space. Later CONNECTED
  does not restore that hard-failure context. Emergency zeros remain bounded;
  repeated delivery failures can strand the host's last state.
- **Generic ACK attribution:** captured radio consumer `0x1100161c` ACKs before
  command dispatch, including A1/A2 reports. A delayed report ACK can retire
  a newer SELECT lease. The 20 ms drain for retired controls cannot identify
  arbitrary delayed ACKs from every report.
- **UART ACK is not host receipt:** captured BLE send function `0x10010294`
  can silently return when a per-host enabled/ready byte is zero. CONNECTED
  emission does not check that byte. Its exact callback meaning and ordering
  are unconfirmed; calling it specifically a CCC flag would be premature.
- **RX loss/quarantine:** UART3 is polled. Line/FIFO errors discard damaged
  RX data. A truncated raw frame retains a finite byte-discard budget that can
  consume subsequent status until exhausted. Removing that quarantine would
  allow payload bytes to masquerade as protocol; elapsed time alone is not a
  safe repair.

Ordinary preference saves are debounced and gated on radio/UART quiescence,
native inactivity and no physical keys. No active-shortcut flash-save deadlock
or indefinite native USB wait was reproduced. The new clear correction removes
a finite, unrelated USB wait, not a demonstrated deadlock.

## Memory and radio backlog

The installed main-MCU ELF has no linked `malloc`, `calloc`, `realloc`, `free`
or `k_heap_alloc` calls; its kernel heap pool is zero. Profile state, mailbox,
parser, immutable TX frame and eight-report FIFO are static. Payloads are
copied into owned buffers; no queued stack pointer or per-switch allocation was
found. Preference updates reuse one four-byte record.

ASan/UBSan passed 16,000,000 mixed operations with canaries, queue/frame bounds
and state checks, including stale ACKs, send failures and tick wrap. A separate
1,000,000 alternating paired BT1/BT2 switches with press/release snapshots also
passed. Existing radio, endpoint, native keymap, function/media clear concurrency,
controls and settings suites passed. These are offline tests, not a million
physical Bluetooth reconnections.

The 382 unallocated RAM bytes are separate from 14,336 already reserved stack
bytes. ARM frame inspection found no new allocation or unbounded recursion on
the reviewed switch path. Specific ordinary scan/settings call chains fit their
reservations, but live high-water marks remain unmeasured: stack initialization,
stack-info and sentinel options are disabled. These checks are not a universal
stack-safety proof.

The external radio has its own heap and BLE stack. Captured GATT dispatch can
retain busy reports on a saved-message list; the main-MCU eight-report FIFO
does not bound that downstream backlog. Inspected disconnect paths free active
ownership and requeue saved messages for rejection/free; no obvious missing
free was found there. Heap exhaustion under congestion remains possible, and
the main-MCU allocation result cannot rule out a radio-side problem. There was
no live radio-heap measurement or failing RF trace.

## Evidence and conclusion

Private reproducible probes, baseline reconstruction and logs are under
`.work/switch-rootcause/{memory,report,service}/`. Root verification and build
logs are in `.work/switch-rootcause/`. No pairing credentials, saved layout or
typed user data is included in this analysis.

Lost release delivery plus radio-state liveness is the primary investigation
target. The installed correction fixes demonstrated old control-state bugs;
the new unflashed candidate removes a verified USB detour during radio clear.
Neither proves that all wireless faults recover automatically, nor identifies
the exact event that started the user's incident.
