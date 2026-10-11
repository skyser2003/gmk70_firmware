# GitHub firmware builds and releases

The **Firmware** workflow selects its build family from the branch or tag.
Tests, Python checks and dependency verification must pass before builds begin.
All builds retain the pinned compiler, frameworks, libraries and external-source
adapter.

| Branch | Firmware packages | Automatic release tags |
| --- | --- | --- |
| `release/via` | VIA release/debug, each with a normal and updater package | `via-v0.1.0`, `via-v0.1.1`, ... |
| `release/vial` | Vial normal and updater packages | `vial-0.1.0`, `vial-0.1.1`, ... |
| `main` | All seven variants, including fixed-keymap QMK | Build artifacts only |

Every successful push to a release branch publishes that family's next patch
version. Version counters are independent and compare numeric version parts.
Returning to an already tagged commit reuses its existing release.
Tags point to the exact commit that was built. They are created by the workflow
using `GITHUB_TOKEN`; they do not trigger another build. VIA and Vial have separate
release entries, notes and assets. No combined firmware release is published.

For a development download, open **Actions → Firmware**, select a successful
run, and download the desired `gmk70-firmware-*` artifact. Pull requests to the
release branches build only that family; pull requests to `main` build all seven.
**Run workflow** builds without publishing: release branches select their own
family, while other branches allow an `all`, `via`, or `vial` choice. Run artifacts
expire after 30 days.

To promote current `main` to a release branch, for example:

```sh
git fetch origin
git switch release/via
git merge --ff-only origin/main
git push origin release/via
```

Use `release/vial` instead to promote Vial. There is no version file to edit.
For a deliberate major/minor version, push a signed tag such as `via-v1.0.0` or
`vial-1.0.0` at the intended commit; this builds and publishes only the matching
family. Legacy `v*` tags and `vial-v*` tags do not publish releases.

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
bash scripts/github-build.sh                 # all seven variants
bash scripts/github-build.sh vial-updater    # one variant
```

Packages are written to `dist/`. Packaging verifies the build's `SHA256SUMS`
and requires its firmware images, update metadata where applicable, and source
archive. Unmanifested stale images are excluded.
