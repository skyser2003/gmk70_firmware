# Permanent ZMK watchdog removal — 2026-10-29

[Implementation and current checkpoint](../../ZMK-PERMANENT-WATCHDOG-REMOVAL.md)
removes the long watchdog from canonical permanent builds and both installed
halves. The short temporary staging-trial watchdog remains separate.

LEFT 70196/986599886 returned automatically. RIGHT 38160/1887741245 required one
user-confirmed joined-LEFT cold cycle after its old watchdog-enabled update failed
automatic verification. Both then passed 70.125 seconds with bit 4/zero feeds,
zero retries and no observation reconnects. Both residents remain 1770733249.
The failed old-to-new RIGHT restart stays a failure in [result.json](result.json).
No right-only USB/pad recovery or repeated INSTALL occurred.

A 256-byte RIGHT staging write followed by ABORT verified its new no-watchdog
write admission without another installation. Actual Studio save/reload and
complete restoration of all four-layer bindings/names passed. Normal Studio
connects to COM3 with remote debugging disabled. No future RIGHT no-watchdog
INSTALL or fresh human typing/visual lighting test is claimed.

Private evidence and saved configuration are frozen under
`artifacts/recovery/2026-10-29-zmk-permanent-watchdog-removal/` with a SHA256 inventory.
These are provenance/recovery material, not executable resume instructions.
