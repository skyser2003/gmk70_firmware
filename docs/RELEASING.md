# GitHub firmware builds and releases

The **Firmware** workflow runs only on pushes to `release/via` or `release/vial`
and selects its build family from that branch. Pushes to `main` or other branches,
pull requests and tag pushes do not start the workflow. There is no manual trigger.
Tests, Python checks and dependency verification must pass before builds begin.
All builds retain the pinned compiler, frameworks, libraries and external-source
adapter.

| Branch | Firmware packages | Automatic release tags |
| --- | --- | --- |
| `release/via` | VIA release/debug, each with a normal and updater package | `via-v0.1.0`, `via-v0.1.1`, ... |
| `release/vial` | Vial normal, updater and unlocked-updater packages | `vial-0.1.0`, `vial-0.1.1`, ... |

Every successful push to a release branch publishes that family's next patch
version. Version counters are independent and compare numeric version parts.
Returning to an already tagged commit reuses its existing release.
Tags point to the exact commit that was built. They are created by the workflow
using `GITHUB_TOKEN`; they do not trigger another build. VIA and Vial have separate
release entries, notes and assets. No combined firmware release is published.

Run artifacts are also available under **Actions → Firmware**: select a successful
release-branch run and download the desired `gmk70-firmware-*` artifact. Artifacts
expire after 30 days. General tests and tooling checks run on both release
branches before the selected family's builds; they do not run automatically on
`main`. For development validation without publishing, use the local commands
below.

To promote current `main` to a release branch, for example:

```sh
git fetch origin
git switch release/via
git merge --ff-only origin/main
git push origin release/via
```

Use `release/vial` instead to promote Vial. There is no version file to edit or
tag to push: the successful release-branch workflow creates the next patch tag.
Manually pushing a tag does not start checks, builds or publication.

After the selected builds succeed, publication verifies all download hashes and
rejects files from another family. It serializes version allocation per family,
creates the tag and draft release, uploads assets, then publishes. Reruns reuse
the same commit's tag and can finish an interrupted draft. An already published
release keeps its original assets, including Vial's original per-build EEPROM
magic. Release branches can advance independently.
Publication retains queued jobs instead of replacing pending releases; GitHub's
per-family queue allows up to 100 pending jobs.

Each variant has a full `.tar.gz` bundle with firmware, checksums, metadata and
matching sources. Normal images also have uniquely named `.bin` and `.hex`
downloads. Vial includes its existing portable `.zip`. Updater images remain
inside separate packages so their `application.bin`, `install.bin` and
`update.json` cannot collide across variants. The release includes an aggregate
`SHA256SUMS` for download verification.

GitHub Actions must be enabled for the repository. The workflow uses the
automatic `GITHUB_TOKEN`; only the publication job requests `contents:
write`. No personal token or GPG private key is needed for builds or publication.
Actions are pinned to commit SHAs, and tool caches are keyed by the dependency
lockfile, setup script and Python lockfiles. Cached tools still go through setup
and dependency verification. No keyboard is flashed by this workflow.

For the same packages locally, after `make setup setup-vial`:

```sh
make test
make check-tools
bash scripts/github-build.sh                 # all eight variants
bash scripts/github-build.sh vial-updater    # one variant
```

Packages are written to `dist/`. Packaging verifies the build's `SHA256SUMS`
and requires its firmware images, update metadata where applicable, and source
archive. Unmanifested stale images are excluded.
