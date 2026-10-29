# Permanent ZMK watchdog removal

The user explicitly requests removing the permanent watchdog and accepts USB
power cycling for recovery. The installed LEFT already has no-watchdog firmware;
RIGHT still has the old watchdog. Preserve the short temporary staging-trial
watchdog, residents, radio, Studio settings and existing key bindings.

Use the existing repository/development branch and execute inline. This plan
also specifies the design: delete the permanent watchdog adapter and its service
calls, use service readiness for write admission, report intentional no-watchdog
health with zero legacy feed counts on either half, and retain legacy health
readback for the transitional old RIGHT. Permanent builds always use SYSRESETREQ;
retire the outgoing-handoff workaround and experimental watchdog toggle. Old
make targets become explicit aliases of canonical no-watchdog builds. Keep the
incoming native-startup capability footer for migration from older senders.

Device work remains separate. Snapshot actual Studio map and identities first.
Install canonical Studio on LEFT with local-only RGB, qualify both halves, then
remove the watchdog from RIGHT using a peripheral build that preserves its
no-lighting behavior and updates through LEFT USB. Stop after any unverified
activation; a physically confirmed cold start may be needed to clear a previously
armed watchdog. Never replay INSTALL, change resident, or request pad recovery.

- [x] Write failing tests for ordinary zero-feed/no-watchdog service and both-role health.
- [x] Remove permanent adapter/handoff activation, replace readiness gates and update fixtures.
- [x] Update canonical builds, retired aliases, linked-image checker and host health validation.
- [x] Run repository/ZMK/updater suites, formatting and strict checks; build affected variants.
- [x] Independent code/artifact/operator review before device writes.
- [x] Freeze source, packages, saved map and separate one-shot device operator.
- [ ] Install/qualify LEFT, then RIGHT through LEFT USB; preserve residents and configuration.
- [ ] Verify Studio save/reload and original map, archive evidence and commit/push results.

Recovery limit: USB reconnect is a tested LEFT cold-start fallback, not a general
firmware rollback guarantee. If the peer remains battery-powered, a LEFT USB
cycle may not cold-start RIGHT; do not claim otherwise or retry writes blindly.

Offline results: all repository/native ZMK/updater suites pass; canonical Studio, peripheral, normal and qualification builds pass with no watchdog/handoff symbols. Independent code/artifact review passed. Paired prior cold-cycle uptimes prove the tested joined-LEFT USB cycle reset both halves (LEFT~21106ms, RIGHT~21114ms after priorRIGHT6124726ms). Operator review pending; no INSTALL sent.

Independent operator review passed; fresh70.172-second baseline has zero retries/reconnects. No device INSTALL has yet been sent. All canonical candidates and the actual clean four-layer map are frozen privately.
