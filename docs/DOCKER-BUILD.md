# Docker build environment

One Linux amd64 image builds QMK/VIA, QMK/Vial and ZMK using the existing Make
targets. Install Docker Engine with BuildKit, or Docker Desktop with WSL2
integration, plus Git, Bash and make. On Windows, clone and run inside the WSL2
Linux filesystem. An ARM host requires Docker's amd64 emulation; the pinned
compiler is x86_64. A local daemon must be able to bind the checkout's paths.

## Setup and builds

From the repository root:

```sh
make docker-image
make docker-setup
make docker-build
```

`docker-image` installs system tools in an Ubuntu 24.04 image pinned by digest.
APT packages receive the versions available when the image is built; this is
not a claim of byte-for-byte reproducible system packages. `docker-setup` runs
`make setup setup-vial setup-zmk` with network access: the repository's existing
scripts verify the xPack ARM GCC 14.2.1-1.1 and uv 0.8.22 archive checksums, initialize
the locked framework commits and install the frozen Python environments.
Neither image creation nor firmware compilation copies maintained sources.

Setup must finish successfully before building. Re-run setup after changing
dependency pins or Python lockfiles. Rebuild the image after Dockerfile changes.

### Select the compiler

Docker defaults to `GMK70_TOOLCHAIN=xpack`, using xPack ARM GCC 14.2.1-1.1
from its official GitHub release. No compiler override is needed for the setup
and build commands above. To use the original Arm GNU 14.2.rel1 archive instead:

```sh
export GMK70_TOOLCHAIN=arm
make docker-setup
make docker-build docker-test
```

Both archives have separate URL/version/SHA-256 pins in
`dependencies/sources.lock.json`. Setup does not silently switch distributions
on download failure. Keep any override set for setup, builds, tests and shells
(or record it in the root `.env`). To return to the Docker default, unset
`GMK70_TOOLCHAIN` and remove any override in `.env`/`.envrc`.
The actual compiler identity is included in every variant's `build-info.txt`.
ZMK's required resident hash is checked with either compiler.

Native setup/build commands retain their original `arm` default and also support
`GMK70_TOOLCHAIN=xpack`. Clear the generated `.work/qmk/`, `.work/vial/` and
`.work/zmk/` build caches before changing native compilers. If a native shell has
already loaded `scripts/project.sh`, its exported selector also applies to Docker;
unset it or explicitly select `xpack` before running Docker commands.

| Command | Result |
| --- | --- |
| `make docker-build` | VIA release/debug, normal Vial, the VIA debug resident prerequisite, then universal ZMK Studio |
| `make docker-build-via` | VIA release/debug |
| `make docker-build-vial` | Normal Vial |
| `make docker-build-zmk` | VIA debug resident prerequisite, then universal ZMK Studio |
| `make docker-test` | `make test check-tools test-zmk` |
| `make docker-shell` | Interactive offline shell with the project environment loaded |

Run other existing build targets explicitly, for example:

```sh
bash scripts/docker.sh run make build-updater build-vial-updater
JOBS=2 make docker-build
```

`run` and `shell` use Docker's `--network none` and uv's offline mode. They never
initialize dependencies automatically. `setup` may also take explicit setup
targets, for example `bash scripts/docker.sh setup setup-vial` after the common
setup has completed. The wrappers do not expose USB devices or the Docker socket.
Build and test commands never flash, reset or write to a keyboard.

## Files and ownership

The checkout is mounted at its original absolute path. Outputs remain under
`artifacts/qmk/`, `artifacts/vial/` and `artifacts/zmk/wired-studio-updater/`.
The container runs as the invoking UID/GID, so outputs remain owned by that user.
Run the wrapper as your regular user with Docker access.

Container intermediates, downloads and toolchains live on the host under
`.work/docker/<arm|xpack>/work/`. Each container Python environment lives under
`.work/docker/<arm|xpack>/venvs/` and is mounted over its project's `.venv/`.
The separate caches prevent reuse of objects built with the other compiler.
Existing native `.work/` contents and Python environments are preserved.
Framework dependencies and artifacts are shared with native builds; do not run native and container
builds concurrently against the same checkout. `make clean` in the container
cleans container intermediates according to the existing project clean rules.

Git linked worktrees are supported: their common Git metadata directory is also
mounted at its original location for submodule setup and source verification.
The main checkout's working files are not mounted. Source distributions can use
the wrapper where their existing setup scripts support the included frameworks;
an archive containing only VIA sources cannot build Vial or ZMK without those
families' source distributions.

The root `.env`/`.envrc` is loaded through `scripts/project.sh` on both sides.
Keep it compatible with Linux container paths. Do not put host-only paths or
automatic device operations there. No project files, `.env`, credentials, or
firmware sources enter the image build context: the context is only `docker/`.

## Proxies and private certificate authorities

Use Docker's normal daemon/client proxy configuration for pulls, image builds
and dependency setup. The wrapper preserves the selected Docker context and
registry credentials. To trust an inspecting proxy, supply a combined PEM bundle
containing public roots and the proxy CA:

```sh
export GMK70_DOCKER_CA_BUNDLE=/absolute/path/to/ca-bundle.pem
make docker-image docker-setup
```

In a managed cloud session with `CODEX_PROXY_CERT`, the system combined bundle
is selected automatically. The bundle is a BuildKit secret during image creation
and a read-only mount during setup; it is never installed into image layers.
TLS verification stays enabled. `GMK70_DOCKER_IMAGE` optionally selects a
different local image tag; set it consistently for image, setup and builds.

Allow the destinations used by the existing setup scripts, including GitHub and
its release asset hosts, Python package indexes and Ubuntu package servers.
The default xPack distribution downloads from GitHub release assets. The optional
`arm` distribution also requires `developer.arm.com` and its compiler redirect
`armkeil.blob.core.windows.net`.
An HTTP 403 at the proxy is not proof that a compiler archive is missing. If
setup cannot download the selected archive, repair that route or explicitly
select the other pinned distribution. Keep TLS verification enabled and report
which compiler was actually used.
