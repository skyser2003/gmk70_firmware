FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

# Session-specific trust is mounted only for this step, never baked into layers.
RUN --mount=type=secret,id=proxy_ca,mode=0444 \
    set -eu; \
    if [ -f /run/secrets/proxy_ca ]; then \
        export APT_CONFIG=/tmp/gmk70-apt.conf; \
        printf 'Acquire::https::CaInfo "/run/secrets/proxy_ca";\n' > "$APT_CONFIG"; \
        sed -i 's|http://|https://|g' /etc/apt/sources.list.d/ubuntu.sources; \
    fi; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl device-tree-compiler file git \
        libhidapi-hidraw0 libusb-1.0-0 python3 python3-venv unzip xz-utils; \
    rm -rf /var/lib/apt/lists/* /tmp/gmk70-apt.conf

# setup.sh installs the repository's checksum-pinned ARM GCC and uv into the
# mounted container cache. Framework sources are initialized explicitly
# through the existing setup targets, never downloaded by a firmware build.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8 UV_PYTHON_DOWNLOADS=never GMK70_TOOLCHAIN=xpack
COPY --chmod=0755 entrypoint.sh /usr/local/bin/gmk70-entrypoint
ENTRYPOINT ["bash", "/usr/local/bin/gmk70-entrypoint"]
CMD ["bash"]
