# Headless Efinity image for CI synthesis of the Forgix T8F49 design.
#
# The Efinity installer is registration-gated and the Efinix license forbids
# redistributing it (license.txt 2.3(a)), so this file is never built by CI and
# the resulting image must stay a PRIVATE package. Build and push it yourself:
#
#   ./ci/efinity/build_image.sh ~/Downloads/efinity-2026.1.132-linux-x64.tar.bz2
#
# Stage one unpacks; the final stage copies only the unpacked tree, so the
# multi-gigabyte tarball never becomes a layer of the published image.

ARG EFINITY_VERSION=2026.1

FROM ubuntu:22.04 AS unpack
ARG EFINITY_VERSION
ARG EFINITY_TARBALL

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

FROM ubuntu:22.04
ARG EFINITY_VERSION

# The base is pinned between two bounds that bin/setup.sh checks at runtime:
# glibc must be at least 2.28, and the system libstdc++.so.6 must not be newer
# than the 6.0.32 Efinity bundles or setup.sh tells you to delete its own copy.
# Ubuntu 22.04 sits inside both (glibc 2.35, libstdc++.so.6.0.30). Re-check both
# before moving to a later base.
# The whole Qt runtime is required despite nothing ever being displayed, and it
# is not obvious why. efx_run.py imports PyQt6.QtCore for QSettings (glib);
# efx_pnr and efx_sta link libX11 directly; and the Interface Designer stage
# reaches PyQt6.QtGui through qtpy, which needs libGL, libEGL, xkbcommon,
# fontconfig, freetype and dbus. Dropping the "GUI" half of that list looks
# safe and is not: see the gate below for what actually happens.
RUN apt-get update \
 && apt-get install --yes --no-install-recommends \
      bash ca-certificates git python3 python-is-python3 zlib1g libgomp1 \
      libglib2.0-0 libx11-6 libgl1 libegl1 libdbus-1-3 libfontconfig1 \
      libfreetype6 libxkbcommon0 \
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

WORKDIR /work
