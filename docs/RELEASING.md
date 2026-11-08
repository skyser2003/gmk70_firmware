# GitHub firmware builds and releases

The **Firmware** workflow runs only on pushes to supported release branches.
The branch selects its firmware family. General tests, Python checks and
dependency verification run on those release pushes and must pass before builds begin.
All builds retain the pinned compiler, frameworks, libraries and external-source
adapter.

| Branch | Firmware packages | Automatic release tags |
| --- | --- | --- |
| `release/via` | VIA release/debug, each with a normal and updater package | `via-v0.1.0`, `via-v0.1.1`, ... |
| `release/vial` | Vial normal, secure updater and unlocked updater packages | `vial-0.1.0`, `vial-0.1.1`, ... |
| `release/zmk` | Universal ZMK Studio application/updater | `zmk-v0.1.0`, `zmk-v0.1.1`, ... |
| `main` or any other branch | No workflow, including general checks | None |

Every successful push to a release branch publishes that family's next patch
version. Version counters are independent and compare numeric version parts.
Returning to an already tagged commit reuses its existing release.
Tags point to the exact commit that was built. They are created by the workflow
using `GITHUB_TOKEN`; they do not trigger another build. VIA, Vial and ZMK have separate
release entries, notes and assets. No combined firmware release is published.

PRs, tag pushes and manual events do not start this workflow, even when they
refer to a release branch. The planner also rejects those contexts instead of
producing a build matrix. For a download, open the family's release or select a
successful release-branch run under **Actions → Firmware**. Its
`gmk70-firmware-*` artifacts expire after 30 days.

Shared checks run for each release family. ZMK dependency setup and native ZMK
tests additionally run on `release/zmk`; the build matrix publishes only that
branch's family. VIA/Vial release pushes do not initialize or build ZMK.

To promote current `main` to a release branch, for example:

```sh
git fetch origin
git switch release/via
git merge --ff-only origin/main
git push origin release/via
```

Use `release/vial` or `release/zmk` instead to promote that family. For the first
ZMK release, create `release/zmk` at the intended committed revision and push it.
There is no version file to edit.
Version tags are publication outputs, not workflow triggers. Pushing a tag
alone never runs checks, builds or publication.

After the selected builds succeed, publication verifies all download hashes and
rejects files from another family. It serializes version allocation per family,
creates the tag and draft release, uploads assets, then publishes. Reruns reuse
the same commit's tag and can finish an interrupted draft. An already published
release keeps its original assets, including Vial's original per-build EEPROM
magic. Release branches can advance independently.
Publication retains queued jobs instead of replacing pending releases; GitHub's
per-family queue allows up to 100 pending jobs.

Each variant has a full `.tar.gz` bundle with firmware, checksums, metadata and
matching sources. Normal images also have uniquely named `.bin`
downloads. Vial and ZMK include portable `.zip` packages. Updater images remain
inside separate packages so their `application.bin`, `install.bin` and
`update.json` cannot collide across variants. The release includes an aggregate
`SHA256SUMS` for download verification.

All firmware families distribute BIN only. Normal VIA/Vial images and full
`install.bin` images start at `0x08000000`; `application.bin` starts at
`0x08002000` and is used with `update.json` by the resident updater. HEX files
are excluded from build manifests and packages; publication rejects obsolete
HEX assets. Existing published releases are unchanged.

ZMK publishes only `wired-studio-updater`, packaged as `gmk70-zmk`. Its identical
application serves both halves and includes the unlocked native updater. The
ZIP contains `install.bin` for first installation on each verified
256 KiB half, plus `application.bin`, `update.json`, the pinned resident reference,
image validation and instructions. Full installers erase ZMK settings; routine
application updates preserve them. Images stay inside the named package.
Legacy QMK/Vial migrations through the split updater require the separately
built native-update bridge; direct installation on each half does not. See the
[ZMK release notes](RELEASE-NOTES-zmk.md). Trials, bridges and qualification
variants are excluded. CI builds the QMK debug-updater resident prerequisite
separately, then the ZMK packager requires its exact reviewed hash. That QMK
prerequisite is not published as part of the ZMK family.

The ZMK source bundle extends the shared source exporter with pinned
ZMK/Zephyr/modules and a separate integrity manifest. Upstream internal source
symlinks are preserved; escaping links and Git metadata are excluded. Extracted
sources can be verified and built without fetching firmware source repositories.
Build/test commands never access the keyboard.

GitHub Actions must be enabled for the repository. The workflow uses the
automatic `GITHUB_TOKEN`; only the publication job requests `contents:
write`. No personal token or GPG private key is needed for builds or publication.
Actions are pinned to commit SHAs, and tool caches are keyed by the dependency
lockfile, setup script and Python lockfiles. Cached tools still go through setup
and dependency verification. No keyboard is flashed by this workflow.

Local commands remain available independently of the Actions trigger policy.
For the same packages locally, after `make setup setup-vial setup-zmk`:

```sh
make test test-zmk
make check-tools
make build-updater-debug                    # reviewed resident prerequisite
bash scripts/github-build.sh                 # all nine variants
bash scripts/github-build.sh vial-updater    # one variant
bash scripts/github-build.sh zmk             # universal Studio release bundle
```

Packages are written to `dist/`. Packaging verifies the build's `SHA256SUMS`
and requires its firmware images, update metadata where applicable, and source
archive. Unmanifested stale images are excluded.
`make build-zmk-release` produces the release-ready ZMK files under
`artifacts/zmk/wired-studio-updater/`; the GitHub wrapper additionally writes
the public ZIP, full TAR bundle and download checksums under `dist/`.
