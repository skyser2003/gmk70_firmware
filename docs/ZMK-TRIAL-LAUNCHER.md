# Temporary staging trial launcher

The separately labelled Vial `trial-updater` and `unlocked-trial-updater`
builds receive and execute bounded staging trials without replacing the
resident or ordinary application. Standard Vial updater builds exclude the
trial backend. The unlocked profile uses upstream `VIAL_INSECURE`; secure
profiles retain native physical unlock.

## Package and protocol boundaries

Trial packages use `trial.json`/`trial.bin`, format
`gmk70-staging-trial-v2`, namespace `0x71` and header ABI 2.
Ordinary installation retains namespace `0x70`, ABI 1.
INSTALL is invalid in the trial namespace. Trial images link directly at
`0x08020000`, are 1,024..122,880 bytes long and are multiples of four.
The RAM start is `0x20000040`; maximum initial stack is `0x20006fc0`.
Core vectors must point into the staged image in Thumb state.

BEGIN requires the entire ordinary commit page to be erased. It never erases
or invalidates a commit. DATA writes staging pages only. Ordinary and trial
transfers are mutually exclusive until ABORT or the ten-second expiry.
Native unlock, LEFT direct USB, wired mode and inherited peer authorization
gate mutations. EEPROM/log queries remain excluded from Vial.

The launcher reserves 64 NOLOAD bytes at `0x20000000` for its role-bound,
checksummed intent/result. Trial allocations exclude both retained RAM regions
and resident boot magic. An intentional reset enters a pre-CRT trampoline;
the intent is consumed before execution, preventing a reboot loop.
A short independent watchdog bounds temporary execution. Trials never feed it.
Permanent ZMK has no watchdog initialization or feed path.

See [execution and recovery](ZMK-TRIAL-EXECUTION.md) for operation numbers,
reset handling and the separate runtime qualification profile.

## Offline tools and builds

```sh
make build-vial-trial-updater
make build-vial-unlocked-trial-updater
make build-zmk-trial-fixtures
make build-zmk
make build-zmk-right-trial
```

Build scripts load `scripts/project.sh`. Build/test/package commands never
access the keyboard. The dependency-free `trial.py` is included in trial
packages. With Python 3.12+, `python trial.py inspect .` validates a package
and `python trial.py plan . --session 42` emits an offline packet plan.
The ordinary updater package loader rejects trial packages.

The separate `trial_run.py` performs device operations: inspect identities,
upload/seal, request RUN once, then verify the terminal result/reset cause and
recovery health. A successful upload alone does not establish execution or
recovery. Trial watchdog recovery does not guarantee rollback from a faulty
permanent application.
