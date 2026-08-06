# Tool locations for the Forgix build scripts. Source this file; do not run it.
#
#   source ./scripts/env.sh --print
#
# Every script that needs a tool path sources this. Sourcing it yourself is
# only needed when you want to invoke cmake, ninja, or picotool by hand.
#
# Machine-specific installation paths do not belong in this tracked file: they
# live in scripts/env.local.sh -- untracked, gitignored -- which this file
# sources first. Copy scripts/env.local.example.sh to create one. Variables
# already set in the environment always win over both files, so CI and one-off
# overrides are unaffected:
#
#   EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH PICOTOOL_BIN_PATH PICO_TINYUSB_PATH
#   FORGIX_FIRMWARE_BUILD_DIR
#
# Inside the forgix-build container (FORGIX_BUILD_CONTAINER=1) every one of
# them arrives pre-set from the image's ENV contract, so neither file applies
# there -- see ci/forgix-build/Dockerfile.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  printf 'env.sh must be sourced, not executed:\n  source %s\n' "${BASH_SOURCE[0]}" >&2
  exit 1
fi

forgix_env_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# One developer's disk layout is not the repository's default: machine paths
# come only from the untracked local file, sourced before the fallbacks below.
if [[ -f "$forgix_env_root/scripts/env.local.sh" ]]; then
  source "$forgix_env_root/scripts/env.local.sh"
fi

# Defined empty when nothing supplied them, so consumers running under `set -u`
# reach their own "set X to ..." guidance instead of an unbound-variable abort.
: "${EFINITY_HOME:=}"
: "${PICO_SDK_PATH:=}"
: "${GHDL_BIN_PATH:=}"
export EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH

# picotool: prefer the USB-enabled install from ./scripts/build_picotool.sh,
# pinned via PICOTOOL_BIN_PATH in env.local.sh. The copy the Pico SDK fetches
# into build/ is built without libusb -- it can convert a UF2 but has no `load`
# or `reboot`, so it must never be the first choice. The repo-relative probe
# below covers an install dropped into the build tree.
if [[ -z "${PICOTOOL_BIN_PATH:-}" ]]; then
  forgix_env_candidate="$forgix_env_root/build/picotool-2.3.0/picotool"
  if [[ -x "$forgix_env_candidate/picotool.exe" || -x "$forgix_env_candidate/picotool" ]]; then
    PICOTOOL_BIN_PATH="$forgix_env_candidate"
  fi
  unset forgix_env_candidate
fi

if [[ -n "${PICOTOOL_BIN_PATH:-}" ]]; then
  export PICOTOOL_BIN_PATH
  # find_package(picotool) in the SDK reads this.
  export picotool_DIR="$PICOTOOL_BIN_PATH"
  case ":$PATH:" in
    *":$PICOTOOL_BIN_PATH:"*) ;;
    *) export PATH="$PICOTOOL_BIN_PATH:$PATH" ;;
  esac
fi

# TinyUSB ships as a Pico SDK submodule. A source archive without submodules
# needs either PICO_TINYUSB_PATH or a checkout in build/tinyusb.
if [[ -z "${PICO_TINYUSB_PATH:-}" ]]; then
  if command -v cygpath >/dev/null 2>&1; then
    forgix_env_sdk_posix="$(cygpath -u "$PICO_SDK_PATH" 2>/dev/null || printf '%s' "$PICO_SDK_PATH")"
  else
    forgix_env_sdk_posix="$PICO_SDK_PATH"
  fi
  for forgix_env_candidate in \
      "$forgix_env_sdk_posix/lib/tinyusb" \
      "$forgix_env_root/build/tinyusb"; do
    if [[ -f "$forgix_env_candidate/src/tusb.c" ]]; then
      export PICO_TINYUSB_PATH="$forgix_env_candidate"
      break
    fi
  done
  unset forgix_env_candidate forgix_env_sdk_posix
fi

# Separate firmware build trees per platform: a CMake cache records absolute
# paths, so the Windows tree and the forgix-build container cannot share one
# directory -- CMake refuses the mismatch outright when the same checkout is
# mounted into the container. Decided here, once, so the script that builds
# into the tree and the script that flashes out of it cannot disagree about
# where it is. cygpath presence is the platform signal, as in build_fpga.sh.
if [[ -z "${FORGIX_FIRMWARE_BUILD_DIR:-}" ]]; then
  if command -v cygpath >/dev/null 2>&1; then
    FORGIX_FIRMWARE_BUILD_DIR="$forgix_env_root/build/firmware"
  else
    FORGIX_FIRMWARE_BUILD_DIR="$forgix_env_root/build/firmware-linux"
  fi
fi
export FORGIX_FIRMWARE_BUILD_DIR

forgix_env_report() {
  local name value
  printf 'Forgix tool environment\n'
  for name in EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH PICOTOOL_BIN_PATH PICO_TINYUSB_PATH; do
    value="${!name:-}"
    if [[ -z "$value" ]]; then
      printf '  %-18s (not found)\n' "$name"
    elif [[ -e "$value" ]]; then
      printf '  %-18s %s\n' "$name" "$value"
    else
      printf '  %-18s %s  (missing)\n' "$name" "$value"
    fi
  done
}

if [[ "${1:-}" == "--print" ]]; then
  forgix_env_report
fi

unset forgix_env_root
