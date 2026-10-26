GMK70 Vial firmware, built from the commit selected by this Vial tag.

Battery-level and charging-status queries now display while RGB is off, without
enabling the saved lighting setting. Custom-key descriptions contain only the
role and `CUSTOM(n)`; unused reserved slots have blank labels with identifying
tooltips. All existing custom-key numbers are preserved.

- Normal firmware: `gmk70-vial`.
- Split updater firmware: `gmk70-vial-updater`.
- Portable `.zip` packages contain firmware and instructions; full `.tar.gz` bundles also include ELF/map files, dependency pins, build information, checksums and matching `source.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. The Vial definition is embedded; physical unlock uses Escape + left Shift. Install the same build on both halves and export your settings before updating. Rebuilt Vial images reset dynamic mappings and macros.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Vial update writes require physical unlock; EEPROM backups and diagnostics are excluded. Follow `docs/SPLIT-UPDATER.md`.

Complete stock equivalence and hardware validation of the latest changes remain unverified. QMK/VIA firmware is published separately with `via-vX.X.X` tags.
