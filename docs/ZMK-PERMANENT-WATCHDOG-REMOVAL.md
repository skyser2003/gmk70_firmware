# Permanent ZMK watchdog policy

Permanent GMK70 ZMK firmware never initializes or feeds a hardware watchdog.
This applies to every canonical build. The short watchdog in the separate
temporary Vial staging launcher remains confined to that launcher.

## Firmware and build contract

Update activation uses ordinary SYSRESETREQ. The native-startup capability
footer remains for migration from compatible older senders; permanent images
have no outgoing resident-handoff path. Linked-image validation rejects
watchdog and handoff symbols.

Update admission uses service readiness and the existing wired, USB, local/peer
role and storage gates. Studio uses the worker-owned settings journal.
Health ABI 1 retains a zero feed-count word, clears the watchdog-ready bit,
and sets bit 4 to identify intentional no-watchdog operation on either half.
Host tools can read legacy watchdog health during migrations.

Legacy handoff/watchdog-rollback target names resolve to canonical builds.
They do not enable a watchdog or select a separate reset experiment.

## Recovery boundary

A hung permanent application has no watchdog reset or old-image rollback slot.
Physical power cycling is the recovery fallback; it cannot guarantee recovery
from a faulty image. Unplug USB and turn both joined halves off before
reconnecting USB to LEFT when a cold restart is required. Battery and power
topology can prevent a USB-only disconnect from cold-starting both halves.

An acknowledged INSTALL does not establish that the new image returned.
Inspect identities and health separately, preserve resident/settings data,
and never resend INSTALL after a restart-verification timeout.
See the [updater workflow](ZMK-PERMANENT-WIRED.md).
