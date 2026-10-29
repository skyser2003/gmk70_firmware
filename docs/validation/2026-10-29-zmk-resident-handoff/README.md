# LEFT resident-handoff hardware validation

Source implementation: `cc8f290`. [Result JSON](result.json) contains sanitized
identities and health; [workflow and limits](../../ZMK-RESIDENT-HANDOFF.md) explain
the application-only activation mechanism.

| Transition | Activation | Subsequent both-half health |
| --- | --- | --- |
| Old Studio → handoff A | INSTALL acknowledged; old SYSRESETREQ failed 80.125-second observation; user confirmed one joined-LEFT cycle | 61.000 seconds, zero retries/reconnects |
| Handoff A → distinct B | Automatic return; no new user cycle | 60.969 seconds, zero retries/reconnects |
| Handoff B → A | Automatic return; no new user cycle | 60.046 seconds, zero retries/reconnects |

RIGHT uptime continued across both new-path LEFT activations. Final LEFT is
71,684 bytes/CRC32 `2883208108`; RIGHT remains 36,576/`1296309606`. Both resident
CRCs remain `1770733249`. No RIGHT application, resident or radio write was made.

Actual Studio RPC read four layers of 72 positions and behavior metadata, with
no unsaved changes. All original base bindings and the stored Fn name survived.
Updated Fn defaults follow the separately requested Mac/RGB implementation.
Normal Studio was reopened without the temporary loopback inspector and
reconnected to COM3, displaying all four layers. Local RGB is enabled in the
LEFT experiment; visual lighting output and RIGHT lighting are not qualified.

The underlying SYSRESETREQ fault remains undiagnosed. This result qualifies
LEFT activation between these marked experimental images, not RIGHT handoff,
ordinary reboot or every future image/configuration. Native and repository
tests, both experimental builds and default normal/Studio/qualification builds
passed. Changed Python files pass strict mypy/Ruff; the full updater type check
has five pre-existing TypedDict keyword-update errors recorded in the plan.

Private frozen evidence/configuration is retained under
`artifacts/recovery/2026-10-29-resident-handoff/operator-and-evidence/`.
All 77 files were rehashed against its sibling `SHA256SUMS.json`;
the inventory SHA256 is
`ecee418ddf4b7f6d2b555cd1effcccf783d067727e54a7eb9642a4703e668cf8`.
Do not execute frozen operator copies as a resume command. They are provenance;
the live `.work/zmk-warm-restart/` wrapper has one-shot INSTALL markers and
refuses replay. Future hardware work must start with fresh read-only inspection.
