# Firmware releases

Push to the corresponding release branch to run checks, build firmware and
publish the next patch version. Pushing `main`, tags or other branches does not
run the workflow. No manual tag or Actions trigger is needed.

| Branch | Packages | Tags |
| --- | --- | --- |
| `release/via` | VIA release/debug, normal and updater | `via-vX.X.X` |
| `release/vial` | Vial normal, updater and unlocked updater | `vial-X.X.X` |
| `release/zmk` | ZMK Studio updater and standalone | `zmk-vX.X.X` |

Promote `main`, for example:

```sh
git fetch origin
git switch release/zmk
git merge --ff-only origin/main
git push origin release/zmk
```

Downloads contain only firmware and checksums:

- Normal VIA/Vial and standalone ZMK: one `.bin` per variant.
- Updaters: one `.zip` containing `application.bin`, `install.bin`,
  `resident.bin`, the required `update.json` and `FIRMWARE-SHA256SUMS`.
- The unlocked Vial package is `gmk70-vial-unlocked.zip`.
- Each variant has a `.sha256` file; `SHA256SUMS` covers the whole release.

Source archives, ELF/map files, configuration dumps, logs, readers and
instructions are not release assets. Sources and installation instructions are
in this repository. Local build directories retain development outputs.
GitHub also supplies its own automatic tag-source links.

Release notes list commit changes since the previous tag in the same family.
They contain no repeated feature lists, installation guide or build instructions.
Tags point to the exact built commit. Retries preserve an already published
release instead of rebuilding/replacing its firmware.

For local release packages after dependency setup:

```sh
bash scripts/github-build.sh via-release
bash scripts/github-build.sh vial-unlocked-updater
make build-updater-debug
bash scripts/github-build.sh zmk
bash scripts/github-build.sh zmk-standalone
```

Files are written to `dist/`. The packager verifies build checksums before
selecting firmware, then publication checks the exact public filenames and ZIP
contents. `make build-zmk-release` builds and packages the ZMK updater in the
same way. Build and test commands never access the keyboard.

An explicit `.github/repackage-releases.json` request selects existing tags and
commit hashes for packaging-only corrections. Its matching release-branch push
runs checks and repackages those published bytes without compiling new firmware
or creating a version. Downloads and uploads are hash-verified before obsolete
assets are removed. Remove the request after the correction to restore normal
build-and-publish behavior.
