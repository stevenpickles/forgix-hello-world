# syntax=docker/dockerfile:1
# Unified Forgix build image: every toolchain the build system needs, pinned
# and collocated on one Ubuntu 24.04 LTS base with known /opt paths.
#
# The Efinity installer is registration-gated and the Efinix license forbids
# redistributing it (license.txt 2.3(a)). Because this image contains the
# Efinity tree, the whole image inherits that restriction: it is never built
# by CI and the resulting package must stay PRIVATE. Build and push yourself:
#
#   ./ci/forgix-build/build_image.sh ~/Downloads/efinity-2026.1.132-linux-x64.tar.bz2
#
# Everything except Efinity is downloaded by the Dockerfile itself, checksum-
# pinned below. Stage one unpacks the tarball; the final stage copies only the
# unpacked tree, so the multi-gigabyte archive never becomes a layer of the
# published image.
#
# The ENV block near the bottom is the contract scripts/env.sh defers to:
# variables already set in the environment always win over its Windows
# defaults, so the same scripts run unchanged in here and on the host.

ARG EFINITY_VERSION=2026.1
ARG GHDL_VERSION=6.0.0
ARG ARM_GNU_VERSION=14.3.rel1
ARG PICO_SDK_VERSION=2.3.0
ARG PICOTOOL_VERSION=2.3.0
ARG VSG_VERSION=3.35.0
ARG GCOVR_VERSION=8.6
ARG CEEDLING_VERSION=1.1.2
ARG ARM_GNU_SHA256=8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd
ARG GHDL_SHA256=30d6a977b8456d140bbafecbbe64b1947a3d92eeae8f5e6d9f528a174f9566e7

FROM ubuntu:24.04 AS unpack
ARG EFINITY_VERSION
ARG EFINITY_TARBALL
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install --yes --no-install-recommends bzip2 xz-utils \
 && rm -rf /var/lib/apt/lists/*

COPY ${EFINITY_TARBALL} /tmp/efinity.tar

# The tarball's top-level directory name varies between releases, so find the
# tree that actually holds bin/setup.sh instead of assuming --strip-components.
RUN set -eu; \
    mkdir -p /stage /opt/efinity; \
    tar -xf /tmp/efinity.tar -C /stage; \
    setup="$(find /stage -maxdepth 4 -type f -name setup.sh -path '*/bin/*' | head -n 1)"; \
    if [ -z "$setup" ]; then echo 'no bin/setup.sh in the tarball' >&2; exit 1; fi; \
    root="$(dirname "$(dirname "$setup")")"; \
    mv "$root" /opt/efinity/${EFINITY_VERSION}; \
    rm -rf /stage /tmp/efinity.tar; \
    rm -rf /opt/efinity/${EFINITY_VERSION}/doc \
           /opt/efinity/${EFINITY_VERSION}/examples

# The 2026.1 tree is ~2.7 GB, of which pgm/ (standalone programmer, ~284 MB),
# ipm/ (IP manager, ~451 MB), and debugger/ (~145 MB) are unused by a compile
# flow -- bitstream generation itself runs bin/efx_pgm, which stays. They are
# left in place because only a full CI compile proves a prune was safe; drop
# them above if pull time matters more than that certainty.

# Ubuntu 24.04 ships libstdc++.so.6.0.33 (GCC 14); Efinity bundles 6.0.32.
# Verified against 2026.1's bin/setup.sh: it compares the system version to a
# hardcoded EFX_GLIBCXX_VERSION=32 and, when the system is newer, prints a
# non-fatal WARNING whose own advice is to "back up and remove
# $EFINITY_HOME/lib/libstdc++.so.6" so everything resolves to the system copy,
# which is ABI-backward-compatible by GCC policy. Do exactly that at image
# build time. (The warning still prints -- the comparison is against the
# constant, not the file -- and is harmless noise in the logs.)
# libgcc_s gets the same treatment for the same reason: setup.sh puts Efinity's
# lib dir first on LD_LIBRARY_PATH, and the system libstdc++ must not be made
# to load against an older bundled libgcc_s. Deleting here, in the unpack
# stage, keeps the dead files out of the published COPY layer entirely.
# Failing when nothing matches is deliberate: an empty result means the install
# layout changed and this whole assumption needs re-checking, not skipping.
# The final stage's Efinity gate (setup.sh source, efx_run.py, ldd sweep,
# Interface Designer import) is what proves the deletion was safe.
RUN set -eu; \
    found="$(find /opt/efinity/${EFINITY_VERSION} \
             \( -name 'libstdc++.so*' -o -name 'libgcc_s.so*' \) )"; \
    if [ -z "$found" ]; then \
      echo 'no bundled libstdc++/libgcc_s found; layout changed, re-verify' >&2; \
      exit 1; \
    fi; \
    printf 'deleting bundled runtime libraries:\n%s\n' "$found"; \
    find /opt/efinity/${EFINITY_VERSION} \
         \( -name 'libstdc++.so*' -o -name 'libgcc_s.so*' \) -delete

FROM ubuntu:24.04
ARG EFINITY_VERSION
ARG GHDL_VERSION
ARG ARM_GNU_VERSION
ARG PICO_SDK_VERSION
ARG PICOTOOL_VERSION
ARG VSG_VERSION
ARG GCOVR_VERSION
ARG CEEDLING_VERSION
ARG ARM_GNU_SHA256
ARG GHDL_SHA256
ARG DEBIAN_FRONTEND=noninteractive

LABEL org.opencontainers.image.source="https://github.com/stevenpickles/forgix-hello-world" \
      org.opencontainers.image.description="Private unified Forgix build toolchain (contains licensed Efinity)" \
      forgix.efinity.version="${EFINITY_VERSION}" \
      forgix.ghdl.version="${GHDL_VERSION}" \
      forgix.arm-gnu-toolchain.version="${ARM_GNU_VERSION}" \
      forgix.pico-sdk.version="${PICO_SDK_VERSION}" \
      forgix.picotool.version="${PICOTOOL_VERSION}" \
      forgix.vsg.version="${VSG_VERSION}" \
      forgix.gcovr.version="${GCOVR_VERSION}" \
      forgix.ceedling.version="${CEEDLING_VERSION}"

# One apt layer for every distro dependency. Grouped by consumer:
#   - curl/git/xz/bzip2: fetching the pinned toolchains below, checkout in CI
#   - python3 + python-is-python3: repository scripts invoke `python`
#   - the Qt/X11 half serves Efinity. The whole Qt runtime is required despite
#     nothing ever being displayed, and it is not obvious why. efx_run.py
#     imports PyQt6.QtCore for QSettings (glib); efx_pnr and efx_sta link
#     libX11 directly; and the Interface Designer stage reaches PyQt6.QtGui
#     through qtpy, which needs libGL, libEGL, xkbcommon, fontconfig, freetype
#     and dbus. Dropping the "GUI" half of that list looks safe and is not:
#     see the Efinity gate below for what actually happens.
#   - build-essential/cmake/ninja/pkg-config: picotool build and the host
#     gcc+gcov pair Ceedling compiles and measures with (same apt gcc for
#     both, so gcov's format matches the compiler by construction)
#   - libusb-1.0-0-dev: picotool `load`/`reboot` support
#   - ruby: 3.2 on 24.04, satisfies Ceedling's >= 3.0
#   - clang-format-18 rather than the `clang-format` metapackage: naming the
#     major makes the pin explicit instead of inherited. A new major can
#     reformat compliant code, so bump it only together with a full-tree
#     reformat in the same change.
RUN apt-get update \
 && apt-get install --yes --no-install-recommends \
      bash ca-certificates curl git xz-utils bzip2 \
      python3 python3-venv python-is-python3 \
      zlib1g libgomp1 libglib2.0-0 libx11-6 libgl1 libegl1 libdbus-1-3 \
      libfontconfig1 libfreetype6 libxkbcommon0 \
      build-essential cmake ninja-build pkg-config \
      libusb-1.0-0-dev \
      ruby ruby-dev \
      clang-format-18 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=unpack /opt/efinity /opt/efinity

# EFINITY_USER_DIR_INI is deliberately NOT set here: setup.sh exports it from
# $HOME unconditionally, so anything set at this level is discarded the moment
# the environment is sourced. scripts/run_efinity.sh pins it after the source.
ENV EFINITY_HOME=/opt/efinity/${EFINITY_VERSION}

# Qt must never look for a display: a runner has none, and the Interface
# Designer loads QtGui whether or not anything is drawn.
ENV QT_QPA_PLATFORM=offscreen

# Fails the image build -- not a CI run three weeks later -- if anything the
# compile flow loads is missing. setup.sh uses BASH_SOURCE and [[ ]], so it
# cannot be sourced by the default /bin/sh, and it reads
# SYSTEM_LIBSTDCXX_VERSION before assigning it, so `set -u` has to be relaxed
# across the source -- the same two constraints scripts/run_efinity.sh handles.
# On this 24.04 base the gate carries a second duty: the unpack stage deleted
# Efinity's bundled libstdc++/libgcc_s in favor of the newer system copies,
# and everything below runs against those, so a bad deletion fails here.
#
# The efx_run_pt_unified import is the important half of this gate, and it was
# added the hard way. efx_run.py catches a failed Interface Designer import,
# writes "Skipping Interface Designer step" to a log file nobody reads, and
# carries on: map, pnr and pgm all report PASS, the flow exits 0, and the only
# symptom is that no LPF and therefore no bitstream is ever produced. An ldd
# sweep of the four binaries does not catch it, because the missing library is
# reached through Python rather than linked.
RUN bash -c 'set -eu; \
    set +u; . "$EFINITY_HOME/bin/setup.sh"; set -u; \
    efx_run.py --help > /dev/null; \
    for tool in efx_map efx_pnr efx_pgm efx_sta; do \
      if ldd "$EFINITY_HOME/bin/$tool" | grep -q "not found"; then \
        echo "unresolved shared libraries for $tool:" >&2; \
        ldd "$EFINITY_HOME/bin/$tool" >&2; \
        exit 1; \
      fi; \
    done; \
    cd "$EFINITY_HOME/scripts"; \
    python3 -c "import efx_run_pt_unified" || { \
      echo "the Interface Designer module does not import: the flow would" >&2; \
      echo "silently skip it and produce no bitstream" >&2; \
      exit 1; \
    }'

# Arm GNU Toolchain from Arm's release tarball, not apt: the distro package is
# whatever GCC the release froze (13.2 on 24.04) with no upgrade path, while
# the tarball IS the pin -- the same compiler bytes on every rebuild of this
# image, verified by checksum. The gate links a Cortex-M33 binary because that
# is what proves newlib and the v8-m.main multilibs unpacked intact;
# --version alone only proves the driver runs.
RUN set -eu; \
    curl -fL -o /tmp/arm.tar.xz \
      "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GNU_VERSION}/binrel/arm-gnu-toolchain-${ARM_GNU_VERSION}-x86_64-arm-none-eabi.tar.xz"; \
    echo "${ARM_GNU_SHA256}  /tmp/arm.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/arm-gnu-toolchain; \
    tar -xf /tmp/arm.tar.xz -C /opt/arm-gnu-toolchain; \
    mv "/opt/arm-gnu-toolchain/arm-gnu-toolchain-${ARM_GNU_VERSION}-x86_64-arm-none-eabi" \
       "/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}"; \
    rm /tmp/arm.tar.xz; \
    rm -rf "/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}/share/doc" \
           "/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}/share/man" \
           "/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}/share/info"; \
    bin="/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}/bin"; \
    "$bin/arm-none-eabi-gcc" --version | head -n1; \
    printf 'int main(void){return 0;}\n' > /tmp/smoke.c; \
    "$bin/arm-none-eabi-gcc" -mcpu=cortex-m33 -mthumb --specs=nosys.specs \
      /tmp/smoke.c -o /tmp/smoke.elf; \
    rm /tmp/smoke.c /tmp/smoke.elf

# Pico SDK at the release tag with its direct submodules (TinyUSB among them).
# Top-level only, matching the SDK's own setup instructions: the nested
# submodules under lib/tinyusb are vendor BSPs for other MCU families and are
# unused by an RP2 build. .git is prunable: the SDK's version comes from the
# static pico_sdk_version.cmake, and the Windows flow already runs this SDK
# from a plain archive with no git metadata. The two `test -f` gates are the
# exact files every consumer probes for (scripts/env.sh, build_firmware.sh).
RUN set -eu; \
    git clone --depth 1 --branch "${PICO_SDK_VERSION}" \
      https://github.com/raspberrypi/pico-sdk.git "/opt/pico-sdk/${PICO_SDK_VERSION}"; \
    git -C "/opt/pico-sdk/${PICO_SDK_VERSION}" submodule update --init --depth 1; \
    find "/opt/pico-sdk/${PICO_SDK_VERSION}" -name .git -prune -exec rm -rf {} +; \
    test -f "/opt/pico-sdk/${PICO_SDK_VERSION}/pico_sdk_version.cmake"; \
    test -f "/opt/pico-sdk/${PICO_SDK_VERSION}/lib/tinyusb/src/tusb.c"

# picotool built once, with libusb, flat-installed. FLAT_INSTALL puts the
# binary AND picotoolConfig.cmake in the same <prefix>/picotool directory --
# the shape scripts/env.sh already expects on Windows, so one path serves both
# PICOTOOL_BIN_PATH and picotool_DIR. The SameMajorVersion config-version file
# satisfies the SDK's find_package(picotool 2.3.0), which is what keeps
# release builds from fetching and compiling picotool from git every run.
# `help load` only exists when libusb was compiled in: the copy the SDK would
# build without it can convert a UF2 but cannot load or reboot, which is why
# that check is a gate and not a nicety (see scripts/env.sh).
RUN set -eu; \
    git clone --depth 1 --branch "${PICOTOOL_VERSION}" \
      https://github.com/raspberrypi/picotool.git /tmp/picotool-src; \
    cmake -S /tmp/picotool-src -B /tmp/picotool-build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DPICO_SDK_PATH="/opt/pico-sdk/${PICO_SDK_VERSION}" \
      -DPICOTOOL_FLAT_INSTALL=1 \
      -DCMAKE_INSTALL_PREFIX="/opt/picotool/${PICOTOOL_VERSION}"; \
    cmake --build /tmp/picotool-build --parallel; \
    cmake --install /tmp/picotool-build; \
    rm -rf /tmp/picotool-src /tmp/picotool-build; \
    ptdir="/opt/picotool/${PICOTOOL_VERSION}/picotool"; \
    "$ptdir/picotool" version; \
    test -f "$ptdir/picotoolConfig.cmake"; \
    ldd "$ptdir/picotool" | grep -q libusb-1.0; \
    "$ptdir/picotool" help load > /dev/null

# GHDL from the project's Ubuntu 24.04 release asset. The gate analyzes,
# elaborates, and runs a one-entity design under --std=08 -- the exact shape
# scripts/test.sh drives -- because --version does not prove the analyzer's
# runtime libraries resolve.
RUN set -eu; \
    curl -fL -o /tmp/ghdl.tar.gz \
      "https://github.com/ghdl/ghdl/releases/download/v${GHDL_VERSION}/ghdl-mcode-${GHDL_VERSION}-ubuntu24.04-x86_64.tar.gz"; \
    echo "${GHDL_SHA256}  /tmp/ghdl.tar.gz" | sha256sum -c -; \
    mkdir -p /tmp/ghdl-stage; \
    tar -xzf /tmp/ghdl.tar.gz -C /tmp/ghdl-stage; \
    ghdl_bin="$(find /tmp/ghdl-stage -maxdepth 3 -type f -name ghdl -path '*/bin/*' | head -n 1)"; \
    if [ -z "$ghdl_bin" ]; then echo 'no bin/ghdl in the GHDL tarball' >&2; exit 1; fi; \
    mkdir -p /opt/ghdl; \
    mv "$(dirname "$(dirname "$ghdl_bin")")" "/opt/ghdl/${GHDL_VERSION}"; \
    rm -rf /tmp/ghdl.tar.gz /tmp/ghdl-stage; \
    ghdl="/opt/ghdl/${GHDL_VERSION}/bin/ghdl"; \
    if ldd "$ghdl" | grep -q 'not found'; then ldd "$ghdl" >&2; exit 1; fi; \
    "$ghdl" --version; \
    mkdir /tmp/ghdl-smoke; cd /tmp/ghdl-smoke; \
    printf 'entity e is end; architecture a of e is begin end;\n' > e.vhd; \
    "$ghdl" -a --std=08 e.vhd; \
    "$ghdl" -e --std=08 e; \
    "$ghdl" -r --std=08 e; \
    cd /; rm -rf /tmp/ghdl-smoke

# Ubuntu 24.04 marks the system interpreter externally managed (PEP 668), so
# pip installs live in a venv rather than --break-system-packages: it keeps
# vsg/gcovr and their dependency trees away from both the interpreter the
# repository's own checks run on and the one Efinity's flow ships.
RUN set -eu; \
    python3 -m venv /opt/venv; \
    /opt/venv/bin/pip install --no-cache-dir \
      "vsg==${VSG_VERSION}" "gcovr==${GCOVR_VERSION}"; \
    /opt/venv/bin/vsg --version; \
    /opt/venv/bin/gcovr --version | head -n1

# Ceedling as a system gem, so it stays readable when scripts/test_ceedling.sh
# maps the caller's uid into the container on Linux hosts.
RUN set -eu; \
    gem install ceedling -v "${CEEDLING_VERSION}" --no-document; \
    ceedling version

RUN ln -s /usr/bin/clang-format-18 /usr/local/bin/clang-format \
 && clang-format --version

# The contract block. scripts/env.sh applies these defaults only when unset,
# so everything here wins inside the container while the Windows defaults
# still apply on a host. FORGIX_BUILD_CONTAINER is the marker
# scripts/test_ceedling.sh keys on to run ceedling directly instead of
# attempting docker-in-docker. picotool_DIR mirrors what scripts/env.sh
# exports so that CMake steps that never source env.sh (CI workflows) still
# resolve find_package(picotool).
ENV PICO_SDK_PATH=/opt/pico-sdk/${PICO_SDK_VERSION} \
    PICO_TINYUSB_PATH=/opt/pico-sdk/${PICO_SDK_VERSION}/lib/tinyusb \
    GHDL_BIN_PATH=/opt/ghdl/${GHDL_VERSION}/bin \
    PICOTOOL_BIN_PATH=/opt/picotool/${PICOTOOL_VERSION}/picotool \
    picotool_DIR=/opt/picotool/${PICOTOOL_VERSION}/picotool \
    PICO_TOOLCHAIN_PATH=/opt/arm-gnu-toolchain/${ARM_GNU_VERSION} \
    FORGIX_BUILD_CONTAINER=1 \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PATH=/opt/venv/bin:/opt/arm-gnu-toolchain/${ARM_GNU_VERSION}/bin:/opt/ghdl/${GHDL_VERSION}/bin:/opt/picotool/${PICOTOOL_VERSION}/picotool:$PATH

# A queryable record of what this image contains:
#   docker run --rm <image> cat /etc/forgix-build-release
RUN printf '%s\n' \
      "efinity=${EFINITY_VERSION}" \
      "ghdl=${GHDL_VERSION}" \
      "arm-gnu-toolchain=${ARM_GNU_VERSION}" \
      "pico-sdk=${PICO_SDK_VERSION}" \
      "picotool=${PICOTOOL_VERSION}" \
      "vsg=${VSG_VERSION}" \
      "gcovr=${GCOVR_VERSION}" \
      "ceedling=${CEEDLING_VERSION}" \
      "clang-format=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)" \
      "gcc=$(gcc -dumpfullversion)" \
      "cmake=$(cmake --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)" \
      > /etc/forgix-build-release

# forgix-verify is inlined rather than COPYed because the build context is the
# directory holding the Efinity tarball, not the repository. Quick mode (the
# default) asserts the whole ENV contract and prints every tool version;
# --full additionally compiles a throwaway RP2350 project to UF2, which in one
# gate proves the .git-pruned SDK configures, TinyUSB compiles from where the
# contract says it lives, the Arm toolchain links real SDK code for
# Cortex-M33, and find_package(picotool) resolves to the flat install instead
# of fetching from git.
COPY <<'EOF' /usr/local/bin/forgix-verify
#!/usr/bin/env bash
set -euo pipefail

mode="${1:-quick}"

echo '== forgix-build manifest =='
cat /etc/forgix-build-release
echo

echo '== ENV contract =='
status=0
for name in EFINITY_HOME PICO_SDK_PATH PICO_TINYUSB_PATH GHDL_BIN_PATH \
            PICOTOOL_BIN_PATH picotool_DIR PICO_TOOLCHAIN_PATH; do
  value="${!name:-}"
  if [[ -z "$value" ]]; then
    printf '  %-20s UNSET\n' "$name"; status=1
  elif [[ ! -e "$value" ]]; then
    printf '  %-20s %s  (missing)\n' "$name" "$value"; status=1
  else
    printf '  %-20s %s\n' "$name" "$value"
  fi
done
if [[ "${FORGIX_BUILD_CONTAINER:-}" != "1" ]]; then
  echo '  FORGIX_BUILD_CONTAINER is not 1'; status=1
fi
(( status == 0 ))
echo

echo '== tool versions =='
arm-none-eabi-gcc --version | head -n1
"$GHDL_BIN_PATH/ghdl" --version | head -n1
picotool version
vsg --version
gcovr --version | head -n1
ceedling version
clang-format --version
cmake --version | head -n1
printf 'ninja %s\n' "$(ninja --version)"
gcc --version | head -n1
ruby --version
python --version
echo

# Subshell: sourcing setup.sh rewrites PATH/PYTHONHOME/PYTHONPATH and must not
# leak into the caller (same constraint scripts/run_efinity.sh handles).
# pipefail is relaxed alongside -u: setup.sh's own pipelines are not written
# for it and fail with SIGPIPE (141) under this base, while the same source
# passes under plain `set -eu`.
( set +u +o pipefail
  . "$EFINITY_HOME/bin/setup.sh"
  set -u -o pipefail
  efx_run.py --help > /dev/null && echo 'efx_run.py responds' )

if [[ "$mode" == "--full" ]]; then
  echo
  echo '== full: throwaway RP2350 build to UF2 =='
  work="$(mktemp -d)"
  trap 'rm -rf "$work"' EXIT
  cat > "$work/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(smoke C CXX ASM)
pico_sdk_init()
add_executable(smoke main.c)
target_link_libraries(smoke pico_stdlib)
pico_enable_stdio_usb(smoke 1)
pico_add_extra_outputs(smoke)
CMAKE
  cat > "$work/main.c" <<'MAIN'
#include "pico/stdlib.h"

int main(void) {
  stdio_init_all();
  return 0;
}
MAIN
  cmake -S "$work" -B "$work/build" -G Ninja -DPICO_BOARD=pico2 > /dev/null
  cmake --build "$work/build" > /dev/null
  test -s "$work/build/smoke.uf2"
  echo 'smoke.uf2 generated'
fi

echo
echo 'forgix-verify: OK'
EOF
RUN chmod 0755 /usr/local/bin/forgix-verify \
 && forgix-verify --full

WORKDIR /work
