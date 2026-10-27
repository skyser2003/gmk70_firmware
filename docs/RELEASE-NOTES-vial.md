GMK70 Vial firmware, built from the commit selected by this Vial tag.

Battery-level and charging-status queries now display while RGB is off, without
enabling the saved lighting setting. Custom-key descriptions contain only the
role and `CUSTOM(n)`; unused reserved slots have blank labels with identifying
tooltips. All existing custom-key numbers are preserved.

- Normal firmware: `gmk70-vial`.
- Split updater firmware: `gmk70-vial-updater`.
- Permanently unlocked split updater: `gmk70-vial-unlocked-updater` (`VIAL_INSECURE=yes`). Host settings and firmware writes do not require physical unlock in this image.
- Portable `.zip` packages contain firmware and instructions; full `.tar.gz` bundles also include ELF/map files, dependency pins, build information, checksums and matching `source.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. The Vial definition is embedded; secure normal/updater builds use Escape + left Shift for physical unlock. Install the same build on both halves and export your settings before updating. Rebuilt Vial images reset dynamic mappings and macros. Unlocked builds retain the same native settings schema and per-build EEPROM magic behavior.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Secure Vial update writes require physical unlock, including when installing an unlocked image from secure firmware. EEPROM backups and diagnostics are excluded. Follow `docs/SPLIT-UPDATER.md`.

Build the permanently unlocked distribution with `make build-vial-unlocked-updater`;
artifacts are isolated under `artifacts/vial/vial-unlocked-updater/` and build
objects under `.work/vial/vial/unlocked-updater/`. Normal Vial release workflows
include this separately named package alongside secure normal/updater packages.
`make build-vial-unlocked-trial-updater` is a separate opt-in staging experiment
under `artifacts/vial/vial-unlocked-trial-updater/`; it is excluded from ordinary
Vial releases. Its trial RUN command remains disabled, and it uses the existing
resident installer unchanged. Neither build target performs device operations.

Complete stock equivalence and hardware validation of the latest changes remain unverified. QMK/VIA firmware is published separately with `via-vX.X.X` tags.
