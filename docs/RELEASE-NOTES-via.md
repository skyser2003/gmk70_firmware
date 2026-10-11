GMK70 QMK/VIA firmware, built from the commit selected by this VIA tag.

- Normal images: `gmk70-via-release` and `gmk70-via-debug`.
- Split updater packages: `gmk70-via-release-updater` and `gmk70-via-debug-updater`.
- Each `.tar.gz` includes firmware, ELF/map files, dependency pins, build information, checksums and matching `source.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. Load the original GMK70 definition in VIA. Install the same build on both halves and back up settings first.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Follow `docs/SPLIT-UPDATER.md`; normal images are not resident updater applications.

These packages target reconstructed stock V15 behavior. Full stock equivalence and validation of the latest changes on hardware remain unverified. Vial firmware is published separately with `vial-X.X.X` tags.
