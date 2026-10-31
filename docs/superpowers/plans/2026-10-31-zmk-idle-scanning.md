# GMK70 interrupt-triggered idle scanning

The user authorized implementation, commit and push after pulling main.
Base: `6a4524c`, including the shared hardware safeguards and NKRO work.

Use board-local WB32 GPIO edge interrupts and retain the existing combined
scanner/maintenance thread. Reusing the upstream scanner immediately would
also require replacing its level-trigger requirement and separating GMK70's
runtime role, UART matrix polling and lighting integration. Full STOP is a
larger clock/radio/split restoration change and is outside this matrix change.

- [x] Pull latest main without changing unrelated research worktrees.
- [x] Implement GPIO EXTI callbacks, edge/port mapping and group-specific W1C.
- [x] Stop local matrix sweeps only after candidates and held keys clear.
- [x] Check inputs after arming and retain polling on interrupt setup failure.
- [x] Retain split polling, mode gates, RGB, updater, settings and NKRO.
- [x] Add actual-driver native cases for IRQ groups, arming races and both roles.
- [x] Format edited C and run full repository/native/tooling checks.
- [x] Compile affected ZMK variants, inspect image/RAM/resident results.
- [x] Record software evidence, review diff, sign and commit for push to main.

Device operations are excluded. Hardware validation and measured battery life
remain separate from the offline implementation and builds.
