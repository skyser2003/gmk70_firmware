# GMK70 volatile radio trial

**Goal:** Continue the supplied RAM-only trial with unchanged Vial and unchanged
stock flash. Preserve existing local WinRT fixes and all private backups.

**Input:** Verified `HANDOFF.md` in `.work/handoffs/gmk70-local-20261023/`.
The handoff is research evidence. Its simulated results do not authorize
unverified assumptions about live addresses, allocation, or callback activation.

**Approach:** Reproduce the existing bootstrap with the local ARM compiler,
then use a separate Windows PowerShell transport whose sender permits only
bounded storage/memory reads and explicitly scoped RAM writes. Never send erase,
program, commit, pairing or reboot commands. No Vial installation or source edits.

- [x] Reproduce the 148-byte bootstrap hash and eleven offline emulator cases.
- [x] Test packet guards and inspect the current application, RAM CRC slot,
      live GATT slots, heap metadata and Windows HID identity without writes.
- [x] Verify a CRC-entry return-false canary, perform a user-controlled power
      cycle, and compare the reloaded RAM bytes with stock before activation.
- [x] Establish a separate activation mechanism independently; read back complete
      code/context before activating the one-shot allocator. The original
      four-byte host callback write remains prohibited.
- [x] Validate an actual owned allocation before uploading any service payload.
- [x] Investigate a separate HID service and the existing UART32 route only
      within proven allocated RAM. Keep transport, Windows enumeration, native
      Vial compatibility and physical stock restoration as separate milestones.
- [x] Verify stock flash and the Vial executable remain unchanged. Record
      blockers honestly and keep private RAM/flash data on the local computer.
- [x] Check native discovery in the exact installed desktop Vial build and record
      its compatibility gate. Continue with unchanged Web Vial as subsequently
      requested by the user.

The following entries record earlier checkpoints; the final section records the
latest physical state and supersedes their provisional stop/status statements.

Independent review confirmed that stock memcpy may tear a four-byte callback
pointer update. Source alignment is not guaranteed, and exhaustive scheduling
serialization remains unproven. Callback activation therefore stays disabled;
the physical trial only stages the four-byte CRC-entry canary. The user's
USB-mode/unplug/reconnect sequence restored all 226 inspected code bytes to stock.
Full before/after 512 KiB captures are byte-identical and match the prior private
backup (`db2924effc06cae742cb6436b79db8270f245f04ea6899fd26cec9d78e3c22a8`).
The installed Vial executable hash is unchanged. Exactly one four-byte RAM write
was sent; no callback activation or service payload was installed. The sanitized
result bundle is `artifacts/analysis/ram-trial-2026-10-23/gmk70-ram-trial-20261023-analysis.zip`,
also copied directly to Windows Downloads. Private captures stay on Windows.

An automated check rejected the additional alternative-activation review for
possible cybersecurity risk. No alternative activation mechanism was implemented.

The subsequent read-only compatibility check used the installed Vial executable's
original Python 3.6 discovery code objects, with controlled HID metadata, and its
actual bundled `hid.cp36-win_amd64.pyd` for live enumeration. Five discovery cases
passed: FF60/61 with an empty or address serial is rejected; the required Vial
serial marker is accepted; the stock FF00/00 usage is rejected even with that
marker; matching a VIA definition creates a VIA-mode device. Original installed
bytecode also confirms that VIA/sideload JSON skips native Vial identification.

The bundled library returns empty serial/manufacturer/product strings for GMK70
Bluetooth collections on Windows build 26300. Direct Windows HID string calls
return success with empty strings, while a separate HIDAPI 0.15.0 enumeration
recovers Freqchip/GMK70-2 and the Bluetooth address through its metadata fallback.
This corrects the earlier assumption that the installed Vial sees the address as
its serial: that observation came from the newer standalone library.

Microsoft documents that these HID string APIs do not expose BLE DIS strings;
the supported approach queries DIS through Bluetooth APIs. Consequently changing
the HID report map alone cannot satisfy this installed Vial's native discovery.
No supported firmware-only route through this stack was established. A modified
DIS-serial/second-HID A/B trial was not performed, so this is not an experimental
proof that every future Windows implementation or alternate host path fails.
Further activation is stopped under the unchanged-Vial constraint. This check
sent no radio commands or HID reports, modified no firmware/settings/host drivers,
and verified the executable and bundled HID library hashes remained unchanged.
The sanitized evidence bundle is
`artifacts/analysis/vial-discovery-2026-10-23/gmk70-vial-discovery-20261023-analysis.zip`,
also copied directly to Windows Downloads. Raw HID paths and extracted installed
application bytecode remain local and are excluded from the bundle.

The user subsequently requested support for both clients, or the easier client
first, and explicitly reconfirmed: **keep all Vial client code unchanged**.
Continue with official Web Vial as the first candidate; no client fork or runtime
transport replacement is authorized. Stock flash remains a preservation constraint.

Additional read-only reconnaissance on 2026-10-23 found a stock FFF0 GATT route:
the write characteristic `0783b03e-8535-b5a0-7140-a304d2495cb8` accepts
`AA 22 A1 <32-byte request>`; compiled decoder `0x10016ECA` passes its payload
through `0x10002600` to the existing UART32 helper `0x10011C6C`. The existing
reply wrapper targets characteristic `0783b03e-8535-b5a0-7140-a304d2495cba`
with `05 AF <32-byte reply>`. This is a candidate external-bridge transport,
not a proven native Vial link. The live service has the expected UUIDs/properties.

The physical diagnostic `.work/device-updates/probe-stock-vial-gatt.ps1` sent
one fixed FE00 GET_KEYBOARD_ID request. Windows accepted its GATT write, but no
notification arrived during the twelve-second capture. Earlier preflight attempts
sent no requests; the eventual notification receiver uses a CLR queue and explicit
WinRT reference assemblies. A separate metadata-only session reports MaxPduSize
128. No configurable payload, settings write, OTA command, RAM write, or flash
command was sent by these probes. A physical Bluetooth-slot-2/wake check is pending
before a new getter attempt. Do not treat an accepted write as a Vial response.

An isolated Chrome 155 profile on Windows 26H2 exposes WebHID and lists the stock
radio as `Unknown Device (0000:0000)` in an unfiltered chooser. Selecting/opening
through the research automation was not verified. A name-only GMK70 match initially
missed this generic entry; this is not evidence that the browser cannot enumerate
BLE. No HID reports or Vial client edits were made. Metadata probe 4's provisional
status must be ignored because its promise was still pending; subsequent probes
fail closed on pending/empty results. Raw chooser contents/profile files remain
private. Existing unrelated virtual HID devices were only enumerated, never altered.

Latest sanitized evidence:
`artifacts/analysis/web-vial-trial-2026-10-23/gmk70-web-vial-trial-20261023-analysis.zip`,
also copied directly to Windows Downloads. Native Web Vial, second-HID registration,
safe activation, and a complete UART32 round trip all remain unverified.

## Firmware-only continuation, 2026-10-23

The user rejected a Windows driver/helper bridge: **firmware-only**, with all
Vial client code unchanged. Official Web Vial remains the first candidate.
The RAM-only/preserve-stock scope remains binding; no persistent radio update
is authorized by this choice.

The awake read-only stock GATT probe returned protocol 6 and UID
`B691D7277FC88251`. A subsequent continuously awake capture received 45 complete
34-byte notifications: identity, definition size and 43 definition blocks.
The 1,352-byte XZ payload has SHA256
`c02ee75d7244cb5e7af10cb522312a969637dffb6e1cb4b2a97234939f048a8f`.
Its decompressed JSON exactly equals `firmware/qmk-vial/keymaps/vial/vial.json`.
An intervening idle attempt received no identity reply. This proves the stock
UART/GATT native Vial route while awake, not WebHID discovery or idle reliability.
No setters, OTA packets, RAM writes or flash commands were sent by these probes.

Ruling: Continue the already authorized RAM-only experimental spike with offline
validation of a one-byte UART branch candidate. Do not retry the rejected agent
review request, and do not infer activation safety from compilation/emulation.
The old four-byte host callback write remains prohibited. Cost if wrong: stop
before live executable changes and retain the read-only transport evidence.

Candidate under investigation: stock UART opcode AF reaches short branch
`0x11001842`, originally `92 E0` to `0x1100196A`. Changing only its high byte to
E1 would branch to `0x11001B6A`, inside the exclusive CRC validator range.
A candidate gateway must preserve all registers/SP/LR, restore that byte under
saved PRIMASK, reject allocation in Handler mode, use actual allocator preflight,
and resume the exact original continuation. Its original CRC entry must still
return false. Staging, instruction visibility, asynchronous behavior, live heap
ownership and rollback all require separate evidence before any service trial.

**Review focus:** Nonatomic memcpy callback updates; disconnect between writes;
unknown live slot ownership; stale/insufficient allocations; Windows serial
discovery incompatible with native Vial. No step may infer success from simulation.

Build and test commands remain hardware-free. Every device operation is an
explicit invocation of the separate physical tool. Physical rollback assistance
is required from the user; unplugging USB alone is not assumed to reset the radio.

## Physically verified Web Vial milestone and revision follow-up

The one-byte UART gateway was validated against the captured WRITE_MEM path at
four source alignments: each uses exactly one byte store. A live allocation-free
execution canary recorded thread mode (IPSR 0), preserved the UART continuation,
and restored the original branch. The actual ROM allocator then returned a
2,048-byte block at `0x1100C840`; its header and a fresh private ownership marker
were verified before uploading code. Thirteen offline one-shot checks cover the
actual captured allocator with synthetic heap fixtures. These offline results do
not substitute for the separately recorded physical execution and allocation.

The first 1,176-byte service payload registered a second HID service in slot 5,
ID `0x4E`, start handle 91. The stock typing service and FFF0 channel remain.
Service Changed refreshed the host's GATT list; a subsequent link disconnect and
reconnect, preserving the allocation marker, exposed the FF60/61 HID collection.
A native HID write of 33 bytes, including report ID zero, returned a 32-byte Vial
identity with protocol 6 and UID `B691D7277FC88251`.

The unchanged official `https://vial.rocks/` client then loaded the actual GMK70
keymap and native Vial tabs in a fresh dedicated Chrome profile. Permission was
granted through the normal HID chooser. No client source, installed Vial file,
host driver, keymap setter, or pairing operation was changed by the trial.
Private screenshots and browser state remain on Windows and are excluded from
analysis archives. This proves the native Web Vial readout, not configuration
setters, unattended idle recovery, or desktop-client discovery.

A complete 512 KiB radio flash capture after that Web Vial readout matches the
original private backup byte for byte, SHA256
`db2924effc06cae742cb6436b79db8270f245f04ea6899fd26cec9d78e3c22a8`.
The installed Vial executable and bundled HID library hashes also remain at
their recorded baselines. Source now lives directly under
`firmware/qmk-common/radio/ram-vial-trial/`; objects and generated linker scripts
live under `.work/radio/ram-vial-trial/normal/`. Normal QMK/Vial builds exclude
this experiment. The original source snapshot and exact live binaries are kept
as historical evidence under `.work`, not as a second maintained source tree.

Independent service review reproduced an overlap bug in the first payload:
two requests forwarded before their replies result in only one notification.
The unchanged client normally sends synchronously but can retry after a 500 ms
read timeout. The corrected source removes the one-bit pending gate and preserves
the stock response stream, including late duplicates. FFF0 and HID cannot serve
independent simultaneous configuration clients: the shared protocol has no
transaction IDs. This also does not solve client-side delayed-reply ambiguity.
The corrected 1,168-byte payload passes 22 offline checks, including overlap,
duplicates, framing, unsubscribe, wrong-connection writes and heap pressure;
review found no remaining Important/Critical issue in that service revision.
`make test` and the standalone ARM build pass. No normal QMK variant is affected.

On the attempted fresh session, the exact first payload, active route and ownership
marker survived the user's USB-mode/unplug sequence. The installer rejected the
nonstock code before any write. Read-only verification at 18:04 UTC confirmed
the previous session, so this sequence cannot be treated as reliable radio power
removal. The user's subsequent full power-off did clear the trial: complete stock
code and free slots were verified, then an actual fresh allocation/header/nonce
was obtained before the corrected payload was registered. Its Service Changed,
link reconnect and native HID reply route were verified separately. A packaging
hash typo was also rejected before device I/O and corrected against the exact
per-file hashes. No active executable payload or host callback cell was replaced.

The initial corrected-revision native probe returned no reply; firmware counters
showed zero forwarded requests and no notification subscription. Read priming,
a held BLE connection, and an attempted one-time restart of only the experimental
Windows HID instance did not resolve this. The restart required a normal Windows UAC
approval; it installed no driver/helper and is not part of a proposed solution.
Windows lists the stock HID parent at handle 24 and the second parent at handle
91, with the Vial vendor collection correctly parented to the latter. The firmware
test therefore isolated subscription restoration rather than assuming that the
second service is unsupported or that the main MCU was asleep.

Under fresh nonce, header, code/constants and slot guards, an explicit diagnostic
temporarily restored the known host's connection and notification-subscription
bytes in owned RAM. Three fixed FE00 identity getters, including a two-request
burst, produced three forwarded requests and three notifications with protocol 6
and the expected UID. The two original data bytes were then restored. No executable
byte, callback pointer, pairing key, settings setter or flash operation changed.
This is direct evidence that losing the already enabled subscription state blocks
the native transport after RAM reinstallation. It does not prove how Windows
internally caches that state, or replace a complete bonded-host implementation.

The previously observed subscription was subsequently restored in owned radio RAM
for the continued unchanged Web Vial check. This one-time initialization is not a
Windows virtual HID, driver/helper installation or runtime bridge. Automatic
notification configuration restoration for each bonded host remains firmware
work: the current trial clears its active subscription on link create/loss.
Preserve the stock flash constraint; do not publish this as a general release.

With that previously observed subscription initialized in radio RAM, the corrected
revision loaded the GMK70 keymap and native extension tabs in unchanged official
Web Vial as well. The UI permission used the normal single-device chooser; no
Vial client code, host transport, virtual HID or driver package was added. This
completes the RAM-only native Web Vial readout milestone. Configuration setters,
idle reliability, automatic bonded-host subscription restoration and installed
desktop-client discovery remain outside the demonstrated result.

The final complete 512 KiB capture finished at 18:25:59 UTC after the corrected
trial and matches the original private backup byte for byte. The installed Vial
executable and bundled HID library hashes also remain unchanged. The sanitized
source/evidence archive is
`artifacts/analysis/web-vial-ram-success-2026-10-23/gmk70-web-vial-ram-success-20261023-analysis.zip`,
copied directly to Windows Downloads. It excludes raw RAM/flash, pairing data,
ownership nonces, host instance paths, browser captures, executable payloads and
physical installation scripts. The corrected payload and its radio-side HID route
are left active in RAM for the trial; automatic reconnect restoration is not
claimed complete.
