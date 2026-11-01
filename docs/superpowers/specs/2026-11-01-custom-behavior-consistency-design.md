# Custom behavior consistency

The user requested implementation after repeated incomplete parity audits.
This batch closes the four confirmed behavior gaps and records one fixed
acceptance checklist for repository additions across VIA, Vial and ZMK.
Existing uncommitted parity changes are part of the integration baseline.

## Approach

Keep upstream dependencies untouched. Fix each adapter at the boundary where
its custom state meets native input, remapping or persistence. Use native
report/listener/storage paths in regressions where a stub previously concealed
the failure. Reuse shared VIA/Vial code and existing ZMK persistence/storage
machinery. Replacing firmware frameworks or rewriting their settings is outside
this task. The user's "DO IT THEN" authorizes execution without another design
approval round.

## Required outcomes

1. Shared VIA/Vial radio recovery cannot retain keys released during a radio
   disconnect. Reconnect and host transitions cannot resurrect stale reports.
   Native report modifiers, function-flip ownership and current mode gates must
   remain coherent. The physical pairing guard must reject invalid switch input
   rather than interpreting an old logical channel as a physical mode.
2. ZMK pairing holds and queued pairing must cancel on every native path that
   replaces their binding, including Studio Discard and Reset. Cancellation
   applies before a slow reload/reset can race the three-second deadline.
3. ZMK RGB saves retain the latest requested native value through wireless,
   USB loss, busy storage and transient failures. Runtime RGB writes require
   LEFT, wired mode, configured USB, service readiness and storage ownership at
   execution. Startup behavior-ID persistence retains its pre-enumeration path.
4. ZMK custom function/media flip owns held shortcut modifiers independently
   of other native key events. An unrelated press/release or release of one
   overlapping shortcut cannot clear another held shortcut's contribution.
   Real modifiers, native implicit modifiers and unrelated bindings retain their
   native behavior; no blanket upstream modifier rewrite is permitted.

## Constraints

- Preserve residents, settings encodings/partitions, saved keymaps and behavior
  IDs, custom keycode values, original VIA definition and dependency pins.
- Permanent ZMK must not initialize/feed a watchdog. Do not change restart
  activation, run hardware commands, flash, reset settings or read private maps.
- Keep all existing local changes. Work in the original main checkout, which
  contains the current implementation; the attached bf0ccad checkout is historical.
- Source scripts/project.sh before runtime commands. Use pinned offline tools,
  format/lint/type-check edited code, run full suites and affected ARM builds.
- No commit or push is part of this request. Reviews use captured diffs against
  task-start snapshots so existing uncommitted work is not mistaken for new code.
- No new thread, reduced stack, speculative RAM enlargement or generic feature
  expansion. Fit the exact admitted ZMK RAM region, excluding both retained areas.

## Acceptance and review

The checklist in docs/CUSTOM-BEHAVIOR-CHECKLIST.md records normal input, release,
disconnect/reconnect, native remapping/reset, physical mode changes, overlapping
holds, failed writes and settings recovery for the existing custom features.
Each cell names evidence or an explicit limit; a passing unit suite alone does
not imply complete hardware qualification. The known LEFT restart investigation
is deferred by the user and remains outside this batch.
