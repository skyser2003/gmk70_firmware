# LEFT original-reset update comparison — 2026-10-29

The [implementation/result](../../ZMK-WATCHDOG-ROLLBACK.md) removes the added
permanent watchdog in separately labeled LEFT firmware. The actual SYSRESETREQ
path returned automatically for A→B and B→final, each followed by more than
70 seconds of uninterrupted both-half health with zero retries/reconnects.

One joined-LEFT cold cycle was needed before the comparison: bootstrap through
the old direct handoff inherited an already-running watchdog. That failed
initial automatic recovery remains a failure in [result.json](result.json).
It is not counted as a successful no-watchdog hardware-reset test.

Final LEFT is 70,340/1796245169. RIGHT remains 36,576/1296309606; both resident
CRCs remain 1770733249. Actual Studio save/reload and restoration of all original
four-layer bindings/names passed. Normal local Studio reconnects to COM3 with
remote debugging disabled. The installed LEFT has no watchdog hang recovery;
the underlying register-retention/failing-PC mechanism remains unmeasured.

Private evidence and saved configuration are frozen under
`artifacts/recovery/2026-10-29-watchdog-rollback/operator-and-evidence/` with a
SHA256 inventory. Do not publish those snapshots or replay an INSTALL. The
reviewed source commits are `9686df0` and `610946f`; no RIGHT or resident write occurred.
