GMK70 firmware packages for the commit selected by this tag.

- Normal images: `gmk70-qmk-default`, `gmk70-via-release`, `gmk70-via-debug`, and `gmk70-vial`.
- Split updater packages: `gmk70-via-release-updater`, `gmk70-via-debug-updater`, and `gmk70-vial-updater`.
- Each `.tar.gz` contains firmware, ELF/map files, dependency pins, checksums, build information and matching `source.tar.gz`.
- Vial `.zip` files are portable firmware packages; full sources are in the corresponding `.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. Install the same build on both halves and back up settings first. Rebuilt Vial images reset dynamic mappings and macros.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Do not substitute a normal image or `install.bin` for `application.bin`. Vial update writes require physical unlock.

These packages target reconstructed stock V15 behavior. Full stock equivalence and validation of the latest changes on hardware remain unverified. See the repository's README and `docs/SPLIT-UPDATER.md` for installation details.
