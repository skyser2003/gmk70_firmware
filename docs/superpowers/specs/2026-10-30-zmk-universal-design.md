# Universal GMK70 ZMK Studio image

The user requests the same application binary on both halves, as QMK uses.
Continue inline in the existing development checkout under the established
autonomous execution preference. Preserve both residents, the saved LEFT layout,
permanent watchdog removal, all thread stacks and current features. Hardware
operations remain separate from builds/tests and use joined LEFT USB only.

## Approach

The existing `gmk70_wired` scanner, wiring tables, serial responder and RGB
driver already select their role through PB9. Battery/radio hardware ownership
and flip are already gated to LEFT. Extend this architecture to Studio storage
and package admission rather than maintaining duplicate board/application trees
or concatenating two distinct images into a distribution bundle. This is the
implementation choice for the requested single binary.

RIGHT must never mount, read, format or write Studio's settings banks. Its
backend registers with readiness false and rejects persistence immediately;
upstream behavior IDs may be assigned in RAM during the ordinary settings commit.
No job or storage claim may be created on RIGHT. LEFT retains the current mount,
failure lock, runtime USB gate and startup behavior-ID save path unchanged.
Studio's secured editing operations stay locked on RIGHT. The shared USB/CDC
descriptors and idle RPC machinery remain compiled into both roles; this does
not make RIGHT a replacement Studio/radio central. Existing matrix/lighting and
relay update service must continue when the peripheral has no settings storage.

## Packaging and update contract

Keep legacy `zmk-studio` packages LEFT-only. A new explicit
`zmk-studio-universal` profile admits both targets and retains the same
application link address, 104 KiB application bound, reserved settings banks,
native footer and exact pinned resident. Only a validated ELF report proving
runtime role-aware backend initialization can produce that profile. Canonical
Studio variants produce one universal application; keep the smaller peripheral
build available as a compatibility/recovery option.

Generalize the frozen qualified installer to one selected target per case,
defaulting to LEFT for legacy cases. RIGHT requires the new universal profile.
Every case freezes its target, package and helpers, qualifies both before
identities/residents/uptime for 70 seconds, seals only that target, rechecks both
roles/residents and the selected sealed session, and sends INSTALL exactly once.
After the durable request marker, no replay or ABORT is permitted. Lost replies
allow STATUS polling and independent read-only observation only. Independent
post-install observation must verify the changed target, unchanged other half,
both residents and another 70 seconds of health.

The ordinary updater must preserve its package gates. Its install dispatch must
also send INSTALL once and use STATUS for deferred/lost acknowledgments; a lost
acknowledgment must not trigger an ABORT after dispatch. Share that dispatch
logic with the qualified installer rather than copying it.

## Validation and delivery

Use native regressions for RIGHT startup with inaccessible settings memory,
immediate rejected saves/no claim/no write and locked Studio, plus existing LEFT
settings tests. Require actual linked role checks in image validation. Python
tests cover legacy rejection, universal bounds/resident/target checks, offline
packaging, RIGHT and LEFT frozen cases, exact sealed target, no-op/wrong-target
rejection, ACK loss, no replay/ABORT and unchanged peer verification.

Run formatting, Ruff, strict mypy, `make test test-zmk check-tools` and affected
ARM builds without dependency edits or downloads. Obtain one fresh final review
before device writes, fix important findings with failing then passing tests.
Sign the source checkpoint and inspect a fresh clean saved map before installation.
Install/verify LEFT first with RIGHT unchanged; only after that passes install the
identical frozen application on RIGHT through LEFT USB. Verify equal running
size/CRC, both resident identities, full saved-map preservation and normal Studio.
Do not erase settings, change unrelated bindings, synthesize arbitrary keys or
claim physical typing/RGB without observation. Keep private evidence out of Git;
publish sanitized results, sign and push the existing branch. If a device fails
to return, never replay INSTALL; independently inspect and report the limit.
