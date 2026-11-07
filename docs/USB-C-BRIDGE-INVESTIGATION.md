# GMK70 bridge replacement with a USB 2.0 Type-C cable

Investigation date: 2026-11-01. Source revision:
`fccfead46bc22627c470fd4b3d81f3cc1900d26f` on `main`.

Status: hardware investigation completed 2026-11-02; temporary diagnostic
implementation retired 2026-11-06. This report preserves the experiment's
observations and limits at the investigated revision. Current sources no longer
contain its firmware, build target, result reader or dedicated tests. Historical
image sizes and validation results below do not describe a current build.

## Finding

**Firmware-only feasibility is undetermined on the existing PCB.** The later
bounded SRAM diagnostic failed with the user's replacement cable in all four
plug orientations; it retained the existing UART pins and did not test a new
physical signal path. The replacement's conductor set is unverified, so this
is not a test of every standards-compliant USB 2.0 data cable. A simple
UART pin-definition change is not a solution. Software UART or a bidirectional
single-wire protocol remains a conditional possibility, requiring a usable
electrical path already present on both boards and independent of LEFT's PC
USB connection. No such alternative path has been established.

If the only paths to the split MCU signals use contacts absent from a USB 2.0
cable, and its remaining contacts provide no independent usable path, hardware
changes are necessary. Conversely, missing UART alternate functions alone do
not rule out software signaling on other *physically connected* GPIOs.

The user cannot take continuity measurements now. **There are no cable or PCB
electrical measurements from this investigation.** All resistance entries below
are unknown. The owner's published cable observations are attributed reports, not measurements
of this keyboard. In the initial investigation no new simulation, transport
implementation or firmware build was performed; its timing figures below are
arithmetic estimates. Subsequent authorized diagnostic preparation is recorded
separately below.

## Scope and repository handling

`AGENTS.md` was read. The existing maintained checkout was clean before work;
`origin/main` was fetched without checking out, rebasing or replacing files.
Local `main` and fetched `origin/main` both resolved to the revision above.
`scripts/project.sh` was sourced before project shell commands. The suggested
checkout path is absent on this host; the existing maintained checkout used by
this thread was used instead.

The initial source change was documentation only. Dependency pins and
upstream sources remain unchanged. The initial investigation was offline;
the user's later request authorized the read-only runtime observation below.
That initial phase involved no flashes, resets, settings operations, driver
installations or powered GPIO/electrical experiments. Reference downloads and raw runtime
evidence stay in ignored local work storage. The user subsequently authorized
preparing and installing a temporary diagnostic application, with restoration
of the original application afterward; this supersedes the original no-flash
scope only for that diagnostic round trip.

The target includes keys, lighting, status, wake/control and application
updates across the bridge while LEFT remains connected to the PC by USB.
Merely getting key events through, switching the PC connection off, or using
the host PC to relay between two separately attached halves does not meet it.

## Evidence and its limits

### Maintained firmware

These sources describe MCU configuration and current protocol behavior. They
do **not** identify USB-C connector nets or constitute a PCB schematic.

| Source | Established source behavior | What it cannot establish |
| --- | --- | --- |
| [Shared QMK configuration](../firmware/qmk-common/keyboards/zuoya/gmk70/config.h) | Split UART1 uses MCU PA9 TX / PA10 RX, AF7, 115200 baud, full duplex. Radio UART3 uses PC10/PC11. | Which bridge contacts carry either UART signal; whether buffers or switches intervene. |
| [ZMK WB32 wired driver](../firmware/zmk/wired/wired_wb32.c) | Configures the same UART1 pins as 115200 8N1; serves peer transactions and serializes polling, lighting and update access. | Connector routing, logic voltage at the socket or electrical isolation. |
| [Board definition](../firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json) | LEFT knob uses MCU PB7/PB6. | Whether either pin also reaches a bridge contact. |
| [QMK wake handling](../firmware/qmk-common/keyboards/zuoya/gmk70/wls/wls.c) | Peer wake can pulse MCU PA9 low for 100 microseconds; split RX, MCU PA10, becomes an edge-wake input in STOP. | A separate CC/SBU wake wire, or the physical wiring of these contacts. |
| [USB power adapter](../firmware/zmk/drivers/usb/device/usb_power_gmk70.c) and [shared transport](../firmware/qmk-common/drivers/wireless/transport.c) | MCU PB1 controls the external USB D+ pull-up supply according to maintained source. MCU PA7 is used for cable detection. | A USB data mux, inter-half power switch, or an ability to electrically isolate the two LEFT sockets. Disabling a pull-up is not disconnecting the data traces. |
| [Split protocol](../firmware/zmk/wired/wired_protocol.c), [update service](../firmware/zmk/update/service.c), [QMK updater](../firmware/qmk-common/keyboards/zuoya/gmk70/update/qmk_update.c) | Matrix and RGB traffic share the byte transport; RPC18 carries 32-byte update requests/responses. Status/control also use the update message path. | That this protocol would operate on a different electrical connection without transport changes. |

The supplied facts identify native USB on MCU PA11/PA12. USB descriptors only
describe host-visible interfaces; they provide no evidence that a particular
socket is connected to those MCU pads.

### MCU and cable documentation

The [WB32FQ95xx reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf),
Table 8-3, printed pp95-96, assigns UART1 TX/RX to PA9/PA10 or PB6/PB7.
PA11/PA12 have UART1 CTS/RTS, **not TX/RX**. Section 8.3 describes GPIO modes;
that makes software signaling a peripheral-level possibility, not a routing
solution. Section 21 describes a full-speed USB **device** peripheral; it does
not establish a second USB controller or a simultaneous host/device bridge.

The [USB-IF Type-C specification, release 2.0](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019.pdf),
Tables 3-4, 3-10 and 3-11, supplies the connector terminology and cable baseline:

| Receptacle contact group | Standard function | USB 2.0 C-to-C cable baseline |
| --- | --- | --- |
| A1/A12/B1/B12 | Ground | Ground return present. |
| A4/A9/B4/B9 | VBUS | Power conductor present. |
| A6/B6 and A7/B7 | D+ and D− | One USB 2.0 pair, not four independent data wires. |
| A5/B5 | CC1/CC2 at a receptacle | One end-to-end CC path; the other plug contact can serve VCONN. Do not assume two freely usable signal wires or a CC1-to-CC2 short. |
| A8/B8 | SBU1/SBU2 | SBU conductors are absent from a standard USB 2.0 cable. |

Plug and receptacle labels differ in the CC/VCONN and duplicated-data details.
Flipping a plug changes which receptacle contacts it mates with. Breakout
labels must be interpreted using that board's connector drawing.

**MCU PA9 is a microcontroller signal. Connector A9 is a VBUS contact. They are
unrelated names.** Likewise MCU PA10/PA11/PA12 are not connector A10/A11/A12.
All tables use explicit MCU or connector prefixes where ambiguity matters.

### Owner report, schematics and photographs

The [2025-09-13 owner article](https://www.abel9999.com/2025/09/juoya-gmk70.html)
reports testing with female breakouts and lists power, ground, D+/D− and extra
A5/B5 and A8/B8 connections. Its wording appears to group contacts together.
Breakout schematics, orientation records, resistance readings and a complete
mapping are absent. In particular, it does not establish that all CC contacts,
or all SBU contacts, are shorted inside the supplied cable. Full-featured-cable
success also does not prove that the GMK70 uses USB, DisplayPort or Thunderbolt
protocols between halves. This is a useful lead, not a verified board pinout.

No PCB netlist, board layout or schematic was found in the maintained source
inventory. The inspected [published board-source tree](https://github.com/PandaKBLab/GMK70-QMK/tree/main)
contains firmware, not a connector schematic. The manufacturer's download page
`https://zuoya.top/qdxz` could not be retrieved during this investigation.

The additional [Mathew download page](https://www.mathewkb.com/china/gmk70-json/)
was checked, and the linked [original source archive](https://www.mathewkb.com/wp-content/uploads/2024/11/ZUOYA-GMK70.7z)
was downloaded and its complete file list inspected. The 17,318-byte archive
contains C sources/headers, a board JSON, make fragments and a README; it
contains no schematic, board layout, boardview, Gerber, photograph or PDF file.
The other linked [personal firmware project's README](https://github.com/uk-taniyama/qmk_zuoya_gmk70/blob/main/README.md)
describes adapting that source and basic wired operation, without supplying a
connector-to-MCU map. These leads do not fill any physical connection cell
below. No code from either external project was executed or installed.

The [original ZUOYA GMK70 leaflet](https://ae01.alicdn.com/kf/S82ca1adeebf04bdfb241731b12233d4ei.pdf),
also available as a [mirror](https://manuals.plus/m/afce334373cd4a880f56d8db405f46f6028be78bc31c28f94df390d428666f4c.pdf),
describes operation/recovery rather than connector-to-MCU routing. General
product/exploded illustrations are insufficient to trace concealed nets.

The photograph in the owner's [separate recovery article](https://www.abel9999.com/2025/09/juoya-gmk70_13.html)
was downloaded and visually inspected. It shows a switch and the spacebar
mounting-hole area through the plate. It does not expose the bridge solder
contacts, MCU pads, battery connectors or a traceable connection between them.
The highlighted recovery locations are **not identified UART/USB test points**.
No accessible board probing points can responsibly be assigned from this photo.
Other bounded photograph/teardown searches did not yield a usable connector/MCU
close-up; a Reddit image fetch failed. No routing conclusion relies on it.

## Physical connection record

**Measurement state: NOT TAKEN.** The following is the connection worksheet,
not a fabricated or inferred pinout. `Unknown` means no physical result has
been supplied. A cable map alone cannot fill in the board destinations.

| Item to establish | Physical result / resistance | Present evidence |
| --- | --- | --- |
| Supplied cable ground, end X to end Y | Unknown | Owner reports ground continuity. |
| Supplied cable VBUS, X to Y | Unknown | Owner reports power continuity. |
| Supplied cable D+ and D− mappings | Unknown | Owner reports these contacts; orientations unknown. |
| Supplied cable CC1/CC2 cross-end and same-end mapping | Unknown | Owner grouping is ambiguous. No short is assumed. |
| Supplied cable SBU1/SBU2 cross-end and same-end mapping | Unknown | Owner grouping is ambiguous. No short is assumed. |
| Supplied cable cross-family CC-to-SBU mapping | Unknown | Not excluded by a trustworthy complete map. |
| LEFT bridge D+ to MCU signal/components | Unknown | Firmware does not specify the connector net. |
| LEFT bridge D− to MCU signal/components | Unknown | Same limitation. |
| RIGHT bridge D+ to MCU signal/components | Unknown | Same limitation; direct-PC recovery does not identify this net. |
| RIGHT bridge D− to MCU signal/components | Unknown | Same limitation. |
| LEFT bridge D+/D− to LEFT PC-port D+/D−, including cross-polarity | Unknown | Shared traces, separate nets and switched paths are not distinguished. |
| CC/SBU to MCU PA9/PA10, other GPIOs or control circuitry, each half | Unknown | No verified board schematic or measurements. |
| Bridge VBUS/ground distribution, battery/charger isolation | Unknown | No power-path schematic. |
| CC/SBU role before application startup: power, reset, enable or detection | Unknown | Neither firmware names nor the cable report establish it. |

## Later read-only observation of the running keyboard

On 2026-11-01, the user additionally requested a test with the running device
that would avoid permanent damage. The existing Windows HID runtime was used
from WSL, without installing a driver or changing the USB attachment. Only
INFO and namespace `0x72` health requests were issued through the maintained
[health reader](../tools/firmware-update/read_health.py). No experimental
transport or GPIO code was loaded.

Both halves supplied 70 health samples each. The minimum verified continuity
window was **70.448 seconds**, with zero transport retries, relay retries or
reconnects, and no detected uptime discontinuity/reset. Both reported the
permanent no-watchdog policy and zero feed calls. Before/after identities
matched exactly:

| Reported identity | LEFT | RIGHT |
| --- | ---: | ---: |
| Application bytes | 83,804 | 83,804 |
| Application CRC32 | 3349640223 | 3349640223 |
| Resident CRC32 | 1770733249 | 1770733249 |
| Updater state | Idle | Idle |

These are device-reported identities, not a new SHA256 readback. No settings
were read or written, and the unavailable matrix-event field supplies no typing
evidence. The newly committed firmware was not installed.

This establishes native USB access and working bidirectional split RPCs over
the **presently connected bridge**, whose cable construction was not independently
identified. It does not identify any connector contact or validate a USB 2.0
replacement. Digital telemetry is not a cable/PCB continuity measurement, so
the physical connection table remains unchanged.

Read-only diagnostics avoid changing pin drive, flash or boot state. They do
not establish that an experimental powered pin-reassignment test would be safe.
Unknown shared nets can create output contention or power-path problems;
temporary RAM execution, firmware rollback or a watchdog would not prevent
electrical damage. No such experiment was attempted or declared safe.

### Requested temporary-RAM transport test: loader availability

The user subsequently clarified that the desired test is an actual temporary
firmware experiment using a USB 2.0-only connection, rather than another
current-link health check. The current application has no supported host
interface for uploading and executing a program from main-MCU SRAM.

The existing [ZMK updater configuration](../firmware/zmk/update/Kconfig)
explicitly limits trial support to forwarding packets to a still-Vial RIGHT;
it provides no local trial execution or watchdog handoff. The older
[trial package format](../tools/firmware-update/trial.py) and
[trial implementation](../firmware/qmk-common/keyboards/zuoya/gmk70/update/trial.c)
use staging **flash at `0x08020000`**, not a RAM-only payload. That separate
Vial launcher preserves the main application but is absent from current ZMK.
The repository's volatile [radio RAM experiment](../firmware/qmk-common/radio/ram-vial-trial/README.md)
runs on the separate radio processor and cannot replace WB32 split transport.

Read-only trial ABI2 CAPS (`0x13`) requests to the running LEFT and RIGHT both
returned **UNAVAILABLE (7)**, with the expected roles and idle state. No BEGIN,
DATA, SEAL, RUN, INSTALL, watchdog probe or reset was sent. Fresh INFO and health
queries afterward confirmed both halves responded with the same application
and resident identities. Raw capability evidence remains local.

This establishes that the documented trial facility is unavailable on the
current installation; it is not a USB 2.0 transport test. Enabling a new loader
would require a separately reviewed firmware change and installation, outside
the requested RAM-only operation. Its recovery behavior and allowed GPIO
access would need qualification before a cable experiment. No experimental
transport firmware was loaded or executed.

A valid later experiment must also physically use a known USB 2.0-only cable.
Software restrictions while the supplied cable remains connected cannot prove
that extra contacts are unnecessary for hardware power/startup paths. A failed
trial of one proposed transport would not prove that every firmware-only
transport is impossible; success would require complete-traffic and PC-USB
coexistence checks, not merely an echoed test byte.

## Subsequent authorized diagnostic preparation

After the read-only capability result, the user authorized a temporary
diagnostic application and restoration of the exact original firmware.
The separate `wired-studio-cable-probe` application used product name
`GMK70 USB2 RAM Probe`; it was never a normal release asset.
The fixed 180-byte Thumb routine executes from worker-stack SRAM, using only
the existing UART1 data/status registers and the unchanged pin configuration.
It tests each receive direction independently for a bounded startup interval;
it does not implement signaling on a newly selected physical path.

The startup session allowed 250 ms to obtain UART ownership and ran for up to
1,500 steps or 1.5 seconds before restoring normal service. Eight role-tagged
peer beacons marked a received direction. A local green/yellow result indication
was intended to last 30 seconds. Read-only namespace `0x72`, ABI1, opcode `0x23`
returned the immutable RAM-execution, completion, peer-detection and error
counters. No arbitrary code-upload interface was added.

At the investigated revision, both diagnostic and ordinary Studio builds passed
ARM image checks, with the diagnostic allocating 28,240 of 28,544 RAM bytes.
Native transport/session, receiver and LED tests, repository tests, formatting,
lint and strict type checks passed. These are build/native simulation results, not physical cable
measurements. The diagnostic package is 86,720 bytes, CRC32 `510541188`, SHA256
`1ddc22e7167daa88928b09f0f787bff9e7a016d825330f1d41b878ba9b5c7c13`.
The original installed application and private native four-layer Studio backup
are frozen locally for restoration; neither resident will be rewritten.

The user has a random cable that previously failed as the bridge, rather than
a measured or independently specified USB 2.0-only cable. Success would prove
behavior with that cable, not its conductor set. The available physical actions
are cable swaps and mode changes. No board probing or case opening is planned;
battery isolation is still required before any future board resistance checks.
Both halves now run the diagnostic application, with INFO size/CRC and resident
CRC checked independently. Each target received one INSTALL request. LEFT
acknowledged but reached the USB-return timeout; the user performed the
documented joined-LEFT USB reconnect. RIGHT's INSTALL acknowledgement failed
validation, so it was not replayed; independent INFO established that RIGHT
had installed the expected image. A first post-RIGHT health window failed an
uptime/discontinuity check without a per-exchange trace; its cause is unknown.
A fresh traced 70-second both-half qualification passed with exact identities,
zero transport/relay retries, zero reconnects and zero watchdog feeds.

Native Studio readback after each half matches the original private backup:
four saved layers, 296 bindings, names/order preserved and no unsaved changes.
Both halves report execution of the 180-byte RAM routine, with zero frame
errors. Their initial separate startup windows received no peer beacons; these
asynchronous installation results are not cable-failure evidence. The user
confirmed typing/RGB in the mixed LEFT-diagnostic/RIGHT-original state on the
working USB 3 cable. Both halves ran the temporary diagnostic at this stage;
the subsequent restoration is recorded below.

The user then performed the shared restart with the working USB 3 cable and
observed green outer LEDs on both halves, with key LEDs temporarily dark.
Stored results confirm RAM execution and peer detection in both directions:

| Control result | LEFT | RIGHT |
| --- | ---: | ---: |
| Result flags | 7 | 7 |
| RAM program bytes | 180 | 180 |
| Transmitted bytes | 1,311 | 1,363 |
| Valid received beacons | 262 | 318 |
| Error/rejected-frame count | 42 | 0 |

The LEFT error count's cause is not isolated; this startup probe establishes
direction detection, not an error-free sustained transport or an electrical
pinout. Fresh INFO still reports the exact diagnostic application and pinned
resident on both halves. The user confirmed normal RGB and typing returned on
both halves after the indication. A separate normal-transport control health
window then passed at least 70 seconds on both halves with zero retries,
reconnects or watchdog feeds.

### Replacement cable, first observed orientation (2026-11-02)

The user disconnected LEFT's PC USB, substituted the previously failing cable
and reconnected PC USB with both halves in wired mode. They reported LEFT's
outer LED yellow, RIGHT dark, and typing working only on LEFT. No electrical
probes, continuity tests or GPIO remapping were performed.

LEFT's read-only result reports RAM execution/completion (flags 3), 180 code
bytes, 1,296 transmitted bytes, zero valid peer beacons and 1,291 combined UART
error/rejected-frame indications. Its application remains 86,720/510541188,
resident CRC 1770733249, with zero watchdog feeds. A bounded read-only RIGHT
query failed with peer unavailable; no write or reset followed that failure.

This establishes failure of the existing UART link in this cable orientation.
RIGHT's dark LED and unreachable software result do not distinguish missing
power, failure to start, an unavailable direction, or another hardware state.
The error count is not a voltage measurement or a connector-to-MCU pinout.
The user also reversed RIGHT alone, then LEFT alone, reporting the same
LEFT-yellow/RIGHT-dark, LEFT-only operation in each case. Relative orientations
use 0 for the initial insertion and 1 for a 180-degree reversal, retaining the
same cable end at each half:

| LEFT / RIGHT orientation | LEFT TX bytes | LEFT peer beacons | LEFT errors | User observation |
| --- | ---: | ---: | ---: | --- |
| 0 / 0 | 1,296 | 0 | 1,291 | LEFT yellow, RIGHT dark; LEFT types only |
| 0 / 1 | 1,296 | 0 | 1,295 | Same |
| 1 / 1 | 1,292 | 0 | 1,287 | Same |
| 1 / 0 | 1,296 | 0 | 1,273 | Same |

All completed LEFT queries report RAM execution/completion and the expected
180-byte code length. The user reported the same failure in the final
orientation as well. No complete-traffic test or update transfer was possible
over the replacement cable. This is a negative result for the existing UART
configuration with this particular cable, not a test of software UART on
PA11/PA12, a single-wire transport or an alternative PCB route. RIGHT's startup
and power state remain unknown. All PCB-routing resistance table entries remain
unknown. No additional electrical signaling experiment is planned.

The user restored the working USB 3 cable and reported both green indications
and both halves working. Read-only results again found valid peer beacons on
both halves (LEFT 263, errors 42; RIGHT 317, errors 0). Studio still matched
the original private map. A fresh 70-second both-half qualification passed,
then one RIGHT original-image INSTALL was acknowledged and its exact identity
and another 70 seconds of both-half health passed with zero retries/feeds.
Studio readback after RIGHT restoration again matched. A fresh both-half
70-second baseline then passed before one LEFT restoration INSTALL, which was
acknowledged. LEFT reached the documented USB-return timeout; the user
performed the joined-LEFT USB reconnect. No INSTALL was replayed.

### Final restored state (2026-11-02)

Both halves independently report the exact original application, idle updater
and unchanged resident:

| Final INFO field | LEFT | RIGHT |
| --- | ---: | ---: |
| Application bytes | 83,804 | 83,804 |
| Application CRC32 | 3349640223 | 3349640223 |
| Resident CRC32 | 1770733249 | 1770733249 |
| Physical role | 3 | 0 |
| Updater state | 0 (idle) | 0 (idle) |

The frozen original package's independently reverified SHA256 is
`096c1f187299843d90debaa4217c10ab2dc4f8f29755446584c925c7d34f3fb8`.
INFO exposes size/CRC, not a device-side SHA256 query; these distinct checks
must not be represented as a SHA256 readback from either MCU.

The final both-half qualification contains 140 samples, 70 per half, with a
minimum verified continuity window of **70.881 seconds**. Exact identities
were checked before and after; transport retries, relay retries, reconnects,
detected resets and watchdog feed calls were all zero in this final window.
This fresh pass does not explain the earlier post-diagnostic timing failure.

Final native Studio readback equals the original backup: all four layers,
296 bindings, layer names/order and capacity fields are unchanged, with no
unsaved changes. Application updates excluded the native settings regions and
both residents; no settings reset or resident write was requested. The user
confirmed first keypresses/typing on both halves, RGB and the
saved lighting binding, knob rotation, and RGB-off battery/charging actions
including release back to dark. Raw snapshots and device evidence remain
private in ignored local storage.

There were four single-target application INSTALL requests total: diagnostic
LEFT, diagnostic RIGHT, original RIGHT, original LEFT. Both LEFT operations
needed the documented manual USB fallback. Diagnostic RIGHT had an invalid
acknowledgement but passed independent image/health verification; both original
restoration INSTALLs were acknowledged and independently verified. No INSTALL
was replayed, resident rewritten, settings reset or driver installed.

For that experiment, both real ARM builds and `make test`, `make test-zmk`,
`make check-tools`, formatting, lint and strict type checks passed. Dependency
pins, upstream sources and the original VIA definition remained unchanged. The
ordinary Studio build excluded the diagnostic payload/endpoint and was not
installed.

## Diagnostic retirement (2026-11-06)

After pulling main through `fbcaa1d`, review found that the temporary diagnostic
no longer linked: its 104 KiB application region overflowed by 808 bytes.
Targeted native checks also reproduced a split-command path bypassing UART
ownership and an expired result indicator remaining lit when normal RGB was
off. The earlier test suites did not cover those integration cases.

The user requested cleanup of the completed experiment. Its source, build and
packaging hooks, host reader, dedicated tests and superseded procedure/spec were
removed. Existing health handling and canonical ZMK sources were restored to
the pulled main revision. A recoverable source snapshot and review evidence
were retained in ignored local cleanup storage, along with the earlier
autostash. Private device evidence and restoration packages were preserved.
Cleanup did not operate the keyboard or change its firmware/settings. This
report retains the observed results and the unresolved electrical questions.

## Passive procedure for a later session

Only the first small batch should be requested when measurements resume.
Later batches are a staged worksheet, not a request to perform everything now.
The cable remains loose throughout: **neither end connects to a keyboard, PC,
charger or powered tester**. Internal keyboard batteries do not affect this
cable-only test because both keyboard halves are completely disconnected.

### Preparation

1. Prefer two labeled passive USB-C receptacle breakouts exposing individual
   contacts, especially CC1/CC2 and SBU1/SBU2. Check their drawings. Some tie
   data contacts together, add CC resistors or omit SBU; record those limits.
   Do not use a powered PD trigger, hub or charger as a breakout.
2. Inspect/check each breakout **without the cable** for pre-existing ties and
   resistors. A tie introduced by the fixture must not be attributed to the
   cable. If the fixture cannot distinguish a contact, mark it unresolved.
3. Label the supplied cable plugs X and Y, not LEFT/RIGHT. Mark one face of each
   plug and a reference face on each breakout. Define orientation 0 as those
   faces aligned, and 1 as the plug rotated 180 degrees. Start at X0/Y0.
4. Short the meter leads and record lead resistance. Measure resistance in
   ohms, recording `OL`, settling/drift and the meter's continuity-beep result.
   A beep alone does not prove a direct short. For asymmetric readings, record
   both probe polarities; resistance can include fixture/e-marker circuitry.

### Batch 1: power and USB 2.0 pair, four readings

Connect the loose supplied cable to the two unpowered breakouts at X0/Y0.
Use grouped D+/D− pads only if their internal connections are documented.

| Measurement | Orientation | Ohms / OL | Beep | Fixture/settling notes |
| --- | --- | --- | --- | --- |
| X.GND to Y.GND | X0/Y0 | Unknown | Unknown | |
| X.VBUS to Y.VBUS | X0/Y0 | Unknown | Unknown | |
| X.D+ to Y.D+ | X0/Y0 | Unknown | Unknown | |
| X.D− to Y.D− | X0/Y0 | Unknown | Unknown | |

Stop and review these results before progressing. Unexpected data opens lead
to a small cross-polarity/contact check, not an assumption about UART wiring.

### Subsequent cable batches, only after review

Each line below is a separate batch of four readings using the same record
columns as Batch 1:

| Batch | Four pairs to measure |
| --- | --- |
| 2: CC mapping | X.CC1→Y.CC1; X.CC1→Y.CC2; X.CC2→Y.CC1; X.CC2→Y.CC2. |
| 3: SBU mapping | X.SBU1→Y.SBU1; X.SBU1→Y.SBU2; X.SBU2→Y.SBU1; X.SBU2→Y.SBU2. |
| 4: same-end ties | X.CC1→X.CC2; Y.CC1→Y.CC2; X.SBU1→X.SBU2; Y.SBU1→Y.SBU2. Compare to empty-fixture readings. |
| 5a: cross-family mapping | X.CC1→Y.SBU1; X.CC1→Y.SBU2; X.CC2→Y.SBU1; X.CC2→Y.SBU2. |
| 5b: reverse cross-family | X.SBU1→Y.CC1; X.SBU1→Y.CC2; X.SBU2→Y.CC1; X.SBU2→Y.CC2. |

Eventually repeat the relevant maps at X0/Y1, X1/Y0 and X1/Y1; keep breakout
labels fixed and record the actual orientation for every result. Check
signal-to-power/ground or other unexpected ties in further small batches if
the results require it. If individual power/ground contacts are exposed,
record which were actually tested rather than claiming every contact from one
grouped-pad measurement. A proposed USB 2.0 replacement can later be mapped by
the same loose-cable method, without connecting it to the keyboard.

### Board measurements: photograph gate first

**No board probing is requested now.** Board measurements will be necessary
unless a reliable schematic resolves the unknown destinations and power path.
Before choosing any points, obtain and review readable photographs of both
sides of each actual PCB revision: connector solder regions, nearby components,
MCU marking/pin-1 orientation, labeled test pads and battery connections.
Identify accessible pads or component ends on those images, rather than
guessing MCU package pin numbers or asking for probes inside a USB-C socket.

Before any later board resistance/continuity measurement, disconnect every
external USB cable, charger and the inter-half bridge. **Isolate every internal
battery connection on both halves. The power switch alone is insufficient.**
Do not cut battery wires or probe cell terminals. If battery isolation or safe
point identification is uncertain, stop. After isolation, check for residual
voltage only at photo-identified board rails before selecting resistance mode;
no resistance/continuity readings are permitted on an energized board.

The first photo-identified board batch should resolve whether LEFT's two data
pairs share nets; later batches trace each bridge signal to accessible MCU-side
components and identify extra-contact power/control circuitry. Series resistors,
ESD devices, diodes and unpowered switches can obscure continuity. An open
reading is not proof of isolation in every operating state, nor can passive
measurements alone establish an active switch's reset-state behavior. Those
cases remain unknown until component documentation or a schematic resolves
them; powered experiments are outside this investigation.

## Transport alternatives against the unresolved routing

| Alternative | Electrical prerequisites | Assessment |
| --- | --- | --- |
| Existing hardware UART over D+/D− | Those contacts must already reach suitable TX/RX nets on both halves with correct end-to-end direction, compatible circuitry and no PC-bus sharing. | Conditional on routing; no evidence establishes these prerequisites. Changing a macro cannot create them. |
| Move UART1 to MCU PA11/PA12 | UART1 TX/RX alternate functions would have to exist there. | Ruled out by the pin-multiplexing table. CTS/RTS do not substitute for serial data pins. |
| Move UART1 to MCU PB6/PB7 | These pads must already reach the required cable conductors; their other board connections must permit reuse. | LEFT knob already occupies them. A firmware reassignment does not reroute their PCB traces or preserve the knob automatically. |
| Software UART on isolated bridge data GPIOs | Both ends need actual GPIO access, suitable voltage/loading, receiver timing and an independent LEFT PC USB path. | Plausible in principle, unproven on this PCB. PA11/PA12 cannot simultaneously serve LEFT's native PC USB and unrelated UART waveforms. |
| Single-wire, scheduled bidirectional transport | At least one retained conductor must connect to suitable bidirectional MCU-accessible circuitry at both ends, with an idle level and safe drive/release behavior. | Conditional; it can reduce required signal wires but cannot compensate for an absent physical path. No hardware single-wire UART mode is assumed. |
| Single-wire on the surviving CC path | Additionally requires usable CC routing in every intended orientation, no incompatible termination/e-marker interaction, and no indispensable CC power/startup role. | Not a generic spare-wire solution. A standard cable's CC presence alone establishes none of these conditions. |
| Native USB between halves | An independent LEFT-side host path and peer device path, plus appropriate PCB power/data routing, would be required while LEFT remains a PC device. | No second independent host-capable USB path is established. Two device controllers or new descriptors alone do not provide it. |

If LEFT bridge D+/D− are permanently connected to LEFT PC-port D+/D−, arbitrary
UART/software-UART drive on those nets would interfere with the PC bus.
Reconfiguring the MCU or disabling its USB pull-up does not break that copper
connection. This rules out using *that shared pair* as an independent serial
channel while keeping PC USB active. It does not independently rule out another
usable retained conductor; that is why CC routing remains a separate question.

If an SBU or other absent contact is indispensable to power, reset or enable
the receiving MCU **before it can execute firmware**, application code cannot
repair the missing startup path. A contact used only as a configurable,
post-startup signal is a different case. Its actual role is currently unknown.

### Complete-traffic requirements and timing

An alternative transport must preserve ordered matrix/key releases, lighting,
status/control, disconnect recovery and bounded application-update RPCs. Wake
also needs an edge that reaches a usable wake input while the relevant receiver
is asleep; moving packet bytes alone does not preserve the existing PA10 wake
behavior. A single-wire design needs explicit direction turnaround, contention
avoidance, acknowledgements, timeout recovery and a way for a sleeping peer to
request service. It must not depend on driving VBUS or ground as signal wires.

At 115200 baud with 8N1, one bit is approximately 8.68 microseconds and one byte
86.8 microseconds. The ideal ceiling is 11,520 payload bytes/s per direction
before protocol overhead. A 32-byte request plus 32-byte response occupies at
least 5.56 ms when serialized, excluding IDs, ACKs, processing and turnaround.
This arithmetic does not prove that software reception can meet USB interrupt
deadlines or that the existing 20-ms wire transaction deadline would suffice.
Single-wire bandwidth is shared. Firmware-update transfer and recovery need
the same scrutiny as ordinary typing.

No timing simulation or replacement-transport electrical qualification was run.
Existing native tests exercise current firmware with modeled I/O; they cannot prove connector
routing, signal integrity, startup power or a proposed replacement transport.

## Conclusion and next evidence

There is presently **no demonstrated firmware-only way** to replace the supplied
bridge with an ordinary USB 2.0 C-to-C cable while meeting the complete target.
There is also insufficient physical evidence for a universal impossibility
claim. The missing facts are the four bridge-data destinations, LEFT PC-bus
sharing, CC/SBU destinations and their power/startup roles.

The next evidence is the four-reading loose-cable Batch 1, whenever convenient,
followed by the staged CC/SBU map. After that, a verified schematic or
photo-identified, battery-isolated board checks must resolve the PCB routing.
Until then, all corresponding cells remain unknown. The completed diagnostic
did not identify a verified alternative physical path on which to base a
firmware-only replacement. Both halves have been restored to their exact
original application with preserved configuration and verified operation.
