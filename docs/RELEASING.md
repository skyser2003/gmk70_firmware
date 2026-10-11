# GitHub firmware builds and releases

The **Firmware** workflow builds all seven supported distributions on pull
requests to `main`, pushes to `main`, `v*` tags, and manual runs. Tests, Python
checks and dependency verification must pass before builds begin. Each build
uses the pinned compiler, frameworks and library revisions with the existing
external-source adapter.

For a development download, open **Actions → Firmware**, select a successful
run, and download the desired `gmk70-firmware-*` artifact. **Run workflow** builds
the selected branch without publishing a release. Run artifacts expire after
30 days.

To publish a version, tag the intended commit and push the tag:

```sh
git tag -s v1.0.0 -m 'GMK70 firmware v1.0.0'
git push origin v1.0.0
```

Choose the actual next version; `v1.0.0` is an example. The tag triggers fresh
checks and builds. After all seven builds succeed, the workflow rechecks
downloaded package hashes, uploads assets to a draft GitHub Release, and
publishes it only after all uploads succeed. A failed upload can be rerun while
the release is still a draft. Published releases are not overwritten by reruns;
use a new version tag for a changed build.

Each variant has a full `.tar.gz` bundle with firmware, checksums, metadata and
matching sources. Normal images also have uniquely named `.bin` and `.hex`
downloads. Vial includes its existing portable `.zip`. Updater images remain
inside separate packages so their `application.bin`, `install.bin` and
`update.json` cannot collide across variants. The release includes an aggregate
`SHA256SUMS` for download verification.

GitHub Actions must be enabled for the repository. The workflow uses the
automatic `GITHUB_TOKEN`; only the tag-publication job requests `contents:
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
