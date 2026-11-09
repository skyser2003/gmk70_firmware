#!/usr/bin/env bash
set -euo pipefail
export GMK70_TOOLCHAIN="${GMK70_TOOLCHAIN:-xpack}"
source "$(dirname "$0")/project.sh"

usage() {
    cat <<'EOF'
Usage: bash scripts/docker.sh image
       bash scripts/docker.sh setup [setup|setup-vial|setup-zmk ...]
       bash scripts/docker.sh run COMMAND [ARG ...]
       bash scripts/docker.sh shell

image builds the Linux amd64 tools image; setup initializes all pinned sources
and tools unless specific setup targets are supplied. Only image/setup use the
network. run and shell are offline. Outputs stay in the checkout's artifacts/.

GMK70_DOCKER_IMAGE overrides the local image tag (default: gmk70-build:ubuntu24.04).
GMK70_DOCKER_CA_BUNDLE optionally supplies a PEM CA bundle for HTTPS setup.
GMK70_TOOLCHAIN selects xpack (default) or the separately pinned arm compiler.
JOBS controls firmware build parallelism (default: 8).
EOF
}

operation="${1:-help}"
[[ $# -eq 0 ]] || shift
case "$operation" in
    help|-h|--help) usage; exit 0 ;;
    image|shell) [[ $# -eq 0 ]] || { usage >&2; exit 2; } ;;
    setup)
        if [[ $# -eq 0 ]]; then set -- setup setup-vial setup-zmk; fi
        for target in "$@"; do
            case "$target" in
                setup|setup-vial|setup-zmk) ;;
                *) printf 'Unsupported setup target: %s\n' "$target" >&2; exit 2 ;;
            esac
        done ;;
    run) [[ $# -gt 0 ]] || { usage >&2; exit 2; } ;;
    *) usage >&2; exit 2 ;;
esac

command -v docker >/dev/null || { printf 'Install Docker Engine or Docker Desktop with WSL2 integration.\n' >&2; exit 2; }
builder_image="${GMK70_DOCKER_IMAGE:-gmk70-build:ubuntu24.04}"
ca_bundle=""
if [[ "$operation" == image || "$operation" == setup ]]; then
    ca_bundle="${GMK70_DOCKER_CA_BUNDLE:-}"
    # Managed cloud sessions provide a combined bundle containing the session CA.
    if [[ -z "$ca_bundle" && -n "${CODEX_PROXY_CERT:-}" ]]; then
        ca_bundle=/etc/ssl/certs/ca-certificates.crt
    fi
    if [[ -n "$ca_bundle" ]]; then
        [[ -f "$ca_bundle" ]] || { printf 'Missing CA bundle: %s\n' "$ca_bundle" >&2; exit 2; }
        ca_bundle="$(realpath "$ca_bundle")"
    fi
fi

if [[ "$operation" == image ]]; then
    build_args=(build --platform linux/amd64 --tag "$builder_image")
    if [[ -n "$ca_bundle" ]]; then
        build_args+=(--secret "id=proxy_ca,src=$ca_bundle")
    fi
    exec docker "${build_args[@]}" "$project_root/docker"
fi

docker image inspect "$builder_image" >/dev/null 2>&1 || {
    printf 'Missing %s; run make docker-image first.\n' "$builder_image" >&2
    exit 2
}

# Bind caches rather than volumes so the caller owns all outputs, including on
# the first run. Cover each host venv; containers must never reuse a host Python.
cache="$project_root/.work/docker/$GMK70_TOOLCHAIN"
for path in .work .work/docker "$cache" "$cache/work" "$cache/venvs"; do
    [[ ! -L "$path" ]] || { printf 'Refusing redirected Docker cache: %s\n' "$path" >&2; exit 2; }
    mkdir -p "$path"
done
run_args=(run --rm --init --platform linux/amd64 --pull never
    --user "$(id -u):$(id -g)" --workdir "$project_root"
    --mount "type=bind,src=$project_root,dst=$project_root"
    --mount "type=bind,src=$cache/work,dst=$project_root/.work"
    --env "JOBS=${JOBS:-8}" --env "GMK70_TOOLCHAIN=$GMK70_TOOLCHAIN")
for project in tools/build-support tools/keymap-migration tools/battery-diagnostics tools/firmware-update firmware/zmk/tools; do
    key="${project//\//-}"
    [[ ! -L "$cache/venvs/$key" && ! -L "$project/.venv" ]] || {
        printf 'Refusing redirected Python environment: %s\n' "$project" >&2; exit 2
    }
    mkdir -p "$cache/venvs/$key" "$project/.venv"
    run_args+=(--mount "type=bind,src=$cache/venvs/$key,dst=$project_root/$project/.venv")
done

# Linked worktrees refer to metadata outside the mounted checkout. Preserve its
# absolute location so submodule setup and exact-revision verification work.
if [[ -f .git ]]; then
    common_git="$(git rev-parse --path-format=absolute --git-common-dir)"
    case "$common_git" in
        "$project_root"/*) ;;
        *) run_args+=(--mount "type=bind,src=$common_git,dst=$common_git") ;;
    esac
fi

if [[ "$operation" == setup ]]; then
    if [[ -n "$ca_bundle" ]]; then
        run_args+=(--mount "type=bind,src=$ca_bundle,dst=/run/gmk70-ca.pem,readonly"
            --env SSL_CERT_FILE=/run/gmk70-ca.pem
            --env CURL_CA_BUNDLE=/run/gmk70-ca.pem
            --env GIT_SSL_CAINFO=/run/gmk70-ca.pem
            --env REQUESTS_CA_BUNDLE=/run/gmk70-ca.pem
            --env PIP_CERT=/run/gmk70-ca.pem)
    fi
    set -- make "$@"
else
    run_args+=(--network none --env UV_OFFLINE=1)
    if [[ "$operation" == shell ]]; then
        [[ -t 0 && -t 1 ]] && run_args+=(-it)
        set -- bash
    fi
fi
exec docker "${run_args[@]}" "$builder_image" "$@"
